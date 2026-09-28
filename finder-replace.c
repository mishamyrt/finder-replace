#include <ApplicationServices/ApplicationServices.h>
#include <CoreServices/CoreServices.h>
#include <dispatch/dispatch.h>
#include <libproc.h>
#include <limits.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define PREFERENCES_DOMAIN CFSTR("co.myrt.finder-replace")
#define DOCK_PATH "/System/Library/CoreServices/Dock.app/Contents/MacOS/Dock"
#define AX_TIMEOUT_SECONDS 0.1f
#define AX_SEARCH_BUDGET_NS UINT64_C(250000000)

static AXUIElementRef system_ax;
static CFURLRef target_url;
static CFMachPortRef tap;
static atomic_bool opening;
static bool swallowing;
static bool debug;

static CFURLRef application_url(CFTypeRef path) {
    if (!path || CFGetTypeID(path) != CFStringGetTypeID() ||
        !CFStringHasPrefix(path, CFSTR("/"))) {
        return NULL;
    }

    CFURLRef url =
        CFURLCreateWithFileSystemPath(NULL, path, kCFURLPOSIXPathStyle, true);
    CFBundleRef bundle = url ? CFBundleCreate(NULL, url) : NULL;
    CFStringRef extension = url ? CFURLCopyPathExtension(url) : NULL;
    CFURLRef executable = bundle ? CFBundleCopyExecutableURL(bundle) : NULL;
    UInt8 executable_path[PATH_MAX];
    bool valid = extension && CFEqual(extension, CFSTR("app")) && executable &&
                 CFURLGetFileSystemRepresentation(
                     executable,
                     true,
                     executable_path,
                     sizeof(executable_path)
                 ) &&
                 access((const char *)executable_path, X_OK) == 0;
    if (executable) {
        CFRelease(executable);
    }
    if (extension) {
        CFRelease(extension);
    }
    if (bundle) {
        CFRelease(bundle);
    }
    if (valid) {
        return url;
    }
    if (url) {
        CFRelease(url);
    }

    return NULL;
}

static bool ax_time_left(uint64_t deadline) {
    uint64_t now = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
    if (now >= deadline) {
        if (debug) {
            fputs("AX: search budget exhausted\n", stderr);
        }
        return false;
    }
    float timeout = (float)(deadline - now) / 1e9f;
    if (timeout > AX_TIMEOUT_SECONDS) {
        timeout = AX_TIMEOUT_SECONDS;
    }

    return AXUIElementSetMessagingTimeout(system_ax, timeout) == kAXErrorSuccess;
}

static CFTypeRef
attribute(AXUIElementRef element, CFStringRef name, uint64_t deadline) {
    if (!ax_time_left(deadline)) {
        return NULL;
    }

    CFTypeRef value = NULL;
    AXError error = AXUIElementCopyAttributeValue(element, name, &value);
    if (debug) {
        char label[64] = "?";
        CFStringGetCString(name, label, sizeof(label), kCFStringEncodingUTF8);
        fprintf(stderr, "%s: error=%d", label, error);
        if (value) {
            fputs(" value=", stderr);
            CFShow(value);
        } else {
            fputc('\n', stderr);
        }
    }
    return value;
}

static bool finder_url(CFTypeRef value) {
    if (!value || CFGetTypeID(value) != CFURLGetTypeID()) {
        return false;
    }
    CFStringRef path = CFURLCopyFileSystemPath(value, kCFURLPOSIXPathStyle);
    bool match =
        path && CFEqual(path, CFSTR("/System/Library/CoreServices/Finder.app"));
    if (path) {
        CFRelease(path);
    }
    return match;
}

static AXUIElementRef element_at(CGPoint point, uint64_t deadline) {
    for (int attempt = 1; attempt <= 2 && ax_time_left(deadline); ++attempt) {
        AXUIElementRef element = NULL;
        AXError error =
            AXUIElementCopyElementAtPosition(system_ax, point.x, point.y, &element);
        if (debug) {
            fprintf(
                stderr,
                "hit-test (%.0f, %.0f), attempt=%d: error=%d\n",
                point.x,
                point.y,
                attempt,
                error
            );
        }
        if (error == kAXErrorSuccess) {
            return element;
        }
        if (element) {
            CFRelease(element);
        }
        // A cold Dock can miss the first AX reply. Retry this read once, within
        // budget.
        if (error != kAXErrorCannotComplete) {
            break;
        }
    }
    return NULL;
}

static bool finder_at(CGPoint point) {
    uint64_t deadline =
        clock_gettime_nsec_np(CLOCK_UPTIME_RAW) + AX_SEARCH_BUDGET_NS;
    AXUIElementRef element = element_at(point, deadline);
    if (!element) {
        return false;
    }

    pid_t pid = 0;
    char executable[PROC_PIDPATHINFO_MAXSIZE] = "";
    bool found = false;
    AXError pid_error = AXUIElementGetPid(element, &pid);
    int path_size = pid_error == kAXErrorSuccess
                        ? proc_pidpath(pid, executable, sizeof(executable))
                        : 0;
    if (debug) {
        fprintf(
            stderr,
            "hit process: pid=%d error=%d path=%s\n",
            pid,
            pid_error,
            executable
        );
    }

    if (pid_error != kAXErrorSuccess || path_size <= 0 ||
        strcmp(executable, DOCK_PATH) != 0) {
        goto done;
    }

    // A cached rectangle alone is unsafe with Dock magnification and auto-hide.
    for (int depth = 0; depth < 6 && element; ++depth) {
        CFTypeRef role = attribute(element, kAXRoleAttribute, deadline);
        bool item = role && CFEqual(role, kAXDockItemRole);
        if (role) {
            CFRelease(role);
        }
        if (item) {
            CFTypeRef subrole = attribute(element, kAXSubroleAttribute, deadline);
            bool app = subrole && CFEqual(subrole, kAXApplicationDockItemSubrole);
            if (subrole) {
                CFRelease(subrole);
            }
            if (app) {
                CFTypeRef url = attribute(element, kAXURLAttribute, deadline);
                found = finder_url(url);
                if (url) {
                    CFRelease(url);
                }
            }
            break;
        }
        CFTypeRef parent = attribute(element, kAXParentAttribute, deadline);
        CFRelease(element);
        element = NULL;
        if (!parent) {
            break;
        }
        if (CFGetTypeID(parent) != AXUIElementGetTypeID()) {
            CFRelease(parent);
            break;
        }
        element = (AXUIElementRef)parent;
    }
done:
    if (element) {
        CFRelease(element);
    }
    bool in_budget = clock_gettime_nsec_np(CLOCK_UPTIME_RAW) < deadline;
    if (debug) {
        fprintf(stderr, "Finder match=%d within-budget=%d\n", found, in_budget);
    }
    return found && in_budget;
}

enum Action { PASS, SWALLOW, OPEN_APP };

static enum Action mouse_action(bool *active, CGEventType type, bool finder) {
    if (type == kCGEventLeftMouseDown) {
        *active = finder;
        return finder ? OPEN_APP : PASS;
    }
    if (type == kCGEventLeftMouseUp) {
        bool was_active = *active;
        *active = false;
        return was_active ? SWALLOW : PASS;
    }
    return type == kCGEventLeftMouseDragged && *active ? SWALLOW : PASS;
}

static void open_target(void *unused) {
    (void)unused;
    OSStatus status = LSOpenCFURLRef(target_url, NULL);
    if (debug) {
        fprintf(stderr, "Target launch: status=%d\n", (int)status);
    }
    if (status != noErr) {
        fprintf(stderr, "Could not open configured application: %d\n", (int)status);
    }
    atomic_store(&opening, false);
}

static CGEventRef
on_event(CGEventTapProxy proxy, CGEventType type, CGEventRef event, void *unused) {
    (void)proxy;
    (void)unused;
    if (type == kCGEventTapDisabledByTimeout ||
        type == kCGEventTapDisabledByUserInput) {
        if (debug) {
            fprintf(
                stderr,
                "Event tap disabled (%u); re-enabling\n",
                (unsigned)type
            );
        }
        // Keep a swallowed down paired with its up; the next down resets stale
        // state.
        CGEventTapEnable(tap, true);
        return event;
    }
    CGEventFlags modifiers = kCGEventFlagMaskControl | kCGEventFlagMaskCommand |
                             kCGEventFlagMaskAlternate | kCGEventFlagMaskShift;
    if (debug && type == kCGEventLeftMouseDown) {
        fprintf(
            stderr,
            "left-down: modifiers=0x%llx\n",
            (unsigned long long)(CGEventGetFlags(event) & modifiers)
        );
    }
    bool finder = type == kCGEventLeftMouseDown &&
                  !(CGEventGetFlags(event) & modifiers) &&
                  finder_at(CGEventGetLocation(event));
    enum Action action = mouse_action(&swallowing, type, finder);
    if (debug && type == kCGEventLeftMouseDown) {
        fprintf(
            stderr,
            "click: %s\n",
            action == PASS ? "pass" : "suppress and open configured application"
        );
    }
    if (action == OPEN_APP && !atomic_exchange(&opening, true)) {
        dispatch_async_f(
            dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0),
            NULL,
            open_target
        );
    }
    return action == PASS ? event : NULL;
}

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        puts(APP_VERSION);
        return 0;
    }
    debug = argc == 2 && strcmp(argv[1], "--debug") == 0;
    if (argc != 1 && !debug) {
        fprintf(stderr, "Usage: %s [--debug | --version | --help]\n", argv[0]);
        return argc == 2 && strcmp(argv[1], "--help") == 0 ? 0 : 2;
    }
    CFTypeRef path = CFPreferencesCopyValue(
        CFSTR("ApplicationPath"),
        PREFERENCES_DOMAIN,
        kCFPreferencesCurrentUser,
        kCFPreferencesAnyHost
    );
    if (!path ||
        (CFGetTypeID(path) == CFStringGetTypeID() && CFStringGetLength(path) == 0)) {
        if (path) {
            CFRelease(path);
        }
        fputs("ApplicationPath is empty; Finder clicks are unchanged.\n", stderr);
        return 0;
    }
    target_url = application_url(path);
    CFRelease(path);
    if (!target_url) {
        fputs(
            "ApplicationPath must be an absolute path to an existing runnable .app "
            "bundle.\n",
            stderr
        );
        return 1;
    }
    const void *keys[] = {kAXTrustedCheckOptionPrompt};
    const void *values[] = {kCFBooleanTrue};
    CFDictionaryRef options = CFDictionaryCreate(
        NULL,
        keys,
        values,
        1,
        &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks
    );
    bool trusted = AXIsProcessTrustedWithOptions(options);
    CFRelease(options);
    if (!trusted) {
        fputs(
            "Allow the process named in the system prompt (your terminal or "
            "finder-replace) "
            "in System Settings > Privacy & Security > Accessibility, "
            "then rerun this command.\n",
            stderr
        );
        CFRelease(target_url);
        return 1;
    }
    system_ax = AXUIElementCreateSystemWide();
    CGEventMask mask = CGEventMaskBit(kCGEventLeftMouseDown) |
                       CGEventMaskBit(kCGEventLeftMouseUp) |
                       CGEventMaskBit(kCGEventLeftMouseDragged);
    tap = CGEventTapCreate(
        kCGSessionEventTap,
        kCGHeadInsertEventTap,
        kCGEventTapOptionDefault,
        mask,
        on_event,
        NULL
    );
    if (!tap) {
        fputs(
            "Cannot create event tap. Check Accessibility and Input Monitoring "
            "permissions, then rerun this command.\n",
            stderr
        );
        CFRelease(target_url);
        CFRelease(system_ax);
        return 1;
    }
    CFRunLoopSourceRef source = CFMachPortCreateRunLoopSource(NULL, tap, 0);
    CFRunLoopAddSource(CFRunLoopGetMain(), source, kCFRunLoopCommonModes);
    CFRelease(source);
    CGEventTapEnable(tap, true);
    fputs(
        "Listening: Finder in Dock -> configured application. Ctrl+C to stop.\n",
        stderr
    );
    CFRunLoopRun();
    return 0;
}
