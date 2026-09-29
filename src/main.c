/* BoyDMG: the macOS program. Loads a ROM, then runs the emulator in step
   with the display's refresh, so every frame is shown exactly once. */
#define _POSIX_C_SOURCE 200809L   /* clock_gettime and nanosleep */
#define _DARWIN_C_SOURCE          /* the same on macOS */

#include "bus.h"
#include "cpu.h"
#include "frontend.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#define FRAME_NS      16742706.0         /* one Game Boy frame (59.73 per second) in nanoseconds */
#define AUTOSAVE_NS   5000000000ULL      /* write save files every 5 seconds */
#define WINDOW_SCALE  3
#define MAX_CATCH_UP  3                  /* never run more than this many frames at once */

static uint64_t now_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000ULL + (uint64_t)t.tv_nsec;
}

static void sleep_until(uint64_t target) {
    uint64_t now = now_ns();
    if (target <= now) return;
    uint64_t wait = target - now;
    struct timespec ts = { (time_t)(wait / 1000000000ULL), (long)(wait % 1000000000ULL) };
    nanosleep(&ts, NULL);
}

static Bus bus;                          /* static: it's large, and this zero-fills it */
static CPU cpu;

/* Runs the Game Boy until the screen finishes a frame. The cycle limit keeps
   frames coming even while a game has the screen switched off. */
static void run_frame(void) {
    long cycles = 0;
    bus.ppu.frame_ready = false;
    while (!bus.ppu.frame_ready && cycles < CYCLES_PER_FRAME) {
        int c = cpu_step(&cpu);
        bus_tick(&bus, c);
        cycles += c;
    }
}

/* How many Game Boy frames to run for this pass of the loop */
static int frames_due(double *accumulated_ns, uint64_t *timer_deadline) {
    int refreshes = frontend_wait_for_refresh();

    if (refreshes < 0) {
        /* No display sync available: fall back to a timer */
        *timer_deadline += (uint64_t)FRAME_NS;
        if (*timer_deadline < now_ns()) *timer_deadline = now_ns();
        sleep_until(*timer_deadline);
        return 1;
    }
    if (refreshes == 0) refreshes = 1;   /* timed out, e.g. the display is asleep */
    if (refreshes > MAX_CATCH_UP) refreshes = MAX_CATCH_UP;

    double hz = frontend_refresh_rate();
    if (hz > 58.5 && hz < 61.5) {
        /* A 60 Hz screen: exactly one Game Boy frame per refresh. That's 0.45%
           faster than real hardware, which can't be noticed, and it's perfectly smooth. */
        return refreshes;
    }

    /* Other refresh rates (120 Hz, 144 Hz, ...): count refresh time and run a
       Game Boy frame each time a full frame's worth has passed. */
    double refresh_ns = (hz > 1.0) ? 1e9 / hz : FRAME_NS;
    *accumulated_ns += refreshes * refresh_ns;
    int frames = 0;
    while (*accumulated_ns >= FRAME_NS && frames < MAX_CATCH_UP) {
        *accumulated_ns -= FRAME_NS;
        frames++;
    }
    if (*accumulated_ns > FRAME_NS * MAX_CATCH_UP) *accumulated_ns = 0;   /* don't build up a backlog */
    return frames;
}

int main(int argc, char **argv) {
    char picked[1024];
    const char *rom_path = (argc >= 2) ? argv[1] : NULL;

    /* No ROM on the command line: show a file picker */
    if (!rom_path) {
        if (!frontend_pick_rom(picked, sizeof(picked))) return 0;
        rom_path = picked;
    }

    if (!cart_load(&bus.cart, rom_path)) return 1;
    bus_init(&bus);
    cpu_init(&cpu, &bus);

    const char *name = bus.cart.title[0] ? bus.cart.title : "Game Boy";
    char title[96];
    snprintf(title, sizeof(title), "BoyDMG - %s", name);
    printf("BoyDMG is running %s\n", rom_path);
    frontend_init(WINDOW_SCALE, title);

    double accumulated_ns = 0;
    uint64_t timer_deadline = now_ns();
    uint64_t last_save = now_ns();
    uint64_t fps_start = now_ns();
    int fps_frames = 0;

    for (;;) {
        int frames = frames_due(&accumulated_ns, &timer_deadline);   /* waits for the screen's next refresh */

        /* Read the keyboard only now, right before the game runs, so a key
           press is seen in the very next frame instead of one frame later */
        if (!frontend_process_events(&bus.joypad)) break;

        for (int i = 0; i < frames; i++) run_frame();
        if (frames > 0) frontend_update(bus.ppu.framebuffer);
        fps_frames += frames;

        uint64_t now = now_ns();
        if (now - fps_start >= 1000000000ULL) {
            /* Show the speed in the title bar: a steady ~60 means smooth, full speed */
            double fps = fps_frames * 1e9 / (double)(now - fps_start);
            snprintf(title, sizeof(title), "BoyDMG - %s  |  %.0f fps", name, fps);
            frontend_set_title(title);
            fps_start = now;
            fps_frames = 0;
        }
        if (now - last_save >= AUTOSAVE_NS) {
            cart_save(&bus.cart);        /* only writes if the game changed its save data */
            last_save = now;
        }
    }

    cart_save(&bus.cart);
    cart_free(&bus.cart);
    return 0;
}