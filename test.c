#include <ApplicationServices/ApplicationServices.h>
#include <assert.h>

static AXError hit_errors[2];
static int hit_calls;
static AXUIElementRef hit_element;
static float last_timeout;
static CFTypeRef configured_path;

static CFPropertyListRef fake_preference(CFStringRef key, CFStringRef app,
                                         CFStringRef user, CFStringRef host) {
    assert(CFEqual(key, CFSTR("ApplicationPath")));
    assert(CFEqual(app, CFSTR("co.myrt.finder-replace")));
    assert(CFEqual(user, kCFPreferencesCurrentUser));
    assert(CFEqual(host, kCFPreferencesAnyHost));
    return configured_path ? CFRetain(configured_path) : NULL;
}

static AXError fake_hit_test(AXUIElementRef application, float x, float y, AXUIElementRef *out) {
    (void)application;
    (void)x;
    (void)y;
    assert(hit_calls < 2);
    AXError error = hit_errors[hit_calls++];
    *out = error == kAXErrorSuccess ? (AXUIElementRef)CFRetain(hit_element) : NULL;
    return error;
}

static AXError fake_timeout(AXUIElementRef element, float seconds) {
    (void)element;
    last_timeout = seconds;
    return kAXErrorSuccess;
}

#define AXUIElementCopyElementAtPosition fake_hit_test
#define AXUIElementSetMessagingTimeout fake_timeout
#define CFPreferencesCopyValue fake_preference
#define main app_main
#include "finder-replace.c"
#undef main
#undef AXUIElementCopyElementAtPosition
#undef AXUIElementSetMessagingTimeout
#undef CFPreferencesCopyValue

int main(void) {
    char *args[] = { "finder-replace", "--help", NULL };
    assert(app_main(2, args) == 0);
    args[1] = "--invalid";
    assert(app_main(2, args) == 2);
    args[1] = "--version";
    assert(app_main(2, args) == 0);

    // No configuration must exit before asking for AX permissions or installing a tap.
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
    CFURLRef valid_app = application_url(CFSTR("/System/Library/CoreServices/Finder.app"));
    assert(valid_app != NULL);
    CFRelease(valid_app);

    // Reproduce the reported first-click failure, then a successful AX reply.
    hit_element = AXUIElementCreateSystemWide();
    hit_errors[0] = kAXErrorCannotComplete;
    hit_errors[1] = kAXErrorSuccess;
    uint64_t deadline = clock_gettime_nsec_np(CLOCK_UPTIME_RAW) + AX_SEARCH_BUDGET_NS;
    AXUIElementRef recovered = element_at(CGPointZero, deadline);
    assert(recovered == hit_element && hit_calls == 2);
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

    CFURLRef finder = CFURLCreateWithString(NULL,
        CFSTR("file:///System/Library/CoreServices/Finder.app/"), NULL);
    CFURLRef other = CFURLCreateWithString(NULL,
        CFSTR("file:///Applications/Finder.app/"), NULL);
    assert(finder_url(finder));
    assert(!finder_url(other));
    assert(!finder_url(NULL));
    assert(!finder_url(CFSTR("Finder")));
    CFRelease(finder);
    CFRelease(other);
    puts("OK: AX retry and deadline, click pairing, drag, double click, recovery, Finder URL");
    return 0;
}
