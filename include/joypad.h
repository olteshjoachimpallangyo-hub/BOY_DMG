#ifndef JOYPAD_H
#define JOYPAD_H

#include "gb.h"

typedef struct {
    uint8_t state;             /* BTN_* bits, 1 = held down */
    uint8_t select;            /* bits 4-5 written by the game to 0xFF00 */
    bool    irq;               /* a button was just pressed */
} Joypad;

void    joypad_press(Joypad *joypad, int button);
void    joypad_release(Joypad *joypad, int button);
uint8_t joypad_read(const Joypad *joypad);

#endif
