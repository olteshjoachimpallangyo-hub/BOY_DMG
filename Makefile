# BoyDMG - Game Boy emulator
#
#   make                 build the emulator (macOS)
#   make run             build and play the bundled game
#   make run ROM="roms/Your Game.gb"
#   make run SHOW_KEYS=1     also list every key press in Terminal
#   make test            download the test ROMs (first time only) and run all checks
#   make clean           delete build files

CC        ?= clang
CFLAGS    ?= -O2
CFLAGS    += -Wall -Wextra -std=c11 -Iinclude
OBJCFLAGS  = -O2 -Wall -Wextra -fobjc-arc -Iinclude

BUILD     = build
CORE      = cpu bus ppu cart joypad
CORE_OBJ  = $(CORE:%=$(BUILD)/%.o)
HEADERS   = $(wildcard include/*.h)
ROM      ?= roms/libbet.gb

TEST_ROMS = tests/roms
BLARGG    = https://raw.githubusercontent.com/retrio/gb-test-roms/master
ACID2     = https://github.com/mattcurrie/dmg-acid2/releases/download/v1.0/dmg-acid2.gb

.PHONY: all run headless test test-roms clean

ifeq ($(shell uname -s),Darwin)
all: $(BUILD)/boy_dmg
else
all:
	@echo "The windowed emulator needs macOS. On this system you can use: make test"
endif

$(BUILD)/boy_dmg: $(CORE_OBJ) $(BUILD)/main.o $(BUILD)/frontend_cocoa.o
	$(CC) $^ -framework Cocoa -framework CoreVideo -framework QuartzCore -o $@

$(BUILD)/boy_headless: $(CORE_OBJ) $(BUILD)/headless.o
	$(CC) $^ -o $@

$(BUILD)/cart_test: $(BUILD)/cart.o $(BUILD)/cart_test.o
	$(CC) $^ -o $@

$(BUILD)/%.o: src/%.c $(HEADERS) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: tests/%.c $(HEADERS) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/frontend_cocoa.o: src/frontend_cocoa.m $(HEADERS) | $(BUILD)
	$(CC) $(OBJCFLAGS) -c $< -o $@

$(BUILD):
	mkdir -p $(BUILD)

run: $(BUILD)/boy_dmg
	BOYDMG_SHOW_KEYS=$(SHOW_KEYS) ./$(BUILD)/boy_dmg "$(ROM)"

headless: $(BUILD)/boy_headless

# Test ROMs are downloaded instead of stored in the repository
test-roms: $(TEST_ROMS)/cpu_instrs.gb $(TEST_ROMS)/instr_timing.gb $(TEST_ROMS)/halt_bug.gb $(TEST_ROMS)/dmg-acid2.gb

$(TEST_ROMS)/cpu_instrs.gb:
	@mkdir -p $(TEST_ROMS)
	curl -sfL -o $@ $(BLARGG)/cpu_instrs/cpu_instrs.gb

$(TEST_ROMS)/instr_timing.gb:
	@mkdir -p $(TEST_ROMS)
	curl -sfL -o $@ $(BLARGG)/instr_timing/instr_timing.gb

$(TEST_ROMS)/halt_bug.gb:
	@mkdir -p $(TEST_ROMS)
	curl -sfL -o $@ $(BLARGG)/halt_bug.gb

$(TEST_ROMS)/dmg-acid2.gb:
	@mkdir -p $(TEST_ROMS)
	curl -sfL -o $@ $(ACID2)

test: $(BUILD)/boy_headless $(BUILD)/cart_test test-roms
	@sh tests/run_tests.sh

clean:
	rm -rf $(BUILD)