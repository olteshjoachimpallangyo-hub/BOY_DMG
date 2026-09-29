#include "joypad.h"

void joypad_press(Joypad *joypad, int button) {
    uint8_t bit = (uint8_t)(1u << button);
    if (!(joypad->state & bit)) {
        joypad->irq = true;              /* a new press can wake the game from STOP/HALT */
    }
    joypad->state |= bit;
}

void joypad_release(Joypad *joypad, int button) {
    joypad->state &= (uint8_t)~(1u << button);
}

/* Value the game sees at 0xFF00. Bits read 0 when a button in the selected group is held. */
uint8_t joypad_read(const Joypad *joypad) {
    uint8_t result = (uint8_t)(0xC0 | joypad->select | 0x0F);
    if (!(joypad->select & 0x10)) {      /* bit 4 low: direction keys */
        result &= (uint8_t)~((joypad->state >> 4) & 0x0F);
    }
    if (!(joypad->select & 0x20)) {      /* bit 5 low: A, B, Select, Start */
        result &= (uint8_t)~(joypad->state & 0x0F);
    }
    return result;
}
