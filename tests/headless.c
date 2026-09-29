/* Runs a ROM with no window. Used by `make test` to check the emulator
   against the test ROMs, which print "Passed" or "Failed" over the serial port.

   Usage: boy_headless <rom> [--frames N] [--screenshot out.ppm] [--quiet]
                              [--press FRAME BUTTON]   (repeatable; held for 6 frames)
                              [--hash]   prints a fingerprint of the final screen
   BUTTON is one of: a b select start right left up down */

#include "bus.h"
#include "cpu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_PRESSES 64
static struct { long frame; int button; } presses[MAX_PRESSES];
static int press_count = 0;

static int button_from_name(const char *name) {
    static const char *names[8] = {"a", "b", "select", "start", "right", "left", "up", "down"};
    for (int i = 0; i < 8; i++) if (!strcmp(name, names[i])) return i;
    return -1;
}

static char serial_text[8192];
static size_t serial_len = 0;
static bool quiet = false;

static void serial_out(uint8_t byte) {
    if (serial_len < sizeof(serial_text) - 1) {
        serial_text[serial_len++] = (char)byte;
        serial_text[serial_len] = '\0';
    }
    if (!quiet) {
        putchar(byte);
        fflush(stdout);
    }
}

/* Saves the screen as a PPM image (a simple format most image tools open) */
static bool write_ppm(const char *path, const uint32_t *fb) {
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    fprintf(f, "P6\n%d %d\n255\n", SCREEN_WIDTH, SCREEN_HEIGHT);
    for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) {
        uint32_t px = fb[i];                               /* stored as 0xAABBGGRR */
        uint8_t rgb[3] = { (uint8_t)(px & 0xFF), (uint8_t)((px >> 8) & 0xFF), (uint8_t)((px >> 16) & 0xFF) };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    return true;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <rom> [--frames N] [--screenshot out.ppm] [--quiet]\n", argv[0]);
        return 2;
    }
    const char *rom = argv[1];
    long frames = 3600;                  /* one minute of emulated time */
    const char *shot = NULL;
    bool print_hash = false;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = strtol(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--screenshot") && i + 1 < argc) shot = argv[++i];
        else if (!strcmp(argv[i], "--quiet")) quiet = true;
        else if (!strcmp(argv[i], "--hash")) print_hash = true;
        else if (!strcmp(argv[i], "--press") && i + 2 < argc && press_count < MAX_PRESSES) {
            presses[press_count].frame = strtol(argv[++i], NULL, 10);
            presses[press_count].button = button_from_name(argv[++i]);
            if (presses[press_count].button < 0) { fprintf(stderr, "Unknown button: %s\n", argv[i]); return 2; }
            press_count++;
        }
    }

    static Bus bus;                      /* static: it's large, and this zero-fills it */
    if (!cart_load(&bus.cart, rom)) return 2;
    bus_init(&bus);
    bus.serial_out = serial_out;

    CPU cpu;
    cpu_init(&cpu, &bus);

    int result = 3;                      /* 3 = no verdict (ran out of time) */
    for (long frame = 0; frame < frames; frame++) {
        for (int i = 0; i < press_count; i++) {
            if (frame == presses[i].frame)     joypad_press(&bus.joypad, presses[i].button);
            if (frame == presses[i].frame + 6) joypad_release(&bus.joypad, presses[i].button);
        }

        long cycles = 0;
        bus.ppu.frame_ready = false;
        while (!bus.ppu.frame_ready && cycles < CYCLES_PER_FRAME) {
            int c = cpu_step(&cpu);
            bus_tick(&bus, c);
            cycles += c;
        }
        if (strstr(serial_text, "Passed")) { result = 0; break; }
        if (strstr(serial_text, "Failed")) { result = 1; break; }
    }

    if (shot && !write_ppm(shot, bus.ppu.framebuffer)) {
        fprintf(stderr, "Could not write %s\n", shot);
    }
    if (!quiet) putchar('\n');
    if (print_hash) {
        /* Fingerprint of the screen. Each pixel is reduced to its shade rank
           (0 = lightest), so changing the palette in ppu.c doesn't change it. */
        uint32_t colors[4];
        int n_colors = 0;
        const uint32_t *fb = bus.ppu.framebuffer;
        for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT && n_colors < 4; i++) {
            bool seen = false;
            for (int k = 0; k < n_colors; k++) if (colors[k] == fb[i]) seen = true;
            if (!seen) colors[n_colors++] = fb[i];
        }
        /* sort lightest first (sum of red, green, blue) */
        for (int a = 0; a < n_colors; a++)
            for (int b = a + 1; b < n_colors; b++) {
                int la = (int)(colors[a] & 0xFF) + (int)((colors[a] >> 8) & 0xFF) + (int)((colors[a] >> 16) & 0xFF);
                int lb = (int)(colors[b] & 0xFF) + (int)((colors[b] >> 8) & 0xFF) + (int)((colors[b] >> 16) & 0xFF);
                if (lb > la) { uint32_t t = colors[a]; colors[a] = colors[b]; colors[b] = t; }
            }
        uint32_t h = 2166136261u;        /* FNV-1a */
        for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) {
            uint32_t rank = 0;
            for (int k = 0; k < n_colors; k++) if (colors[k] == fb[i]) rank = (uint32_t)k;
            h = (h ^ rank) * 16777619u;
        }
        printf("%08x\n", h);
    }
    cart_free(&bus.cart);
    return result;
}
