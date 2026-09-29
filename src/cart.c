#include "cart.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------- Loading ---------------- */

static size_t ram_size_from_header(uint8_t code) {
    switch (code) {
        case 0x01: return 2 * 1024;     /* unofficial, but some homebrew uses it */
        case 0x02: return 8 * 1024;
        case 0x03: return 32 * 1024;
        case 0x04: return 128 * 1024;
        case 0x05: return 64 * 1024;
        default:   return 0;
    }
}

/* Reads the cartridge type byte (0x147) and fills in the chip and battery flags. */
static bool parse_cart_type(Cart *cart, uint8_t type) {
    switch (type) {
        case 0x00:                       cart->mbc = MBC_NONE; break;
        case 0x08:                       cart->mbc = MBC_NONE; break;
        case 0x09:                       cart->mbc = MBC_NONE; cart->has_battery = true; break;
        case 0x01: case 0x02:            cart->mbc = MBC_1; break;
        case 0x03:                       cart->mbc = MBC_1; cart->has_battery = true; break;
        case 0x05:                       cart->mbc = MBC_2; break;
        case 0x06:                       cart->mbc = MBC_2; cart->has_battery = true; break;
        case 0x11: case 0x12:            cart->mbc = MBC_3; break;
        case 0x0F: case 0x10: case 0x13: cart->mbc = MBC_3; cart->has_battery = true; break;
        case 0x19: case 0x1A: case 0x1C: case 0x1D:
                                         cart->mbc = MBC_5; break;
        case 0x1B: case 0x1E:            cart->mbc = MBC_5; cart->has_battery = true; break;
        default:
            fprintf(stderr, "This cartridge type (0x%02X) isn't supported yet.\n", type);
            return false;
    }
    return true;
}

/* "roms/game.gb" becomes "roms/game.sav" */
static void make_save_path(Cart *cart, const char *rom_path) {
    snprintf(cart->save_path, sizeof(cart->save_path), "%s", rom_path);
    char *dot = strrchr(cart->save_path, '.');
    char *slash = strrchr(cart->save_path, '/');
    if (dot && (!slash || dot > slash)) {
        *dot = '\0';
    }
    strncat(cart->save_path, ".sav", sizeof(cart->save_path) - strlen(cart->save_path) - 1);
}

static void load_save_file(Cart *cart) {
    FILE *f = fopen(cart->save_path, "rb");
    if (!f) return;                      /* no save yet: that's fine */
    size_t got = fread(cart->ram, 1, cart->ram_size, f);
    fclose(f);
    printf("Loaded save file: %s (%zu bytes)\n", cart->save_path, got);
}

bool cart_load(Cart *cart, const char *path) {
    memset(cart, 0, sizeof(*cart));

    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Could not open ROM file: %s\n", path);
        return false;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size < 0x150 || size > 8 * 1024 * 1024) {
        fprintf(stderr, "%s doesn't look like a Game Boy ROM (size %ld bytes).\n", path, size);
        fclose(f);
        return false;
    }

    /* Always allocate at least 32 KB so small or odd files can't be read past the end */
    size_t alloc = (size_t)size < 0x8000 ? 0x8000 : (size_t)size;
    cart->rom = calloc(1, alloc);
    if (!cart->rom || fread(cart->rom, 1, (size_t)size, f) != (size_t)size) {
        fprintf(stderr, "Could not read ROM file: %s\n", path);
        fclose(f);
        free(cart->rom);
        cart->rom = NULL;
        return false;
    }
    fclose(f);

    cart->rom_size = alloc;
    cart->rom_banks = (uint16_t)(alloc / 0x4000);

    memcpy(cart->title, &cart->rom[0x134], 16);
    cart->title[16] = '\0';
    for (int i = 0; i < 16; i++) {
        if ((unsigned char)cart->title[i] < 0x20 || (unsigned char)cart->title[i] > 0x7E) {
            cart->title[i] = '\0';       /* stop at the first non-text byte */
            break;
        }
    }

    if (cart->rom[0x143] == 0xC0) {
        fprintf(stderr, "Note: this game is marked Game Boy Color only, so it may not run here.\n");
    }

    if (!parse_cart_type(cart, cart->rom[0x147])) {
        cart_free(cart);
        return false;
    }

    cart->ram_size = (cart->mbc == MBC_2) ? 512 : ram_size_from_header(cart->rom[0x149]);
    if (cart->ram_size > 0) {
        cart->ram = calloc(1, cart->ram_size);
        if (!cart->ram) {
            cart_free(cart);
            return false;
        }
    }

    cart->rom_bank = 1;
    cart->mbc1_bank1 = 1;

    make_save_path(cart, path);
    if (cart->has_battery && cart->ram) {
        load_save_file(cart);
    }
    return true;
}

void cart_free(Cart *cart) {
    free(cart->rom);
    free(cart->ram);
    cart->rom = NULL;
    cart->ram = NULL;
}

bool cart_save(Cart *cart) {
    if (!cart->has_battery || !cart->ram || !cart->ram_dirty) return true;
    FILE *f = fopen(cart->save_path, "wb");
    if (!f) {
        fprintf(stderr, "Could not write save file: %s\n", cart->save_path);
        return false;
    }
    bool ok = fwrite(cart->ram, 1, cart->ram_size, f) == cart->ram_size;
    fclose(f);
    if (ok) cart->ram_dirty = false;
    return ok;
}

/* ---------------- Reading ---------------- */

static uint8_t rom_byte(Cart *cart, uint32_t bank, uint16_t offset) {
    bank %= cart->rom_banks;             /* games may ask for banks past the end */
    return cart->rom[bank * 0x4000 + offset];
}

static size_t ram_offset(Cart *cart, uint32_t bank, uint16_t addr) {
    return ((size_t)bank * 0x2000 + (addr - 0xA000)) % cart->ram_size;
}

uint8_t cart_read(Cart *cart, uint16_t addr) {
    /* ROM bank 0 area (0x0000-0x3FFF) */
    if (addr < 0x4000) {
        if (cart->mbc == MBC_1 && cart->mbc1_mode == 1) {
            return rom_byte(cart, (uint32_t)cart->mbc1_bank2 << 5, addr);
        }
        return cart->rom[addr];
    }

    /* Switchable ROM bank (0x4000-0x7FFF) */
    if (addr < 0x8000) {
        uint16_t offset = addr - 0x4000;
        switch (cart->mbc) {
            case MBC_NONE: return rom_byte(cart, 1, offset);
            case MBC_1:    return rom_byte(cart, ((uint32_t)cart->mbc1_bank2 << 5) | cart->mbc1_bank1, offset);
            default:       return rom_byte(cart, cart->rom_bank, offset);
        }
    }

    /* Cartridge RAM (0xA000-0xBFFF) */
    if (addr >= 0xA000 && addr < 0xC000) {
        if (cart->mbc == MBC_3 && cart->ram_enabled && cart->rtc_select >= 0x08) {
            return cart->rtc_regs[cart->rtc_select - 0x08];
        }
        if (!cart->ram || (cart->mbc != MBC_NONE && !cart->ram_enabled)) return 0xFF;

        switch (cart->mbc) {
            case MBC_1: {
                uint32_t bank = (cart->mbc1_mode == 1) ? cart->mbc1_bank2 : 0;
                return cart->ram[ram_offset(cart, bank, addr)];
            }
            case MBC_2:
                /* 512 half-bytes, repeated across the whole area */
                return (uint8_t)(0xF0 | (cart->ram[(addr - 0xA000) & 0x1FF] & 0x0F));
            case MBC_3:
            case MBC_5:
                return cart->ram[ram_offset(cart, cart->ram_bank, addr)];
            default:
                return cart->ram[ram_offset(cart, 0, addr)];
        }
    }
    return 0xFF;
}

/* ---------------- Writing (bank switching and cartridge RAM) ---------------- */

static void write_ram(Cart *cart, uint16_t addr, uint8_t value) {
    if (cart->mbc == MBC_3 && cart->ram_enabled && cart->rtc_select >= 0x08) {
        cart->rtc_regs[cart->rtc_select - 0x08] = value;
        return;
    }
    if (!cart->ram || (cart->mbc != MBC_NONE && !cart->ram_enabled)) return;

    size_t index;
    switch (cart->mbc) {
        case MBC_1: index = ram_offset(cart, (cart->mbc1_mode == 1) ? cart->mbc1_bank2 : 0, addr); break;
        case MBC_2: index = (addr - 0xA000) & 0x1FF; value &= 0x0F; break;
        case MBC_3:
        case MBC_5: index = ram_offset(cart, cart->ram_bank, addr); break;
        default:    index = ram_offset(cart, 0, addr); break;
    }
    if (cart->ram[index] != value) {
        cart->ram[index] = value;
        cart->ram_dirty = true;
    }
}

void cart_write(Cart *cart, uint16_t addr, uint8_t value) {
    if (addr >= 0xA000 && addr < 0xC000) {
        write_ram(cart, addr, value);
        return;
    }
    if (addr >= 0x8000) return;

    switch (cart->mbc) {
        case MBC_NONE:
            break;                       /* plain ROM: writes do nothing */

        case MBC_1:
            if (addr < 0x2000) {
                cart->ram_enabled = (value & 0x0F) == 0x0A;
            } else if (addr < 0x4000) {
                cart->mbc1_bank1 = value & 0x1F;
                if (cart->mbc1_bank1 == 0) cart->mbc1_bank1 = 1;
            } else if (addr < 0x6000) {
                cart->mbc1_bank2 = value & 0x03;
            } else {
                cart->mbc1_mode = value & 0x01;
            }
            break;

        case MBC_2:
            if (addr < 0x4000) {
                if (addr & 0x0100) {     /* address bit 8 picks ROM bank vs RAM enable */
                    cart->rom_bank = value & 0x0F;
                    if (cart->rom_bank == 0) cart->rom_bank = 1;
                } else {
                    cart->ram_enabled = (value & 0x0F) == 0x0A;
                }
            }
            break;

        case MBC_3:
            if (addr < 0x2000) {
                cart->ram_enabled = (value & 0x0F) == 0x0A;
            } else if (addr < 0x4000) {
                cart->rom_bank = value & 0x7F;
                if (cart->rom_bank == 0) cart->rom_bank = 1;
            } else if (addr < 0x6000) {
                if (value <= 0x07) {
                    cart->ram_bank = value;
                    cart->rtc_select = 0;
                } else if (value >= 0x08 && value <= 0x0C) {
                    cart->rtc_select = value;
                }
            }
            /* 0x6000-0x7FFF latches the clock; the clock doesn't tick here */
            break;

        case MBC_5:
            if (addr < 0x2000) {
                cart->ram_enabled = (value & 0x0F) == 0x0A;
            } else if (addr < 0x3000) {
                cart->rom_bank = (uint16_t)((cart->rom_bank & 0x100) | value);   /* bank 0 is allowed on MBC5 */
            } else if (addr < 0x4000) {
                cart->rom_bank = (uint16_t)((cart->rom_bank & 0xFF) | ((value & 0x01) << 8));
            } else if (addr < 0x6000) {
                cart->ram_bank = value & 0x0F;
            }
            break;
    }
}
