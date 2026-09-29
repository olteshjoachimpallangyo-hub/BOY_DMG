#ifndef CART_H
#define CART_H

#include "gb.h"

typedef enum { MBC_NONE, MBC_1, MBC_2, MBC_3, MBC_5 } MbcType;

typedef struct {
    uint8_t *rom;
    size_t   rom_size;
    uint16_t rom_banks;        /* number of 16 KB banks */

    uint8_t *ram;
    size_t   ram_size;
    bool     ram_dirty;        /* true when unsaved changes exist */

    MbcType  mbc;
    bool     has_battery;
    bool     ram_enabled;

    /* Bank registers (their meaning depends on the MBC) */
    uint16_t rom_bank;         /* MBC2, MBC3, MBC5 */
    uint8_t  ram_bank;         /* MBC3, MBC5 */
    uint8_t  mbc1_bank1;       /* MBC1: 5-bit ROM bank */
    uint8_t  mbc1_bank2;       /* MBC1: 2-bit upper ROM / RAM bank */
    uint8_t  mbc1_mode;        /* MBC1: banking mode */
    uint8_t  rtc_select;       /* MBC3: 0 = RAM, 0x08-0x0C = clock register */
    uint8_t  rtc_regs[5];      /* MBC3 clock: present but not ticking */

    char title[17];
    char save_path[1024];
} Cart;

/* Loads a ROM file. Returns false and prints a reason on failure. */
bool    cart_load(Cart *cart, const char *path);
void    cart_free(Cart *cart);
bool    cart_save(Cart *cart);   /* writes battery RAM to the .sav file */

uint8_t cart_read(Cart *cart, uint16_t addr);
void    cart_write(Cart *cart, uint16_t addr, uint8_t value);

#endif
