#include <ApplicationServices/ApplicationServices.h>
#include <assert.h>
#include <libproc.h>
#include <stdio.h>

static AXError hit_errors[3];
static int hit_calls;
static AXUIElementRef hit_element;
static float last_timeout;
static CFTypeRef configured_path;
static CGPoint hit_point;
static CGPoint first_hit_point;
static bool dock_strip;
static CFURLRef hit_url;
static bool hit_is_dock = true;
static CGRect display_bounds[] = {
    {{0, 0}, {1920, 1080}},
    {{204, 1080}, {1512, 982}},
    {{-1280, 0}, {1280, 1024}},
};
static CGError display_error;

static CGError
fake_displays(uint32_t capacity, CGDirectDisplayID *displays, uint32_t *count) {
    *count = sizeof(display_bounds) / sizeof(display_bounds[0]);
    assert(capacity >= *count);
    for (uint32_t i = 0; i < *count; ++i) {
        displays[i] = i;
    }
    return display_error;
}

static CGRect fake_display_bounds(CGDirectDisplayID display) {
    return display_bounds[display];
}

static CFPropertyListRef fake_preference(
    CFStringRef key,
    CFStringRef app,
    CFStringRef user,
    CFStringRef host
) {
    assert(CFEqual(key, CFSTR("ApplicationPath")));
    assert(CFEqual(app, CFSTR("co.myrt.finder-replace")));
    assert(CFEqual(user, kCFPreferencesCurrentUser));
    assert(CFEqual(host, kCFPreferencesAnyHost));
    return configured_path ? CFRetain(configured_path) : NULL;
}

static AXError
fake_hit_test(AXUIElementRef application, float x, float y, AXUIElementRef *out) {
    (void)application;
    hit_point = CGPointMake(x, y);
    if (hit_calls == 0) {
        first_hit_point = hit_point;
    }
    assert(hit_calls < 3);
    AXError error = hit_errors[hit_calls++];
    if (dock_strip) {
        error = y >= 2057 ? kAXErrorNoValue : kAXErrorSuccess;
    }
    *out = error == kAXErrorSuccess ? (AXUIElementRef)CFRetain(hit_element) : NULL;
    return error;
}

static AXError fake_timeout(AXUIElementRef element, float seconds) {
    (void)element;
    last_timeout = seconds;
    return kAXErrorSuccess;
}

static AXError fake_pid(AXUIElementRef element, pid_t *pid) {
    assert(element == hit_element);
    *pid = 123;
    return kAXErrorSuccess;
}

static int fake_pidpath(int pid, void *buffer, uint32_t size) {
    assert(pid == 123);
    return snprintf(
        buffer,
        size,
        "/System/Library/CoreServices/%s.app/Contents/MacOS/%s",
        hit_is_dock ? "Dock" : "Finder",
        hit_is_dock ? "Dock" : "Finder"
    );
}

static AXError
fake_attribute(AXUIElementRef element, CFStringRef name, CFTypeRef *value) {
    assert(element == hit_element);
    *value = NULL;
    if (CFEqual(name, kAXRoleAttribute)) {
        *value = CFRetain(kAXDockItemRole);
    } else if (CFEqual(name, kAXSubroleAttribute)) {
        *value = CFRetain(kAXApplicationDockItemSubrole);
    } else if (CFEqual(name, kAXURLAttribute) && hit_url) {
        *value = CFRetain(hit_url);
    }
    return *value ? kAXErrorSuccess : kAXErrorNoValue;
}

#define AXUIElementGetPid fake_pid
#define proc_pidpath fake_pidpath
#define AXUIElementCopyAttributeValue fake_attribute
#define AXUIElementCopyElementAtPosition fake_hit_test
#define AXUIElementSetMessagingTimeout fake_timeout
#define CFPreferencesCopyValue fake_preference
#define CGGetActiveDisplayList fake_displays
#define CGDisplayBounds fake_display_bounds
#define main app_main
#include "finder-replace.c"
#undef main
#undef AXUIElementGetPid
#undef proc_pidpath
#undef AXUIElementCopyAttributeValue
#undef AXUIElementCopyElementAtPosition
#undef AXUIElementSetMessagingTimeout
#undef CFPreferencesCopyValue
#undef CGGetActiveDisplayList
#undef CGDisplayBounds

int main(void) {
    char *args[] = {"finder-replace", "--help", NULL};
    assert(app_main(2, args) == 0);
    args[1] = "--invalid";
    assert(app_main(2, args) == 2);
    args[1] = "--version";
    assert(app_main(2, args) == 0);

    // No configuration must exit before asking for AX permissions or installing a
    // tap.
    assert(app_main(1, args) == 0 && tap == NULL);
    configured_path = CFSTR("");
    assert(app_main(1, args) == 0 && tap == NULL);
    configured_path = kCFBooleanTrue;
    assert(app_main(1, args) == 1 && tap == NULL);
    configured_path = CFSTR("/nonexistent/finder-replace-test.app");
    assert(app_main(1, args) == 1 && tap == NULL);
    configured_path = NULL;
    assert(application_url(CFSTR("relative.app")) == NULL);
    assert(application_url(CFSTR("/Applications")) == NULL);
    CFURLRef valid_app =
        application_url(CFSTR("/System/Library/CoreServices/Finder.app"));
    assert(valid_app != NULL);
    CFRelease(valid_app);

    // The reported bottom edge is outside AX, but still receives Dock clicks.
    const CGPoint edge_cases[][2] = {
        {{521, 2062}, {521, 2054}},
        {{520.25, 2061.999}, {520.25, 2054}},
        {{520.25, 2061.5}, {520.25, 2054}},
        {{521, 2061}, {521, 2054}},
        {{1920, 500}, {1912, 500}},
        {{1919.999, 500.25}, {1912, 500.25}},
        {{1716, 2062}, {1708, 2054}},
        {{1715.999, 2061.999}, {1708, 2054}},
        {{204, 2062}, {204, 2054}},
        {{521, 1080}, {521, 1080}},  // Shared display edge stays on its screen.
        {{0, 500}, {0, 500}},
        {{-500, 1024}, {-500, 1016}},
        {{-1280, 500}, {-1280, 500}},
        {{845, 654}, {845, 654}},
        {{521, 2063}, {521, 2063}},  // Do not pull off-screen points into Dock.
    };
    for (size_t i = 0; i < sizeof(edge_cases) / sizeof(edge_cases[0]); ++i) {
        assert(
            CGPointEqualToPoint(ax_screen_point(edge_cases[i][0]), edge_cases[i][1])
        );
    }
    display_error = kCGErrorFailure;
    assert(CGPointEqualToPoint(
        ax_screen_point(CGPointMake(521, 2062)),
        CGPointMake(521, 2062)
    ));
    display_error = kCGErrorSuccess;

    // Reproduce the reported first-click failure, then a successful AX reply.
    hit_element = AXUIElementCreateSystemWide();
    hit_errors[0] = kAXErrorCannotComplete;
    hit_errors[1] = kAXErrorSuccess;
    uint64_t deadline =
        clock_gettime_nsec_np(CLOCK_UPTIME_RAW) + AX_SEARCH_BUDGET_NS;
    AXUIElementRef recovered = element_at(CGPointMake(521, 2061.5), deadline);
    assert(recovered == hit_element && hit_calls == 2);
    assert(CGPointEqualToPoint(hit_point, CGPointMake(521, 2061.5)));
    assert(last_timeout > 0 && last_timeout <= AX_TIMEOUT_SECONDS);
    CFRelease(recovered);

    hit_calls = 0;
    hit_errors[0] = kAXErrorSuccess;
    recovered = element_at(CGPointZero, deadline);
    assert(recovered == hit_element && hit_calls == 1);
    CFRelease(recovered);

    hit_calls = 0;
    hit_errors[0] = hit_errors[1] = kAXErrorCannotComplete;
    assert(element_at(CGPointZero, deadline) == NULL && hit_calls == 2);
    hit_calls = 0;
    hit_errors[0] = kAXErrorNoValue;
    assert(element_at(CGPointZero, deadline) == NULL && hit_calls == 1);
    hit_calls = 0;
    assert(element_at(CGPointZero, 0) == NULL && hit_calls == 0);
    // Reproduce the measured five-point gap; check live Finder identification.
    dock_strip = true;
    hit_url = CFURLCreateWithString(
        NULL,
        CFSTR("file:///System/Library/CoreServices/Finder.app/"),
        NULL
    );
    hit_calls = 0;
    assert(finder_at(CGPointMake(479.5, 2061.984375)));
    assert(hit_calls == 2);
    assert(CGPointEqualToPoint(first_hit_point, CGPointMake(479.5, 2061.984375)));
    assert(CGPointEqualToPoint(hit_point, CGPointMake(479.5, 2054)));
    hit_is_dock = false;
    hit_calls = 0;
    assert(!finder_at(CGPointMake(479.5, 2061.984375)) && hit_calls == 2);
    hit_is_dock = true;
    CFRelease(hit_url);
    hit_url = CFURLCreateWithString(
        NULL,
        CFSTR("file:///Applications/Telegram.app/"),
        NULL
    );
    hit_calls = 0;
    assert(!finder_at(CGPointMake(526.64453125, 2061.984375)) && hit_calls == 2);
    CFRelease(hit_url);
    hit_url = NULL;
    dock_strip = false;

    // Preserve a successful original edge hit; only NoValue permits projection.
    hit_calls = 0;
    hit_errors[0] = kAXErrorSuccess;
    recovered = element_at(CGPointMake(521, 2061.5), deadline);
    assert(recovered == hit_element && hit_calls == 1);
    assert(CGPointEqualToPoint(hit_point, CGPointMake(521, 2061.5)));
    CFRelease(recovered);
    hit_calls = 0;
    hit_errors[0] = hit_errors[1] = kAXErrorNoValue;
    assert(element_at(CGPointMake(521, 2061.5), deadline) == NULL && hit_calls == 2);
    for (int retry_first = 0; retry_first < 2; ++retry_first) {
        hit_calls = 0;
        hit_errors[retry_first] = kAXErrorNoValue;
        hit_errors[1 - retry_first] = kAXErrorCannotComplete;
        hit_errors[2] = kAXErrorSuccess;
        recovered = element_at(CGPointMake(521, 2061.5), deadline);
        assert(recovered == hit_element && hit_calls == 3);
        CFRelease(recovered);
    }
    hit_calls = 0;
    hit_errors[0] = kAXErrorAPIDisabled;
    assert(element_at(CGPointMake(521, 2061.5), deadline) == NULL && hit_calls == 1);
    CFRelease(hit_element);

    bool active = false;
    assert(mouse_action(&active, kCGEventLeftMouseDown, false) == PASS);
    assert(mouse_action(&active, kCGEventLeftMouseDragged, false) == PASS);
    assert(mouse_action(&active, kCGEventLeftMouseUp, false) == PASS);
    for (int click = 0; click < 2; ++click) {
        assert(mouse_action(&active, kCGEventLeftMouseDown, true) == OPEN_APP);
        assert(mouse_action(&active, kCGEventLeftMouseDragged, false) == SWALLOW);
        assert(mouse_action(&active, kCGEventRightMouseDown, false) == PASS);
        assert(mouse_action(&active, kCGEventLeftMouseUp, false) == SWALLOW);
        assert(!active);
        assert(mouse_action(&active, kCGEventLeftMouseUp, false) == PASS);
    }
    // A missing mouse-up must not poison the next ordinary click.
    assert(mouse_action(&active, kCGEventLeftMouseDown, true) == OPEN_APP);
    assert(mouse_action(&active, kCGEventLeftMouseDown, false) == PASS);
    assert(mouse_action(&active, kCGEventLeftMouseUp, false) == PASS);

    CFURLRef finder = CFURLCreateWithString(
        NULL,
        CFSTR("file:///System/Library/CoreServices/Finder.app/"),
        NULL
    );
    CFURLRef other =
        CFURLCreateWithString(NULL, CFSTR("file:///Applications/Finder.app/"), NULL);
    assert(finder_url(finder));
    assert(!finder_url(other));
    assert(!finder_url(NULL));
    assert(!finder_url(CFSTR("Finder")));
    CFRelease(finder);
    CFRelease(other);
    puts(
        "OK: display edges, AX retry and deadline, click pairing, drag, double "
        "click, recovery, "
        "Finder URL"
    );
    return 0;
}
