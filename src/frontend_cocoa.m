/* macOS window, keyboard, and file picker. Compiled with ARC (-fobjc-arc). */
#import <Cocoa/Cocoa.h>
#import <CoreVideo/CoreVideo.h>
#import <QuartzCore/QuartzCore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "frontend.h"

static NSWindow *g_window = nil;
static CALayer *g_layer = nil;               /* each frame is handed to this layer */
static CGColorSpaceRef g_colorspace = NULL;
static id g_controller = nil;
static bool g_quit = false;
static bool g_show_keys = false;             /* BOYDMG_SHOW_KEYS=1 prints each key press */

/* Display sync: the display link calls us on every screen refresh */
static CVDisplayLinkRef g_display_link = NULL;
static dispatch_semaphore_t g_refresh_signal = nil;

/* ---------------- Window content ---------------- */

@interface DMGView : NSView
@end

@implementation DMGView
- (BOOL)acceptsFirstResponder { return YES; }

/* Keys are handled in frontend_process_events. These empty handlers stop
   macOS from playing its "unhandled key" alert sound. */
- (void)keyDown:(NSEvent *)event { (void)event; }
- (void)keyUp:(NSEvent *)event { (void)event; }
@end

/* Handles closing the window and the Quit menu item */
@interface DMGController : NSObject <NSWindowDelegate>
@end

@implementation DMGController
- (void)windowWillClose:(NSNotification *)notification { (void)notification; g_quit = true; }
- (void)quit:(id)sender { (void)sender; g_quit = true; }   /* lets main save the game before exiting */
@end

/* ---------------- Setup ---------------- */

static void ensure_app(void) {
    static bool ready = false;
    if (ready) return;
    ready = true;

    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    g_controller = [[DMGController alloc] init];

    /* Minimal menu bar so Cmd+Q works */
    NSMenu *bar = [[NSMenu alloc] init];
    NSMenuItem *appItem = [[NSMenuItem alloc] init];
    [bar addItem:appItem];
    NSMenu *appMenu = [[NSMenu alloc] init];
    NSMenuItem *quit = [[NSMenuItem alloc] initWithTitle:@"Quit BoyDMG" action:@selector(quit:) keyEquivalent:@"q"];
    [quit setTarget:g_controller];
    [appMenu addItem:quit];
    [appItem setSubmenu:appMenu];
    [NSApp setMainMenu:bar];

    [NSApp finishLaunching];
}

/* Makes the emulator the active app, so key presses go to it instead of Terminal */
static void bring_to_front(void) {
#if defined(__MAC_OS_X_VERSION_MAX_ALLOWED) && __MAC_OS_X_VERSION_MAX_ALLOWED >= 140000
    if (@available(macOS 14.0, *)) {
        [NSApp activate];
        return;
    }
#endif
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    [NSApp activateIgnoringOtherApps:YES];
#pragma clang diagnostic pop
}

bool frontend_pick_rom(char *out_path, size_t size) {
    ensure_app();
    @autoreleasepool {
        NSOpenPanel *panel = [NSOpenPanel openPanel];
        [panel setTitle:@"Choose a Game Boy ROM"];
        [panel setCanChooseDirectories:NO];
        [panel setAllowsMultipleSelection:NO];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        [panel setAllowedFileTypes:@[@"gb", @"gbc"]];
#pragma clang diagnostic pop
        NSString *roms = [[[NSFileManager defaultManager] currentDirectoryPath]
                             stringByAppendingPathComponent:@"roms"];
        [panel setDirectoryURL:[NSURL fileURLWithPath:roms isDirectory:YES]];

        bring_to_front();
        if ([panel runModal] != NSModalResponseOK) return false;
        snprintf(out_path, size, "%s", [[panel URL] fileSystemRepresentation]);
        return true;
    }
}

/* CVDisplayLink is marked deprecated on the newest macOS, but it still works
   everywhere and is the simplest way to follow the screen's refresh. */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

/* Runs on a background thread at every screen refresh. It only signals the main loop. */
static CVReturn on_refresh(CVDisplayLinkRef link, const CVTimeStamp *now, const CVTimeStamp *output_time,
                           CVOptionFlags flags_in, CVOptionFlags *flags_out, void *context) {
    (void)link; (void)now; (void)output_time; (void)flags_in; (void)flags_out; (void)context;
    dispatch_semaphore_signal(g_refresh_signal);
    return kCVReturnSuccess;
}

static void start_display_sync(void) {
    g_refresh_signal = dispatch_semaphore_create(0);
    if (CVDisplayLinkCreateWithActiveCGDisplays(&g_display_link) != kCVReturnSuccess) {
        g_display_link = NULL;           /* main.c falls back to a timer */
        return;
    }
    CVDisplayLinkSetOutputCallback(g_display_link, on_refresh, NULL);
    if (CVDisplayLinkStart(g_display_link) != kCVReturnSuccess) {
        CVDisplayLinkRelease(g_display_link);
        g_display_link = NULL;
    }
}

double frontend_refresh_rate(void) {
    if (!g_display_link) return 0;
    double period = CVDisplayLinkGetActualOutputVideoRefreshPeriod(g_display_link);   /* seconds */
    if (period <= 0) {
        CVTime nominal = CVDisplayLinkGetNominalOutputVideoRefreshPeriod(g_display_link);
        if (!(nominal.flags & kCVTimeIsIndefinite) && nominal.timeValue > 0) {
            period = (double)nominal.timeValue / (double)nominal.timeScale;
        }
    }
    return period > 0 ? 1.0 / period : 0;
}

#pragma clang diagnostic pop

int frontend_wait_for_refresh(void) {
    if (!g_display_link) return -1;
    /* Wait up to 100 ms, so the game keeps going even if refreshes stop (display asleep) */
    if (dispatch_semaphore_wait(g_refresh_signal, dispatch_time(DISPATCH_TIME_NOW, 100 * NSEC_PER_MSEC)) != 0) {
        return 0;
    }
    int refreshes = 1;
    while (dispatch_semaphore_wait(g_refresh_signal, DISPATCH_TIME_NOW) == 0) {
        refreshes++;                     /* count any refreshes we fell behind on */
    }
    return refreshes;
}

void frontend_set_title(const char *title) {
    [g_window setTitle:[NSString stringWithUTF8String:title]];
}

void frontend_init(int scale, const char *title) {
    ensure_app();
    const char *show = getenv("BOYDMG_SHOW_KEYS");
    g_show_keys = show && show[0] == '1';
    if (g_show_keys) fprintf(stderr, "Key check is on: every key you press is listed here.\n");

    NSRect frame = NSMakeRect(0, 0, SCREEN_WIDTH * scale, SCREEN_HEIGHT * scale);
    g_window = [[NSWindow alloc] initWithContentRect:frame
                                           styleMask:(NSWindowStyleMaskTitled |
                                                      NSWindowStyleMaskClosable |
                                                      NSWindowStyleMaskMiniaturizable)
                                             backing:NSBackingStoreBuffered
                                               defer:NO];
    [g_window setReleasedWhenClosed:NO];     /* ARC owns the window; closing must not free it */
    [g_window setDelegate:g_controller];
    [g_window setTitle:[NSString stringWithUTF8String:title]];

    DMGView *view = [[DMGView alloc] initWithFrame:frame];

    /* The view hosts a Core Animation layer that we fill directly each frame.
       Handing frames to the layer puts them on screen right away. */
    g_layer = [CALayer layer];
    g_layer.magnificationFilter = kCAFilterNearest;     /* sharp pixels instead of blurry */
    g_layer.contentsGravity = kCAGravityResize;
    g_layer.backgroundColor = [[NSColor whiteColor] CGColor];   /* blank Game Boy screen until the first frame */
    g_layer.opaque = YES;
    [view setLayer:g_layer];                            /* set the layer first, then turn it on: */
    [view setWantsLayer:YES];                           /* this order means we own the layer */
    g_colorspace = CGColorSpaceCreateDeviceRGB();

    [g_window setContentView:view];
    [g_window center];
    bring_to_front();
    [g_window makeKeyAndOrderFront:nil];
    [g_window orderFrontRegardless];
    [g_window makeFirstResponder:view];
    start_display_sync();
}

/* ---------------- Each frame ---------------- */

void frontend_update(const uint32_t *framebuffer) {
    @autoreleasepool {
        /* Wrap the pixels in an image. The framebuffer's bytes are red, green,
           blue, alpha in memory, which is what "skip last" describes. */
        CFDataRef data = CFDataCreate(NULL, (const UInt8 *)framebuffer,
                                      SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(uint32_t));
        CGDataProviderRef provider = CGDataProviderCreateWithCFData(data);
        CGImageRef image = CGImageCreate(SCREEN_WIDTH, SCREEN_HEIGHT, 8, 32, SCREEN_WIDTH * 4,
                                         g_colorspace,
                                         (CGBitmapInfo)kCGImageAlphaNoneSkipLast | kCGBitmapByteOrderDefault,
                                         provider, NULL, false, kCGRenderingIntentDefault);

        [CATransaction begin];
        [CATransaction setDisableActions:YES];   /* no fade animation between frames */
        g_layer.contents = (__bridge id)image;
        [CATransaction commit];
        [CATransaction flush];                   /* send it to the screen now, not later */

        CGImageRelease(image);
        CGDataProviderRelease(provider);
        CFRelease(data);
    }
}

/* Mac key codes to Game Boy buttons */
static int button_for_key(unsigned short code) {
    switch (code) {
        case 6:   return BTN_A;       /* Z */
        case 7:   return BTN_B;       /* X */
        case 36:  return BTN_START;   /* Return */
        case 51:  return BTN_SELECT;  /* Backspace (Shift also works, in frontend_process_events) */
        case 124: return BTN_RIGHT;
        case 123: return BTN_LEFT;
        case 126: return BTN_UP;
        case 125: return BTN_DOWN;
        default:  return -1;
    }
}

/* Matches by key position first (arrows, Return, Backspace), then by the
   letter itself, so Z and X work on any keyboard layout (QWERTZ, AZERTY, ...). */
static int button_for_event(NSEvent *event) {
    int button = button_for_key([event keyCode]);
    if (button >= 0) return button;
    NSString *chars = [[event charactersIgnoringModifiers] lowercaseString];
    if ([chars isEqualToString:@"z"]) return BTN_A;
    if ([chars isEqualToString:@"x"]) return BTN_B;
    return -1;
}

static void show_key(NSEvent *event, int button) {
    static const char *names[8] = {"A", "B", "Select", "Start", "Right", "Left", "Up", "Down"};
    const char *chars = [[event charactersIgnoringModifiers] UTF8String];
    fprintf(stderr, "key code %3d  '%s'  ->  %s\n", [event keyCode],
            (chars && chars[0] >= 0x20 && chars[0] < 0x7F) ? chars : "?",
            button >= 0 ? names[button] : "(not a Game Boy button)");
}

bool frontend_process_events(Joypad *joypad) {
    @autoreleasepool {
        NSEvent *event;
        while ((event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                           untilDate:nil
                                              inMode:NSDefaultRunLoopMode
                                             dequeue:YES])) {
            NSEventType type = [event type];
            bool command = ([event modifierFlags] & NSEventModifierFlagCommand) != 0;

            if ((type == NSEventTypeKeyDown || type == NSEventTypeKeyUp) && !command) {
                int button = button_for_event(event);
                if (g_show_keys && type == NSEventTypeKeyDown && ![event isARepeat]) show_key(event, button);
                if (button >= 0) {
                    if (type == NSEventTypeKeyDown) joypad_press(joypad, button);
                    else joypad_release(joypad, button);
                }
                continue;                    /* keep game keys away from macOS */
            }

            if (type == NSEventTypeFlagsChanged) {
                /* Shift doesn't send key-down events, only modifier changes */
                if ([event modifierFlags] & NSEventModifierFlagShift) joypad_press(joypad, BTN_SELECT);
                else joypad_release(joypad, BTN_SELECT);
            }

            [NSApp sendEvent:event];         /* menus, window dragging, Cmd+Q, and so on */
        }
        [NSApp updateWindows];

        /* Release everything when the window loses focus, so no key gets stuck */
        if (![g_window isKeyWindow]) joypad->state = 0;
    }
    return !g_quit;
}