#include "rng.h"

/* The seed the original substitutes if the state ever reaches zero. */
#define RNG_RESEED 0x2A6D365Au

void rng_init(Rng *r, u32 seed)
{
    r->seed = seed;
}

u16 rng_next(Rng *r)
{
    u32 d1 = r->seed;

    if (d1 == 0) {
        d1 = RNG_RESEED;
    }

    /* asl.l #2 / add.l / asl.l #3 / add.l -- multiply by 41. */
    u32 d0 = d1;
    d1 <<= 2;
    d1 += d0;
    d1 <<= 3;
    d1 += d0;

    /* Fold the high word into the low word and reassemble, matching the
     * move.w / swap / add.w / move.w / swap sequence. */
    u16 lo  = (u16)(d1 & 0xFFFF);
    u16 hi  = (u16)(d1 >> 16);
    u16 mix = (u16)(lo + hi);

    r->seed = ((u32)mix << 16) | lo;
    return mix;
}

u32 rng_next32(Rng *r)
{
    u32 before = r->seed;
    if (before == 0) {
        before = RNG_RESEED;
    }
    u16 mix = rng_next(r);
    return (before & 0xFFFF0000u) | mix;
}

u16 rng_below(Rng *r, u16 bound)
{
    if (bound == 0) {
        return 0;
    }
    return (u16)(rng_next(r) % bound);
}
