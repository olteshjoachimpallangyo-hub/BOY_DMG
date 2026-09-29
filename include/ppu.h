#ifndef PPU_H
#define PPU_H

#include "gb.h"

typedef struct {
    uint8_t  vram[0x2000];
    uint8_t  oam[0xA0];
    uint32_t framebuffer[SCREEN_WIDTH * SCREEN_HEIGHT];

    uint8_t lcdc, stat, scy, scx, ly, lyc, bgp, obp0, obp1, wy, wx;

    uint8_t mode;              /* 0 HBlank, 1 VBlank, 2 OAM scan, 3 drawing */
    int     dots;              /* clock cycles into the current line */
    uint8_t window_line;       /* the window keeps its own line counter */
    bool    stat_irq_line;     /* for detecting rising edges of the STAT interrupt */
    bool    frame_ready;       /* set at the start of VBlank */
} PPU;

void ppu_init(PPU *ppu);
void ppu_step(PPU *ppu, Bus *bus, int cycles);
void ppu_write_lcdc(PPU *ppu, Bus *bus, uint8_t value);
void ppu_write_stat(PPU *ppu, Bus *bus, uint8_t value);
void ppu_write_lyc(PPU *ppu, Bus *bus, uint8_t value);

#endif
