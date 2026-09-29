#ifndef GB_H
#define GB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SCREEN_WIDTH      160
#define SCREEN_HEIGHT     144
#define CYCLES_PER_FRAME  70224      /* clock cycles (T-cycles) per 59.73 Hz frame */

/* Interrupt bits, used in IE (0xFFFF) and IF (0xFF0F) */
#define INT_VBLANK  (1 << 0)
#define INT_LCDSTAT (1 << 1)
#define INT_TIMER   (1 << 2)
#define INT_SERIAL  (1 << 3)
#define INT_JOYPAD  (1 << 4)

/* Joypad button bits (1 = pressed) */
#define BTN_A      0
#define BTN_B      1
#define BTN_SELECT 2
#define BTN_START  3
#define BTN_RIGHT  4
#define BTN_LEFT   5
#define BTN_UP     6
#define BTN_DOWN   7

typedef struct Bus Bus;

#endif
