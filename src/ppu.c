#include "ppu.h"
#include "bus.h"

#include <string.h>

/* Pixels are stored so the macOS window can use them directly: bytes are
   red, green, blue, alpha in memory, which on a little-endian Mac is the
   32-bit value 0xAABBGGRR. */
#define RGB(r, g, b) (0xFF000000u | ((uint32_t)(b) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(r))

/* The four shades, lightest to darkest. For the classic green screen, use:
   RGB(0x9B, 0xBC, 0x0F), RGB(0x8B, 0xAC, 0x0F), RGB(0x30, 0x62, 0x30), RGB(0x0F, 0x38, 0x0F) */
static const uint32_t shades[4] = {
    RGB(0xFF, 0xFF, 0xFF), RGB(0xAA, 0xAA, 0xAA), RGB(0x55, 0x55, 0x55), RGB(0x00, 0x00, 0x00),
};

/* LCDC bits */
#define LCDC_BG_ON        0x01
#define LCDC_OBJ_ON       0x02
#define LCDC_OBJ_TALL     0x04
#define LCDC_BG_MAP       0x08
#define LCDC_TILE_DATA    0x10
#define LCDC_WIN_ON       0x20
#define LCDC_WIN_MAP      0x40
#define LCDC_LCD_ON       0x80

/* Line timing in clock cycles */
#define DOTS_OAM     80
#define DOTS_DRAW    172
#define DOTS_LINE    456

void ppu_init(PPU *ppu) {
    memset(ppu, 0, sizeof(*ppu));
    /* State the boot ROM leaves: screen on, starting a fresh frame */
    ppu->lcdc = 0x91;
    ppu->bgp = 0xFC;
    ppu->obp0 = 0xFF;
    ppu->obp1 = 0xFF;
    ppu->mode = 2;
    ppu->stat = 0x02;
    for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) ppu->framebuffer[i] = shades[0];
}

/* ---------------- Interrupt helpers ---------------- */

/* The STAT interrupt fires when any enabled condition becomes true.
   All conditions share one line, so it only fires on a rising edge. */
static void update_stat_irq(PPU *p, Bus *bus) {
    bool lyc_match = p->ly == p->lyc;
    p->stat = (uint8_t)((p->stat & ~0x07) | (lyc_match ? 0x04 : 0) | (p->mode & 0x03));

    bool line = (lyc_match && (p->stat & 0x40)) ||
                (p->mode == 0 && (p->stat & 0x08)) ||
                (p->mode == 1 && (p->stat & 0x10)) ||
                (p->mode == 2 && (p->stat & 0x20));
    if (line && !p->stat_irq_line) {
        bus_request_interrupt(bus, INT_LCDSTAT);
    }
    p->stat_irq_line = line;
}

static void set_mode(PPU *p, Bus *bus, uint8_t mode) {
    p->mode = mode;
    update_stat_irq(p, bus);
}

/* ---------------- Drawing one line ---------------- */

static uint8_t tile_pixel(const PPU *p, uint8_t tile, int row, int col, bool object) {
    uint16_t addr;
    if (object || (p->lcdc & LCDC_TILE_DATA)) {
        addr = (uint16_t)(tile * 16);                        /* 0x8000 area, tiles 0-255 */
    } else {
        addr = (uint16_t)(0x1000 + (int8_t)tile * 16);       /* 0x9000 area, tiles -128..127 */
    }
    addr = (uint16_t)(addr + row * 2);
    uint8_t lo = p->vram[addr];
    uint8_t hi = p->vram[addr + 1];
    int bit = 7 - col;                                       /* bit 7 is the leftmost pixel */
    return (uint8_t)((((hi >> bit) & 1) << 1) | ((lo >> bit) & 1));
}

static void render_scanline(PPU *p) {
    uint32_t *line = &p->framebuffer[p->ly * SCREEN_WIDTH];
    uint8_t bg_index[SCREEN_WIDTH];     /* color number before the palette, for sprite priority */

    /* On the original Game Boy, turning the background off blanks both it and the window */
    if (!(p->lcdc & LCDC_BG_ON)) {
        memset(bg_index, 0, sizeof(bg_index));
        for (int x = 0; x < SCREEN_WIDTH; x++) line[x] = shades[0];
    } else {
        /* Background */
        uint16_t map = (p->lcdc & LCDC_BG_MAP) ? 0x1C00 : 0x1800;
        uint8_t y = (uint8_t)(p->ly + p->scy);
        for (int x = 0; x < SCREEN_WIDTH; x++) {
            uint8_t sx = (uint8_t)(x + p->scx);
            uint8_t tile = p->vram[map + (y / 8) * 32 + sx / 8];
            uint8_t color = tile_pixel(p, tile, y % 8, sx % 8, false);
            bg_index[x] = color;
            line[x] = shades[(p->bgp >> (color * 2)) & 0x03];
        }

        /* Window: drawn over the background from (WX - 7, WY) */
        int wx = p->wx - 7;
        if ((p->lcdc & LCDC_WIN_ON) && p->ly >= p->wy && p->wx <= 166) {
            uint16_t wmap = (p->lcdc & LCDC_WIN_MAP) ? 0x1C00 : 0x1800;
            uint8_t wy = p->window_line;
            bool drew = false;
            for (int x = (wx < 0 ? 0 : wx); x < SCREEN_WIDTH; x++) {
                int col = x - wx;
                uint8_t tile = p->vram[wmap + (wy / 8) * 32 + col / 8];
                uint8_t color = tile_pixel(p, tile, wy % 8, col % 8, false);
                bg_index[x] = color;
                line[x] = shades[(p->bgp >> (color * 2)) & 0x03];
                drew = true;
            }
            if (drew) p->window_line++;
        }
    }

    /* Sprites */
    if (!(p->lcdc & LCDC_OBJ_ON)) return;

    int height = (p->lcdc & LCDC_OBJ_TALL) ? 16 : 8;
    int found[10];
    int count = 0;

    /* The hardware picks the first 10 sprites on this line, in OAM order */
    for (int i = 0; i < 40 && count < 10; i++) {
        int sy = p->oam[i * 4] - 16;
        if (p->ly >= sy && p->ly < sy + height) {
            found[count++] = i;
        }
    }

    /* Priority: lower X wins; on a tie, the earlier sprite in OAM wins.
       Insertion sort keeps OAM order for ties. */
    for (int i = 1; i < count; i++) {
        int cur = found[i];
        int j = i - 1;
        while (j >= 0 && p->oam[found[j] * 4 + 1] > p->oam[cur * 4 + 1]) {
            found[j + 1] = found[j];
            j--;
        }
        found[j + 1] = cur;
    }

    bool taken[SCREEN_WIDTH] = {false};  /* pixel already claimed by a higher-priority sprite */
    for (int n = 0; n < count; n++) {
        const uint8_t *s = &p->oam[found[n] * 4];
        int sy = s[0] - 16;
        int sx = s[1] - 8;
        uint8_t tile = s[2];
        uint8_t flags = s[3];

        int row = p->ly - sy;
        if (flags & 0x40) row = height - 1 - row;             /* vertical flip */
        if (height == 16) {
            tile &= 0xFE;                                     /* tall sprites use an even/odd tile pair */
            if (row >= 8) { tile |= 0x01; row -= 8; }
        }
        uint8_t palette = (flags & 0x10) ? p->obp1 : p->obp0;

        for (int px = 0; px < 8; px++) {
            int x = sx + px;
            if (x < 0 || x >= SCREEN_WIDTH || taken[x]) continue;
            int col = (flags & 0x20) ? 7 - px : px;           /* horizontal flip */
            uint8_t color = tile_pixel(p, tile, row, col, true);
            if (color == 0) continue;                         /* color 0 is see-through */
            taken[x] = true;
            if ((flags & 0x80) && bg_index[x] != 0) continue; /* behind background colors 1-3 */
            line[x] = shades[(palette >> (color * 2)) & 0x03];
        }
    }
}

/* ---------------- Timing ---------------- */

static void next_line(PPU *p, Bus *bus) {
    p->ly++;
    if (p->ly == SCREEN_HEIGHT) {
        p->mode = 1;
        p->frame_ready = true;
        bus_request_interrupt(bus, INT_VBLANK);
    } else if (p->ly > 153) {
        p->ly = 0;
        p->window_line = 0;
        p->mode = 2;
    } else if (p->ly < SCREEN_HEIGHT) {
        p->mode = 2;
    }
    update_stat_irq(p, bus);
}

void ppu_step(PPU *p, Bus *bus, int cycles) {
    if (!(p->lcdc & LCDC_LCD_ON)) return;

    p->dots += cycles;
    for (;;) {
        if (p->mode == 2 && p->dots >= DOTS_OAM) {
            set_mode(p, bus, 3);
        } else if (p->mode == 3 && p->dots >= DOTS_OAM + DOTS_DRAW) {
            render_scanline(p);
            set_mode(p, bus, 0);
        } else if ((p->mode == 0 || p->mode == 1) && p->dots >= DOTS_LINE) {
            p->dots -= DOTS_LINE;
            next_line(p, bus);
        } else {
            break;
        }
    }
}

/* ---------------- Register writes that need extra work ---------------- */

void ppu_write_lcdc(PPU *p, Bus *bus, uint8_t value) {
    bool was_on = p->lcdc & LCDC_LCD_ON;
    bool now_on = value & LCDC_LCD_ON;
    p->lcdc = value;

    if (was_on && !now_on) {
        /* Screen off: LY resets and the display goes blank */
        p->ly = 0;
        p->dots = 0;
        p->mode = 0;
        p->window_line = 0;
        p->stat &= (uint8_t)~0x03;
        for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) p->framebuffer[i] = shades[0];
        p->frame_ready = true;
    } else if (!was_on && now_on) {
        /* Screen on: start a new frame from line 0 */
        p->ly = 0;
        p->dots = 0;
        p->window_line = 0;
        set_mode(p, bus, 2);
    }
}

void ppu_write_stat(PPU *p, Bus *bus, uint8_t value) {
    p->stat = (uint8_t)((p->stat & 0x07) | (value & 0x78));  /* only bits 3-6 can be written */
    if (p->lcdc & LCDC_LCD_ON) update_stat_irq(p, bus);
}

void ppu_write_lyc(PPU *p, Bus *bus, uint8_t value) {
    p->lyc = value;
    if (p->lcdc & LCDC_LCD_ON) update_stat_irq(p, bus);
}
