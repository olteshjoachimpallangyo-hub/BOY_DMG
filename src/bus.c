#include "bus.h"

#include <string.h>

/* Bits of the internal counter that drive TIMA, for TAC settings 0-3.
   TIMA steps when the chosen bit falls from 1 to 0, which gives the
   real periods of 1024, 16, 64 and 256 clock cycles. */
static const uint16_t timer_bit[4] = {1u << 9, 1u << 3, 1u << 5, 1u << 7};

/* Bits that always read as 1 for the sound registers 0xFF10-0xFF3F */
static const uint8_t sound_read_mask[0x30] = {
    0x80, 0x3F, 0x00, 0xFF, 0xBF, 0xFF, 0x3F, 0x00, 0xFF, 0xBF, 0x7F, 0xFF, 0x9F, 0xFF, 0xBF, 0xFF,
    0xFF, 0x00, 0x00, 0xBF, 0x00, 0x00, 0x70, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

void bus_init(Bus *bus) {
    Cart cart = bus->cart;               /* keep a cartridge that was loaded before init */
    memset(bus, 0, sizeof(*bus));
    bus->cart = cart;
    ppu_init(&bus->ppu);

    /* Values the real boot ROM leaves behind, since we start straight at the game */
    bus->sysclk = 0xABCC;
    bus->tac = 0xF8;
    bus->if_flags = 0xE1;
    bus->sc = 0x7E;
    bus->joypad.select = 0x30;

    static const uint8_t sound_init[0x17] = {
        0x80, 0xBF, 0xF3, 0xFF, 0xBF, 0xFF, 0x3F, 0x00, 0xFF, 0xBF, 0x7F, 0xFF,
        0x9F, 0xFF, 0xBF, 0xFF, 0xFF, 0x00, 0x00, 0xBF, 0x77, 0xF3, 0xF1,
    };
    memcpy(&bus->io[0x10], sound_init, sizeof(sound_init));
}

void bus_request_interrupt(Bus *bus, uint8_t bit) {
    bus->if_flags |= bit;
}

/* ---------------- Timer ---------------- */

static void timer_increment(Bus *bus) {
    if (bus->tima == 0xFF) {
        bus->tima = bus->tma;
        bus_request_interrupt(bus, INT_TIMER);
    } else {
        bus->tima++;
    }
}

static bool timer_input(const Bus *bus) {
    return (bus->tac & 0x04) && (bus->sysclk & timer_bit[bus->tac & 0x03]);
}

static void timer_tick(Bus *bus, int cycles) {
    for (int i = 0; i < cycles; i += 4) {       /* one step per machine cycle */
        bool before = timer_input(bus);
        bus->sysclk += 4;
        if (before && !timer_input(bus)) {
            timer_increment(bus);
        }
    }
}

void bus_tick(Bus *bus, int cycles) {
    timer_tick(bus, cycles);
    ppu_step(&bus->ppu, bus, cycles);
    if (bus->joypad.irq) {
        bus->joypad.irq = false;
        bus_request_interrupt(bus, INT_JOYPAD);
    }
}

/* ---------------- I/O registers (0xFF00-0xFF7F) ---------------- */

static uint8_t io_read(Bus *bus, uint16_t addr) {
    PPU *p = &bus->ppu;
    switch (addr) {
        case 0xFF00: return joypad_read(&bus->joypad);
        case 0xFF01: return bus->sb;
        case 0xFF02: return (uint8_t)(bus->sc | 0x7E);
        case 0xFF04: return (uint8_t)(bus->sysclk >> 8);
        case 0xFF05: return bus->tima;
        case 0xFF06: return bus->tma;
        case 0xFF07: return (uint8_t)(bus->tac | 0xF8);
        case 0xFF0F: return (uint8_t)(bus->if_flags | 0xE0);
        case 0xFF40: return p->lcdc;
        case 0xFF41: return (uint8_t)(p->stat | 0x80);
        case 0xFF42: return p->scy;
        case 0xFF43: return p->scx;
        case 0xFF44: return p->ly;
        case 0xFF45: return p->lyc;
        case 0xFF46: return bus->io[0x46];
        case 0xFF47: return p->bgp;
        case 0xFF48: return p->obp0;
        case 0xFF49: return p->obp1;
        case 0xFF4A: return p->wy;
        case 0xFF4B: return p->wx;
    }
    if (addr >= 0xFF10 && addr <= 0xFF3F) {
        uint8_t value = bus->io[addr - 0xFF00] | sound_read_mask[addr - 0xFF10];
        if (addr == 0xFF26) value = (uint8_t)((bus->io[0x26] & 0x80) | 0x70);   /* no channels playing */
        return value;
    }
    return 0xFF;                         /* unused, or Game Boy Color only */
}

static void io_write(Bus *bus, uint16_t addr, uint8_t value) {
    PPU *p = &bus->ppu;
    switch (addr) {
        case 0xFF00: bus->joypad.select = value & 0x30; return;
        case 0xFF01: bus->sb = value; return;
        case 0xFF02:
            bus->sc = value;
            if ((value & 0x81) == 0x81) {
                /* Transfer with internal clock: finish it at once. With no cable
                   connected, the byte received is 0xFF. */
                if (bus->serial_out) bus->serial_out(bus->sb);
                bus->sb = 0xFF;
                bus->sc &= 0x7F;
                bus_request_interrupt(bus, INT_SERIAL);
            }
            return;
        case 0xFF04: {
            /* Resetting DIV can make the timer's input fall, which counts as a step */
            bool before = timer_input(bus);
            bus->sysclk = 0;
            if (before) timer_increment(bus);
            return;
        }
        case 0xFF05: bus->tima = value; return;
        case 0xFF06: bus->tma = value; return;
        case 0xFF07: {
            bool before = timer_input(bus);
            bus->tac = value & 0x07;
            if (before && !timer_input(bus)) timer_increment(bus);
            return;
        }
        case 0xFF0F: bus->if_flags = value & 0x1F; return;
        case 0xFF40: ppu_write_lcdc(p, bus, value); return;
        case 0xFF41: ppu_write_stat(p, bus, value); return;
        case 0xFF42: p->scy = value; return;
        case 0xFF43: p->scx = value; return;
        case 0xFF44: return;             /* LY is read-only */
        case 0xFF45: ppu_write_lyc(p, bus, value); return;
        case 0xFF46: {
            /* OAM DMA: copy 160 bytes of sprite data into OAM. Done instantly here;
               games wait for it anyway, so the result is the same. */
            bus->io[0x46] = value;
            uint16_t src = (uint16_t)(value << 8);
            if (src >= 0xE000) src -= 0x2000;   /* the echo area mirrors work RAM */
            for (int i = 0; i < 0xA0; i++) {
                p->oam[i] = bus_read(bus, (uint16_t)(src + i));
            }
            return;
        }
        case 0xFF47: p->bgp = value; return;
        case 0xFF48: p->obp0 = value; return;
        case 0xFF49: p->obp1 = value; return;
        case 0xFF4A: p->wy = value; return;
        case 0xFF4B: p->wx = value; return;
    }
    if (addr >= 0xFF10 && addr <= 0xFF3F) {
        bus->io[addr - 0xFF00] = value;  /* sound isn't played, but games can read their settings back */
    }
}

/* ---------------- Memory map ---------------- */

uint8_t bus_read(Bus *bus, uint16_t addr) {
    if (addr < 0x8000)  return cart_read(&bus->cart, addr);                  /* cartridge ROM */
    if (addr < 0xA000)  return bus->ppu.vram[addr - 0x8000];                 /* video RAM */
    if (addr < 0xC000)  return cart_read(&bus->cart, addr);                  /* cartridge RAM */
    if (addr < 0xE000)  return bus->wram[addr - 0xC000];                     /* work RAM */
    if (addr < 0xFE00)  return bus->wram[addr - 0xE000];                     /* echo of work RAM */
    if (addr < 0xFEA0)  return bus->ppu.oam[addr - 0xFE00];                  /* sprite table */
    if (addr < 0xFF00)  return 0xFF;                                         /* unusable */
    if (addr < 0xFF80)  return io_read(bus, addr);                           /* I/O registers */
    if (addr < 0xFFFF)  return bus->hram[addr - 0xFF80];                     /* high RAM */
    return bus->ie;                                                          /* interrupt enable */
}

void bus_write(Bus *bus, uint16_t addr, uint8_t value) {
    if (addr < 0x8000)       cart_write(&bus->cart, addr, value);            /* bank switching */
    else if (addr < 0xA000)  bus->ppu.vram[addr - 0x8000] = value;
    else if (addr < 0xC000)  cart_write(&bus->cart, addr, value);
    else if (addr < 0xE000)  bus->wram[addr - 0xC000] = value;
    else if (addr < 0xFE00)  bus->wram[addr - 0xE000] = value;
    else if (addr < 0xFEA0)  bus->ppu.oam[addr - 0xFE00] = value;
    else if (addr < 0xFF00)  { /* unusable */ }
    else if (addr < 0xFF80)  io_write(bus, addr, value);
    else if (addr < 0xFFFF)  bus->hram[addr - 0xFF80] = value;
    else                     bus->ie = value;
}
