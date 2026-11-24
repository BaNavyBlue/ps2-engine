/* (build 188) tools/extract_npcs.py's 68000 decoder in C (ex68k.h): every
 * instruction the game uses, its length and its operands, the same
 * instructions refused. */

#include "ex68k.h"

#include <string.h>

typedef struct {
    Extract    *x;
    ExInsn     *in;
    long long   p;          /* the next extension word */
    bool        ok;
    const char *why;
} Dec;

static long long rd(Dec *d, long long at, int n)
{
    if (at < 0 || at + n > (long long)d->x->map.size) {
        if (d->ok) {
            d->why = ex_fmt(d->x, "read of %d at %llX is outside the ROM", n,
                            (unsigned long long)at);
        }
        d->ok = false;
        return 0;
    }
    const u8 *b = d->x->map.rom + at;
    long long v = 0;
    for (int i = 0; i < n; i++) {
        v = (v << 8) | b[i];
    }
    return v;
}

static long long u16_at(Dec *d, long long at)
{
    return rd(d, at, 2);
}

static long long s16_at(Dec *d, long long at)
{
    long long v = rd(d, at, 2);
    return (v >= 0x8000) ? v - 0x10000 : v;
}

static long long s8_at(Dec *d, long long at)
{
    long long v = rd(d, at, 1);
    return (v >= 0x80) ? v - 0x100 : v;
}

static void refuse(Dec *d)
{
    if (d->ok) {
        d->why = ex_fmt(d->x, "opcode %04X at %llX", d->in->op,
                        (unsigned long long)d->in->at);
    }
    d->ok = false;
}

/* An effective address whose extension words start at d->p (_ea). */
static void ea(Dec *d, int mode, int reg, int size)
{
    ExOperand o;
    o.kind  = mode;
    o.reg   = reg;
    o.value = 0;
    int n = 0;
    long long at = d->p;
    if (mode <= 4) {
        n = 0;
    } else if (mode == 5) {
        o.value = s16_at(d, at);
        n = 2;
    } else if (mode == 6) {
        o.value = u16_at(d, at);
        n = 2;
    } else {
        o.reg = 0;
        if (reg == 0) {
            o.kind  = EX_EA_ABS;
            o.value = s16_at(d, at) & 0xFFFFFF;
            n = 2;
        } else if (reg == 1) {
            o.kind  = EX_EA_ABS;
            o.value = rd(d, at, 4) & 0xFFFFFF;
            n = 4;
        } else if (reg == 2) {
            o.kind  = EX_EA_PC;
            o.value = at + s16_at(d, at);
            n = 2;
        } else if (reg == 3) {
            o.kind  = EX_EA_PCX;
            o.value = at + s8_at(d, at + 1);
            n = 2;
        } else if (reg == 4) {
            o.kind = EX_EA_IMM;
            if (size == 4) {
                o.value = rd(d, at, 4);
                n = 4;
            } else {
                long long v = u16_at(d, at);
                o.value = (size == 1) ? (v & 0xFF) : v;
                n = 2;
            }
        } else {
            if (d->ok) {
                d->why = ex_fmt(d->x, "mode 7 reg %d", reg);
            }
            d->ok = false;
            return;
        }
    }
    d->p += n;
    if (d->in->nops < 2) {
        d->in->ops[d->in->nops++] = o;
    }
}

static int size_of(int ss)
{
    return (ss == 0) ? 1 : (ss == 1) ? 2 : 4;
}

static void top0(Dec *d, u16 op, int mode, int reg, int ss)
{
    ExInsn *in = d->in;
    if (op & 0x100) {
        if (mode == 1) {
            d->p += 2;                                 /* movep */
        } else {                                       /* bit op, Dn */
            ExOperand o = { 0, (op >> 9) & 7, 0 };
            in->ops[in->nops++] = o;
            ea(d, mode, reg, 1);
        }
    } else if (((op >> 8) & 0xF) == 8) {              /* bit op, #n */
        ea(d, 7, 4, 1);
        ea(d, mode, reg, 1);
    } else if (ss == 3 || ((op >> 9) & 7) == 7) {
        refuse(d);
    } else {                                           /* ori andi ... cmpi */
        in->size = size_of(ss);
        ea(d, 7, 4, in->size);
        if ((op & 0x3F) != 0x3C) {
            ea(d, mode, reg, in->size);
        }
    }
}

static bool among(u16 v, const u16 *set, int n)
{
    for (int i = 0; i < n; i++) {
        if (v == set[i]) {
            return true;
        }
    }
    return false;
}

static void top4(Dec *d, u16 op, int mode, int reg, int ss)
{
    static const u16 BARE[7] = { 0x4E70, 0x4E71, 0x4E73, 0x4E75, 0x4E76,
                                 0x4E77, 0x4AFC };
    static const u16 TRAP_USP[2] = { 0x4E40, 0x4E60 };
    static const u16 LEA_CHK[2]  = { 0x41C0, 0x4180 };
    static const u16 SWAP_EXT[3] = { 0x4840, 0x4880, 0x48C0 };
    static const u16 NBCD_PEA[3] = { 0x4800, 0x4840, 0x4AC0 };
    ExInsn *in = d->in;
    if (among(op, BARE, 7)) {
        return;                                        /* reset nop rte rts */
    }
    if (op == 0x4E72) {
        d->p += 2;                                     /* stop #n */
    } else if (among(op & 0xFFF0, TRAP_USP, 2) || (op & 0xFFF8) == 0x4E58) {
        return;                                        /* trap, usp, unlk */
    } else if ((op & 0xFFF8) == 0x4E50) {
        d->p += 2;                                     /* link */
    } else if ((op & 0xFF80) == 0x4E80) {
        ea(d, mode, reg, 0);                           /* jsr, jmp */
    } else if (among(op & 0xF1C0, LEA_CHK, 2)) {
        ea(d, mode, reg, (op & 0x40) ? 4 : 2);         /* lea, chk */
    } else if ((op & 0xFB80) == 0x4880 && mode != 0) {
        d->p += 2;                                     /* movem */
        ea(d, mode, reg, (op & 0x40) ? 4 : 2);
    } else if (among(op & 0xFFC0, SWAP_EXT, 3) && mode == 0) {
        return;                                        /* swap, ext */
    } else if (among(op & 0xFFC0, NBCD_PEA, 3)) {
        ea(d, mode, reg, 1);                           /* nbcd, pea, tas */
    } else if ((op & 0xF9C0) == 0x40C0) {
        ea(d, mode, reg, 2);                           /* move sr/ccr */
    } else if ((op & 0xF900) == 0x4000 || (op & 0xFF00) == 0x4A00) {
        if (ss == 3) {                                 /* negx clr neg not tst */
            refuse(d);
            return;
        }
        in->size = size_of(ss);
        ea(d, mode, reg, in->size);
    } else {
        refuse(d);
    }
}

bool ex_m68k_try(Extract *x, long long at, ExInsn *out, const char **why)
{
    Dec d;
    memset(out, 0, sizeof *out);
    d.x   = x;
    d.in  = out;
    d.p   = at + 2;
    d.ok  = true;
    d.why = NULL;
    out->at = at;
    out->op = (u16)u16_at(&d, at);
    if (!d.ok) {
        *why = d.why;
        return false;
    }
    u16 op   = out->op;
    int top  = op >> 12;
    int mode = (op >> 3) & 7;
    int reg  = op & 7;
    int ss   = (op >> 6) & 3;
    if (top == 0) {
        top0(&d, op, mode, reg, ss);
    } else if (top == 1 || top == 2 || top == 3) {    /* move, movea */
        out->size = (top == 1) ? 1 : (top == 3) ? 2 : 4;
        ea(&d, mode, reg, out->size);
        ea(&d, (op >> 6) & 7, (op >> 9) & 7, out->size);
    } else if (top == 4) {
        top4(&d, op, mode, reg, ss);
    } else if (top == 5) {
        if (ss == 3) {
            if (mode == 1) {                           /* dbcc */
                out->has_target = true;
                out->target = d.p + s16_at(&d, d.p);
                d.p += 2;
            } else {                                   /* scc */
                ea(&d, mode, reg, 1);
            }
        } else {                                       /* addq, subq */
            out->size = size_of(ss);
            ea(&d, mode, reg, out->size);
        }
    } else if (top == 6) {                             /* bra bsr bcc */
        out->has_target = true;
        if (op & 0xFF) {
            out->target = d.p + s8_at(&d, at + 1);
        } else {
            out->target = d.p + s16_at(&d, d.p);
            d.p += 2;
        }
    } else if (top == 7) {
        /* moveq */
    } else if (top == 8 || top == 0xC) {              /* or div, and mul */
        int opmode = (op >> 6) & 7;
        if (opmode == 3 || opmode == 7) {
            ea(&d, mode, reg, 2);
        } else if (opmode >= 4 && (mode == 0 || mode == 1)) {
            /* sbcd abcd exg */
        } else {
            out->size = size_of(opmode & 3);
            ea(&d, mode, reg, out->size);
        }
    } else if (top == 9 || top == 0xB || top == 0xD) {
        int opmode = (op >> 6) & 7;                   /* sub cmp eor add */
        if (opmode == 3 || opmode == 7) {
            ea(&d, mode, reg, (opmode == 3) ? 2 : 4);
        } else if (opmode >= 4 && (mode == 0 || mode == 1) && top != 0xB) {
            /* subx, addx */
        } else if (opmode >= 4 && mode == 1) {
            /* cmpm */
        } else {
            out->size = size_of(opmode & 3);
            ea(&d, mode, reg, out->size);
        }
    } else if (top == 0xE) {
        if (ss == 3) {                                 /* shift memory */
            ea(&d, mode, reg, 2);
        }
    } else {
        refuse(&d);
    }
    if (!d.ok) {
        *why = d.why;
        return false;
    }
    out->length = (int)(d.p - at);
    return true;
}

ExInsn ex_m68k(Extract *x, long long at)
{
    ExInsn in;
    const char *why = NULL;
    if (!ex_m68k_try(x, at, &in, &why)) {
        ex_fail(x, "%s", why);
    }
    return in;
}
