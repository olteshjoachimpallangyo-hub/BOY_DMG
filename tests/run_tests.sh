#!/bin/sh
# Runs every check. Used by `make test`.

BIN=build/boy_headless
ROMS=tests/roms
failures=0

pass() { echo "  PASS  $1"; }
fail() { echo "  FAIL  $1"; failures=$((failures + 1)); }

# Tests that print "Passed" or "Failed" over the serial port
serial_test() {
    if "$BIN" "$ROMS/$1" --frames "$2" --quiet; then pass "$3"; else fail "$3"; fi
}

# Tests that show their result on screen: compared with a known-good fingerprint
screen_test() {
    got=$("$BIN" "$ROMS/$1" --frames "$2" --quiet --hash)
    if [ "$got" = "$3" ]; then pass "$4"; else fail "$4 (fingerprint $got, expected $3)"; fi
}

echo "Emulator tests"
serial_test cpu_instrs.gb   4000 "CPU: every instruction gives the right result (cpu_instrs)"
serial_test instr_timing.gb 2000 "CPU: every instruction takes the right time (instr_timing)"
screen_test halt_bug.gb     1500 0050d394 "CPU: the HALT quirk (halt_bug)"
screen_test dmg-acid2.gb     120 b312e6f6 "Graphics: pixel-exact background, window and sprites (dmg-acid2)"

echo "Cartridge tests"
if build/cart_test > /tmp/boy_cart_test.log 2>&1; then
    pass "Bank switching (MBC1, MBC3, MBC5) and save files"
else
    fail "Cartridge checks:"
    grep FAIL /tmp/boy_cart_test.log
fi

echo
if [ "$failures" -eq 0 ]; then
    echo "All checks passed."
else
    echo "$failures check(s) failed."
    exit 1
fi
