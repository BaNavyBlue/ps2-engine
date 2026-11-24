#ifndef PS2_EXTRACT_EX68K_H
#define PS2_EXTRACT_EX68K_H

/* (build 188) The 68000 code, as far as the C steps read it --
 * extract_npcs.py's decode(): each instruction's length, so a walk stays
 * on instruction boundaries, and its operands. The objects' inits are read
 * through it (ex_npcs.c), and the routines that name a flag byte
 * (ex_flag_names.c). */

#include "extract.h"

/* An operand's kind: modes 0-6 are the register modes by number (Dn, An,
 * (An), (An)+, -(An), d16(An), d8(An,Xn)); mode 7 is one of these. */
enum {
    EX_EA_ABS = 10,     /* value: the address, 24 bits */
    EX_EA_PC,           /* d16(pc): value the address */
    EX_EA_PCX,          /* d8(pc,Xn): value the base */
    EX_EA_IMM           /* #value */
};

typedef struct {
    int       kind;
    int       reg;      /* modes 0-6; 0 for mode 7 */
    long long value;    /* d16 for mode 5, the extension word for 6 */
} ExOperand;

typedef struct {
    long long at;
    u16       op;
    int       size;     /* 1, 2, 4, or 0 for none */
    ExOperand ops[2];
    int       nops;
    bool      has_target;
    long long target;   /* a branch's or a dbcc's destination */
    int       length;   /* bytes */
} ExInsn;

/* The instruction at `at`; false (and *why) for an opcode the 68000 has
 * not, or one running off the ROM. */
bool   ex_m68k_try(Extract *x, long long at, ExInsn *out, const char **why);
/* The same, a stop on either. */
ExInsn ex_m68k(Extract *x, long long at);

static inline bool ex_insn_bra(const ExInsn *i)
{
    return (i->op >> 8) == 0x60;
}

static inline bool ex_insn_rts(const ExInsn *i)
{
    return i->op == 0x4E75;
}

#endif /* PS2_EXTRACT_EX68K_H */
