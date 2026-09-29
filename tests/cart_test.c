/* Checks cartridge bank switching and battery saves using small fake ROMs.
   Each 16 KB bank starts with its own bank number, so reading 0x4000
   shows which bank is currently selected. */

#include "cart.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, ...) do { \
    if (!(cond)) { printf("  FAIL: " __VA_ARGS__); printf("\n"); failures++; } \
} while (0)

static void make_rom(const char *path, uint8_t type, uint8_t rom_code, uint8_t ram_code, int banks) {
    size_t size = (size_t)banks * 0x4000;
    uint8_t *rom = calloc(1, size);
    for (int b = 0; b < banks; b++) {
        rom[b * 0x4000] = (uint8_t)b;               /* bank number, low byte */
        rom[b * 0x4000 + 1] = (uint8_t)(b >> 8);    /* bank number, high byte */
    }
    memcpy(&rom[0x134], "CARTTEST", 8);
    rom[0x147] = type;
    rom[0x148] = rom_code;
    rom[0x149] = ram_code;
    FILE *f = fopen(path, "wb");
    fwrite(rom, 1, size, f);
    fclose(f);
    free(rom);
}

static int bank_at_4000(Cart *c) {
    return cart_read(c, 0x4000) | (cart_read(c, 0x4001) << 8);
}

static void test_mbc1(void) {
    printf("MBC1\n");
    make_rom("/tmp/boy_mbc1.gb", 0x03, 0x05, 0x03, 64);   /* 1 MB ROM, 32 KB RAM, battery */
    Cart c;
    CHECK(cart_load(&c, "/tmp/boy_mbc1.gb"), "load");
    CHECK(bank_at_4000(&c) == 1, "starts on bank 1");
    cart_write(&c, 0x2000, 5);
    CHECK(bank_at_4000(&c) == 5, "select bank 5 (got %d)", bank_at_4000(&c));
    cart_write(&c, 0x2000, 0);
    CHECK(bank_at_4000(&c) == 1, "bank 0 maps to 1 (got %d)", bank_at_4000(&c));
    cart_write(&c, 0x2000, 3);
    cart_write(&c, 0x4000, 1);                           /* upper bits: bank 32 + 3 */
    CHECK(bank_at_4000(&c) == 35, "upper bank bits (got %d)", bank_at_4000(&c));
    cart_free(&c);
}

static void test_mbc3(void) {
    printf("MBC3\n");
    make_rom("/tmp/boy_mbc3.gb", 0x13, 0x06, 0x03, 128);  /* 2 MB ROM, 32 KB RAM, battery */
    Cart c;
    CHECK(cart_load(&c, "/tmp/boy_mbc3.gb"), "load");
    cart_write(&c, 0x2000, 0x45);
    CHECK(bank_at_4000(&c) == 0x45, "select bank 0x45 (got %d)", bank_at_4000(&c));
    cart_write(&c, 0x0000, 0x0A);
    cart_write(&c, 0x4000, 2);
    cart_write(&c, 0xA000, 0x77);
    cart_write(&c, 0x4000, 0);
    CHECK(cart_read(&c, 0xA000) != 0x77, "RAM banks are separate");
    cart_write(&c, 0x4000, 2);
    CHECK(cart_read(&c, 0xA000) == 0x77, "RAM bank 2 keeps its data");
    cart_free(&c);
}

static void test_mbc5_and_saves(void) {
    printf("MBC5 + battery save (the chip GB Studio games use)\n");
    remove("/tmp/boy_mbc5.sav");
    make_rom("/tmp/boy_mbc5.gb", 0x1B, 0x08, 0x03, 512);  /* 8 MB ROM, 32 KB RAM, battery */
    Cart c;
    CHECK(cart_load(&c, "/tmp/boy_mbc5.gb"), "load");
    cart_write(&c, 0x2000, 0x34);
    cart_write(&c, 0x3000, 0x01);                        /* 9th bank bit */
    CHECK(bank_at_4000(&c) == 0x134, "select bank 0x134 (got 0x%X)", bank_at_4000(&c));
    cart_write(&c, 0x2000, 0x00);
    cart_write(&c, 0x3000, 0x00);
    CHECK(bank_at_4000(&c) == 0, "bank 0 is allowed on MBC5 (got %d)", bank_at_4000(&c));

    CHECK(cart_read(&c, 0xA000) == 0xFF, "RAM reads 0xFF while disabled");
    cart_write(&c, 0x0000, 0x0A);                        /* enable RAM */
    cart_write(&c, 0x4000, 3);
    cart_write(&c, 0xA123, 0x5A);
    CHECK(cart_read(&c, 0xA123) == 0x5A, "RAM write and read back");
    CHECK(c.ram_dirty, "RAM marked as changed");
    CHECK(cart_save(&c), "save file written");
    CHECK(strcmp(c.save_path, "/tmp/boy_mbc5.sav") == 0, "save path is next to the ROM (%s)", c.save_path);
    cart_free(&c);

    Cart again;
    CHECK(cart_load(&again, "/tmp/boy_mbc5.gb"), "reload");
    cart_write(&again, 0x0000, 0x0A);
    cart_write(&again, 0x4000, 3);
    CHECK(cart_read(&again, 0xA123) == 0x5A, "save data survives a restart");
    cart_free(&again);
    remove("/tmp/boy_mbc5.sav");
}

static void test_bad_files(void) {
    printf("Bad files\n");
    FILE *f = fopen("/tmp/boy_tiny.gb", "wb");
    fputs("not a rom", f);
    fclose(f);
    Cart c;
    CHECK(!cart_load(&c, "/tmp/boy_tiny.gb"), "tiny file is rejected");
    CHECK(!cart_load(&c, "/tmp/does_not_exist.gb"), "missing file is rejected");
}

int main(void) {
    test_mbc1();
    test_mbc3();
    test_mbc5_and_saves();
    test_bad_files();
    printf(failures ? "\n%d cartridge check(s) failed\n" : "\nAll cartridge checks passed\n", failures);
    return failures ? 1 : 0;
}
