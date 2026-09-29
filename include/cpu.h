#ifndef CPU_H
#define CPU_H

#include "gb.h"

typedef struct {
    /* Register pairs: on a little-endian Mac the low byte comes first */
    union { struct { uint8_t f, a; }; uint16_t af; };
    union { struct { uint8_t c, b; }; uint16_t bc; };
    union { struct { uint8_t e, d; }; uint16_t de; };
    union { struct { uint8_t l, h; }; uint16_t hl; };
    uint16_t sp, pc;

    bool ime;                  /* interrupts enabled */
    bool ime_scheduled;        /* EI takes effect after the next instruction */
    bool halted;
    bool halt_bug;             /* HALT quirk: the next byte is read twice */
    bool locked;               /* hit an invalid opcode, like real hardware freezes */

    Bus *bus;
} CPU;

void cpu_init(CPU *cpu, Bus *bus);
int  cpu_step(CPU *cpu);       /* runs one instruction, returns clock cycles used */

#endif
