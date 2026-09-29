#ifndef BUS_H
#define BUS_H

#include "gb.h"
#include "cart.h"
#include "joypad.h"
#include "ppu.h"

struct Bus {
    Cart   cart;
    PPU    ppu;
    Joypad joypad;

    uint8_t wram[0x2000];
    uint8_t hram[0x7F];
    uint8_t io[0x80];          /* raw storage for registers without special handling (sound) */

    uint8_t ie;                /* 0xFFFF */
    uint8_t if_flags;          /* 0xFF0F */

    /* Timer: DIV is the top 8 bits of a 16-bit counter */
    uint16_t sysclk;
    uint8_t  tima, tma, tac;

    /* Serial port: test ROMs print their results through it */
    uint8_t sb, sc;
    void  (*serial_out)(uint8_t byte);
};

void    bus_init(Bus *bus);
uint8_t bus_read(Bus *bus, uint16_t addr);
void    bus_write(Bus *bus, uint16_t addr, uint8_t value);
void    bus_tick(Bus *bus, int cycles);    /* advances timer, PPU, and joypad */
void    bus_request_interrupt(Bus *bus, uint8_t bit);

#endif
