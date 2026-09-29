#include "cpu.h"
#include "bus.h"

#include <stdio.h>

#define FLAG_Z 0x80
#define FLAG_N 0x40
#define FLAG_H 0x20
#define FLAG_C 0x10

/* Clock cycles for each instruction. Conditional jumps, calls and returns
   list the "not taken" time; the extra time is added when they're taken. */
static const uint8_t op_cycles[256] = {
/*        0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F */
/* 0 */   4, 12,  8,  8,  4,  4,  8,  4, 20,  8,  8,  8,  4,  4,  8,  4,
/* 1 */   4, 12,  8,  8,  4,  4,  8,  4, 12,  8,  8,  8,  4,  4,  8,  4,
/* 2 */   8, 12,  8,  8,  4,  4,  8,  4,  8,  8,  8,  8,  4,  4,  8,  4,
/* 3 */   8, 12,  8,  8, 12, 12, 12,  4,  8,  8,  8,  8,  4,  4,  8,  4,
/* 4 */   4,  4,  4,  4,  4,  4,  8,  4,  4,  4,  4,  4,  4,  4,  8,  4,
/* 5 */   4,  4,  4,  4,  4,  4,  8,  4,  4,  4,  4,  4,  4,  4,  8,  4,
/* 6 */   4,  4,  4,  4,  4,  4,  8,  4,  4,  4,  4,  4,  4,  4,  8,  4,
/* 7 */   8,  8,  8,  8,  8,  8,  4,  8,  4,  4,  4,  4,  4,  4,  8,  4,
/* 8 */   4,  4,  4,  4,  4,  4,  8,  4,  4,  4,  4,  4,  4,  4,  8,  4,
/* 9 */   4,  4,  4,  4,  4,  4,  8,  4,  4,  4,  4,  4,  4,  4,  8,  4,
/* A */   4,  4,  4,  4,  4,  4,  8,  4,  4,  4,  4,  4,  4,  4,  8,  4,
/* B */   4,  4,  4,  4,  4,  4,  8,  4,  4,  4,  4,  4,  4,  4,  8,  4,
/* C */   8, 12, 12, 16, 12, 16,  8, 16,  8, 16, 12,  0, 12, 24,  8, 16,
/* D */   8, 12, 12,  4, 12, 16,  8, 16,  8, 16, 12,  4, 12,  4,  8, 16,
/* E */  12, 12,  8,  4,  4, 16,  8, 16, 16,  4, 16,  4,  4,  4,  8, 16,
/* F */  12, 12,  8,  4,  4, 16,  8, 16, 12,  8, 16,  4,  4,  4,  8, 16,
};

/* ---------------- Small helpers ---------------- */

static inline uint8_t rd(CPU *c, uint16_t a)             { return bus_read(c->bus, a); }
static inline void    wr(CPU *c, uint16_t a, uint8_t v)  { bus_write(c->bus, a, v); }

static inline uint8_t fetch8(CPU *c) { return rd(c, c->pc++); }
static inline uint16_t fetch16(CPU *c) {
    uint8_t lo = fetch8(c);
    uint8_t hi = fetch8(c);
    return (uint16_t)(lo | (hi << 8));
}

static inline void push16(CPU *c, uint16_t v) {
    wr(c, --c->sp, (uint8_t)(v >> 8));
    wr(c, --c->sp, (uint8_t)(v & 0xFF));
}
static inline uint16_t pop16(CPU *c) {
    uint8_t lo = rd(c, c->sp++);
    uint8_t hi = rd(c, c->sp++);
    return (uint16_t)(lo | (hi << 8));
}

static inline void set_flags(CPU *c, bool z, bool n, bool h, bool cy) {
    c->f = (uint8_t)((z ? FLAG_Z : 0) | (n ? FLAG_N : 0) | (h ? FLAG_H : 0) | (cy ? FLAG_C : 0));
}
static inline bool flag(const CPU *c, uint8_t f) { return (c->f & f) != 0; }

/* Registers by their 3-bit instruction code: B C D E H L (HL) A */
static uint8_t get_r(CPU *c, int i) {
    switch (i) {
        case 0: return c->b;  case 1: return c->c;
        case 2: return c->d;  case 3: return c->e;
        case 4: return c->h;  case 5: return c->l;
        case 6: return rd(c, c->hl);
        default: return c->a;
    }
}
static void set_r(CPU *c, int i, uint8_t v) {
    switch (i) {
        case 0: c->b = v; break;  case 1: c->c = v; break;
        case 2: c->d = v; break;  case 3: c->e = v; break;
        case 4: c->h = v; break;  case 5: c->l = v; break;
        case 6: wr(c, c->hl, v); break;
        default: c->a = v; break;
    }
}

/* Register pairs for 16-bit loads, INC/DEC and ADD HL: BC DE HL SP */
static uint16_t get_rr(CPU *c, int i) {
    switch (i) { case 0: return c->bc; case 1: return c->de; case 2: return c->hl; default: return c->sp; }
}
static void set_rr(CPU *c, int i, uint16_t v) {
    switch (i) { case 0: c->bc = v; break; case 1: c->de = v; break; case 2: c->hl = v; break; default: c->sp = v; break; }
}

/* Conditions by their 2-bit code: NZ Z NC C */
static bool condition(const CPU *c, int i) {
    switch (i) {
        case 0: return !flag(c, FLAG_Z);
        case 1: return  flag(c, FLAG_Z);
        case 2: return !flag(c, FLAG_C);
        default: return flag(c, FLAG_C);
    }
}

/* ---------------- Arithmetic ---------------- */

/* The 8 accumulator operations, by their 3-bit code: ADD ADC SUB SBC AND XOR OR CP */
static void alu(CPU *c, int op, uint8_t v) {
    uint8_t a = c->a;
    int carry = flag(c, FLAG_C) ? 1 : 0;
    int r;
    switch (op) {
        case 0: /* ADD */
            r = a + v;
            set_flags(c, (uint8_t)r == 0, false, ((a & 0xF) + (v & 0xF)) > 0xF, r > 0xFF);
            c->a = (uint8_t)r;
            break;
        case 1: /* ADC */
            r = a + v + carry;
            set_flags(c, (uint8_t)r == 0, false, ((a & 0xF) + (v & 0xF) + carry) > 0xF, r > 0xFF);
            c->a = (uint8_t)r;
            break;
        case 2: /* SUB */
            r = a - v;
            set_flags(c, (uint8_t)r == 0, true, (a & 0xF) < (v & 0xF), r < 0);
            c->a = (uint8_t)r;
            break;
        case 3: /* SBC */
            r = a - v - carry;
            set_flags(c, (uint8_t)r == 0, true, ((a & 0xF) - (v & 0xF) - carry) < 0, r < 0);
            c->a = (uint8_t)r;
            break;
        case 4: /* AND */
            c->a = a & v;
            set_flags(c, c->a == 0, false, true, false);
            break;
        case 5: /* XOR */
            c->a = a ^ v;
            set_flags(c, c->a == 0, false, false, false);
            break;
        case 6: /* OR */
            c->a = a | v;
            set_flags(c, c->a == 0, false, false, false);
            break;
        default: /* CP: subtract without storing */
            r = a - v;
            set_flags(c, (uint8_t)r == 0, true, (a & 0xF) < (v & 0xF), r < 0);
            break;
    }
}

static uint8_t inc8(CPU *c, uint8_t v) {
    uint8_t r = (uint8_t)(v + 1);
    set_flags(c, r == 0, false, (v & 0x0F) == 0x0F, flag(c, FLAG_C));
    return r;
}
static uint8_t dec8(CPU *c, uint8_t v) {
    uint8_t r = (uint8_t)(v - 1);
    set_flags(c, r == 0, true, (v & 0x0F) == 0x00, flag(c, FLAG_C));
    return r;
}

static void add_hl(CPU *c, uint16_t v) {
    uint32_t r = (uint32_t)c->hl + v;
    set_flags(c, flag(c, FLAG_Z), false, ((c->hl & 0x0FFF) + (v & 0x0FFF)) > 0x0FFF, r > 0xFFFF);
    c->hl = (uint16_t)r;
}

/* SP + signed byte, used by ADD SP,e and LD HL,SP+e. Flags come from the low byte. */
static uint16_t sp_plus_e(CPU *c) {
    uint8_t e = fetch8(c);
    uint16_t r = (uint16_t)(c->sp + (int8_t)e);
    set_flags(c, false, false, ((c->sp & 0x0F) + (e & 0x0F)) > 0x0F, ((c->sp & 0xFF) + e) > 0xFF);
    return r;
}

static void daa(CPU *c) {
    uint8_t a = c->a;
    bool carry = flag(c, FLAG_C);
    if (!flag(c, FLAG_N)) {              /* after an addition */
        if (carry || a > 0x99) { a = (uint8_t)(a + 0x60); carry = true; }
        if (flag(c, FLAG_H) || (a & 0x0F) > 0x09) a = (uint8_t)(a + 0x06);
    } else {                             /* after a subtraction */
        if (carry) a = (uint8_t)(a - 0x60);
        if (flag(c, FLAG_H)) a = (uint8_t)(a - 0x06);
    }
    set_flags(c, a == 0, flag(c, FLAG_N), false, carry);
    c->a = a;
}

/* ---------------- CB-prefixed instructions ---------------- */

static int execute_cb(CPU *c) {
    uint8_t op = fetch8(c);
    int reg = op & 7;
    int bit = (op >> 3) & 7;
    uint8_t v = get_r(c, reg);
    bool on_hl = (reg == 6);

    switch (op >> 6) {
        case 0: { /* rotates and shifts */
            bool carry_in = flag(c, FLAG_C);
            uint8_t r;
            bool carry_out;
            switch (bit) {
                case 0: carry_out = v & 0x80; r = (uint8_t)((v << 1) | (v >> 7)); break;             /* RLC  */
                case 1: carry_out = v & 0x01; r = (uint8_t)((v >> 1) | (v << 7)); break;             /* RRC  */
                case 2: carry_out = v & 0x80; r = (uint8_t)((v << 1) | (carry_in ? 1 : 0)); break;   /* RL   */
                case 3: carry_out = v & 0x01; r = (uint8_t)((v >> 1) | (carry_in ? 0x80 : 0)); break;/* RR   */
                case 4: carry_out = v & 0x80; r = (uint8_t)(v << 1); break;                          /* SLA  */
                case 5: carry_out = v & 0x01; r = (uint8_t)((v >> 1) | (v & 0x80)); break;           /* SRA  */
                case 6: carry_out = false;    r = (uint8_t)((v << 4) | (v >> 4)); break;             /* SWAP */
                default: carry_out = v & 0x01; r = (uint8_t)(v >> 1); break;                         /* SRL  */
            }
            set_flags(c, r == 0, false, false, carry_out);
            set_r(c, reg, r);
            return on_hl ? 16 : 8;
        }
        case 1: /* BIT */
            set_flags(c, !(v & (1 << bit)), false, true, flag(c, FLAG_C));
            return on_hl ? 12 : 8;
        case 2: /* RES */
            set_r(c, reg, (uint8_t)(v & ~(1 << bit)));
            return on_hl ? 16 : 8;
        default: /* SET */
            set_r(c, reg, (uint8_t)(v | (1 << bit)));
            return on_hl ? 16 : 8;
    }
}

/* ---------------- Main instruction decoder ---------------- */

static int execute(CPU *c, uint8_t op) {
    int cycles = op_cycles[op];

    /* 0x40-0x7F: LD r, r' (0x76 is HALT) */
    if (op >= 0x40 && op <= 0x7F) {
        if (op == 0x76) {
            uint8_t pending = c->bus->ie & c->bus->if_flags & 0x1F;
            if (!c->ime && pending) {
                c->halt_bug = true;      /* hardware quirk: HALT is skipped, next byte read twice */
            } else {
                c->halted = true;
            }
        } else {
            set_r(c, (op >> 3) & 7, get_r(c, op & 7));
        }
        return cycles;
    }

    /* 0x80-0xBF: ALU A, r */
    if (op >= 0x80 && op <= 0xBF) {
        alu(c, (op >> 3) & 7, get_r(c, op & 7));
        return cycles;
    }

    switch (op) {
        /* ---- 0x00-0x3F ---- */
        case 0x00: break;                                              /* NOP */
        case 0x10: fetch8(c); c->bus->sysclk = 0; break;               /* STOP (treated as a pause-less NOP) */

        case 0x01: case 0x11: case 0x21: case 0x31:                    /* LD rr, nn */
            set_rr(c, op >> 4, fetch16(c)); break;

        case 0x02: wr(c, c->bc, c->a); break;                          /* LD (BC), A */
        case 0x12: wr(c, c->de, c->a); break;                          /* LD (DE), A */
        case 0x22: wr(c, c->hl++, c->a); break;                        /* LD (HL+), A */
        case 0x32: wr(c, c->hl--, c->a); break;                        /* LD (HL-), A */
        case 0x0A: c->a = rd(c, c->bc); break;                         /* LD A, (BC) */
        case 0x1A: c->a = rd(c, c->de); break;                         /* LD A, (DE) */
        case 0x2A: c->a = rd(c, c->hl++); break;                       /* LD A, (HL+) */
        case 0x3A: c->a = rd(c, c->hl--); break;                       /* LD A, (HL-) */

        case 0x03: case 0x13: case 0x23: case 0x33:                    /* INC rr */
            set_rr(c, op >> 4, (uint16_t)(get_rr(c, op >> 4) + 1)); break;
        case 0x0B: case 0x1B: case 0x2B: case 0x3B:                    /* DEC rr */
            set_rr(c, op >> 4, (uint16_t)(get_rr(c, op >> 4) - 1)); break;
        case 0x09: case 0x19: case 0x29: case 0x39:                    /* ADD HL, rr */
            add_hl(c, get_rr(c, op >> 4)); break;

        case 0x04: case 0x0C: case 0x14: case 0x1C:                    /* INC r */
        case 0x24: case 0x2C: case 0x34: case 0x3C:
            set_r(c, (op >> 3) & 7, inc8(c, get_r(c, (op >> 3) & 7))); break;
        case 0x05: case 0x0D: case 0x15: case 0x1D:                    /* DEC r */
        case 0x25: case 0x2D: case 0x35: case 0x3D:
            set_r(c, (op >> 3) & 7, dec8(c, get_r(c, (op >> 3) & 7))); break;
        case 0x06: case 0x0E: case 0x16: case 0x1E:                    /* LD r, n */
        case 0x26: case 0x2E: case 0x36: case 0x3E:
            set_r(c, (op >> 3) & 7, fetch8(c)); break;

        case 0x07: /* RLCA */
            set_flags(c, false, false, false, c->a & 0x80);
            c->a = (uint8_t)((c->a << 1) | (c->a >> 7)); break;
        case 0x0F: /* RRCA */
            set_flags(c, false, false, false, c->a & 0x01);
            c->a = (uint8_t)((c->a >> 1) | (c->a << 7)); break;
        case 0x17: { /* RLA */
            uint8_t cin = flag(c, FLAG_C) ? 1 : 0;
            set_flags(c, false, false, false, c->a & 0x80);
            c->a = (uint8_t)((c->a << 1) | cin); break;
        }
        case 0x1F: { /* RRA */
            uint8_t cin = flag(c, FLAG_C) ? 0x80 : 0;
            set_flags(c, false, false, false, c->a & 0x01);
            c->a = (uint8_t)((c->a >> 1) | cin); break;
        }

        case 0x08: { /* LD (nn), SP */
            uint16_t addr = fetch16(c);
            wr(c, addr, (uint8_t)(c->sp & 0xFF));
            wr(c, (uint16_t)(addr + 1), (uint8_t)(c->sp >> 8));
            break;
        }

        case 0x18: { int8_t e = (int8_t)fetch8(c); c->pc = (uint16_t)(c->pc + e); break; }   /* JR e */
        case 0x20: case 0x28: case 0x30: case 0x38: {                                        /* JR cc, e */
            int8_t e = (int8_t)fetch8(c);
            if (condition(c, (op >> 3) & 3)) { c->pc = (uint16_t)(c->pc + e); cycles += 4; }
            break;
        }

        case 0x27: daa(c); break;                                                           /* DAA */
        case 0x2F: c->a = (uint8_t)~c->a; c->f |= FLAG_N | FLAG_H; break;                   /* CPL */
        case 0x37: c->f = (uint8_t)((c->f & FLAG_Z) | FLAG_C); break;                       /* SCF */
        case 0x3F: c->f = (uint8_t)((c->f & FLAG_Z) | ((c->f & FLAG_C) ? 0 : FLAG_C)); break; /* CCF */

        /* ---- 0xC0-0xFF ---- */
        case 0xC0: case 0xC8: case 0xD0: case 0xD8:                    /* RET cc */
            if (condition(c, (op >> 3) & 3)) { c->pc = pop16(c); cycles += 12; }
            break;
        case 0xC9: c->pc = pop16(c); break;                            /* RET */
        case 0xD9: c->pc = pop16(c); c->ime = true; break;             /* RETI */

        case 0xC2: case 0xCA: case 0xD2: case 0xDA: {                  /* JP cc, nn */
            uint16_t addr = fetch16(c);
            if (condition(c, (op >> 3) & 3)) { c->pc = addr; cycles += 4; }
            break;
        }
        case 0xC3: c->pc = fetch16(c); break;                          /* JP nn */
        case 0xE9: c->pc = c->hl; break;                               /* JP HL */

        case 0xC4: case 0xCC: case 0xD4: case 0xDC: {                  /* CALL cc, nn */
            uint16_t addr = fetch16(c);
            if (condition(c, (op >> 3) & 3)) { push16(c, c->pc); c->pc = addr; cycles += 12; }
            break;
        }
        case 0xCD: { uint16_t addr = fetch16(c); push16(c, c->pc); c->pc = addr; break; }   /* CALL nn */

        case 0xC7: case 0xCF: case 0xD7: case 0xDF:                    /* RST n */
        case 0xE7: case 0xEF: case 0xF7: case 0xFF:
            push16(c, c->pc); c->pc = (uint16_t)(op & 0x38); break;

        case 0xC1: c->bc = pop16(c); break;                            /* POP */
        case 0xD1: c->de = pop16(c); break;
        case 0xE1: c->hl = pop16(c); break;
        case 0xF1: c->af = (uint16_t)(pop16(c) & 0xFFF0); break;       /* low 4 bits of F are always 0 */
        case 0xC5: push16(c, c->bc); break;                            /* PUSH */
        case 0xD5: push16(c, c->de); break;
        case 0xE5: push16(c, c->hl); break;
        case 0xF5: push16(c, c->af); break;

        case 0xC6: case 0xCE: case 0xD6: case 0xDE:                    /* ALU A, n */
        case 0xE6: case 0xEE: case 0xF6: case 0xFE:
            alu(c, (op >> 3) & 7, fetch8(c)); break;

        case 0xCB: cycles = execute_cb(c); break;

        case 0xE0: wr(c, (uint16_t)(0xFF00 + fetch8(c)), c->a); break; /* LDH (n), A */
        case 0xF0: c->a = rd(c, (uint16_t)(0xFF00 + fetch8(c))); break;/* LDH A, (n) */
        case 0xE2: wr(c, (uint16_t)(0xFF00 + c->c), c->a); break;      /* LD (C), A */
        case 0xF2: c->a = rd(c, (uint16_t)(0xFF00 + c->c)); break;     /* LD A, (C) */
        case 0xEA: wr(c, fetch16(c), c->a); break;                     /* LD (nn), A */
        case 0xFA: c->a = rd(c, fetch16(c)); break;                    /* LD A, (nn) */

        case 0xE8: c->sp = sp_plus_e(c); break;                        /* ADD SP, e */
        case 0xF8: c->hl = sp_plus_e(c); break;                        /* LD HL, SP+e */
        case 0xF9: c->sp = c->hl; break;                               /* LD SP, HL */

        case 0xF3: c->ime = false; c->ime_scheduled = false; break;    /* DI */
        case 0xFB: c->ime_scheduled = true; break;                     /* EI */

        default:
            /* 0xD3, 0xDB, 0xDD, 0xE3, 0xE4, 0xEB, 0xEC, 0xED, 0xF4, 0xFC, 0xFD
               don't exist. A real Game Boy freezes when it hits one. */
            if (!c->locked) {
                fprintf(stderr, "Invalid opcode 0x%02X at 0x%04X. The CPU has stopped, as a real Game Boy would.\n",
                        op, (uint16_t)(c->pc - 1));
            }
            c->locked = true;
            cycles = 4;
            break;
    }
    return cycles;
}

/* ---------------- Public ---------------- */

void cpu_init(CPU *c, Bus *bus) {
    *c = (CPU){0};
    c->bus = bus;
    /* Register values the original Game Boy boot ROM leaves when it hands over to the game */
    c->af = 0x01B0;
    c->bc = 0x0013;
    c->de = 0x00D8;
    c->hl = 0x014D;
    c->sp = 0xFFFE;
    c->pc = 0x0100;
}

static int service_interrupts(CPU *c) {
    uint8_t pending = c->bus->ie & c->bus->if_flags & 0x1F;
    if (!pending) return 0;

    c->halted = false;                   /* any pending interrupt wakes the CPU, even with IME off */
    if (!c->ime) return 0;

    for (int i = 0; i < 5; i++) {        /* lowest bit has the highest priority */
        if (pending & (1 << i)) {
            c->ime = false;
            c->bus->if_flags &= (uint8_t)~(1 << i);
            push16(c, c->pc);
            c->pc = (uint16_t)(0x40 + i * 8);
            return 20;
        }
    }
    return 0;
}

int cpu_step(CPU *c) {
    if (c->locked) return 4;

    int cycles = service_interrupts(c);
    if (cycles) return cycles;
    if (c->halted) return 4;

    bool enable_ime = c->ime_scheduled;  /* EI takes effect after the instruction that follows it */

    uint8_t op = rd(c, c->pc);
    if (c->halt_bug) {
        c->halt_bug = false;             /* PC fails to advance once */
    } else {
        c->pc++;
    }
    cycles = execute(c, op);

    if (enable_ime && c->ime_scheduled) {
        c->ime = true;
        c->ime_scheduled = false;
    }
    return cycles;
}
