#ifndef PS2_RNG_H
#define PS2_RNG_H

#include "ps2_types.h"

/* Direct port of GenerateRandomNumber (ps2.asm line 8752).
 *
 * The original is called from VBlankExit on every single frame, not only
 * when a roll is needed, so the sequence is a function of elapsed frames.
 * That is why the game loop ticks at a fixed rate: change the cadence and
 * encounter rates and damage rolls drift from the original. */

typedef struct {
    u32 seed;
} Rng;

void rng_init(Rng *r, u32 seed);

/* Advances the generator and returns the low word, which is what the
 * original leaves in d0.w. Callers that consumed the whole longword need
 * checking individually as they are ported. */
u16 rng_next(Rng *r);

/* The whole of d0 as GenerateRandomNumber leaves it: the high word is
 * the high word of the seed *before* the step, the low word is the mix.
 * ProcessRandomBattle picks the formation from `swap d0; andi.w #7`, so
 * it reads the high word. */
u32 rng_next32(Rng *r);

/* Convenience wrapper: uniform value in [0, bound). */
u16 rng_below(Rng *r, u16 bound);

#endif /* PS2_RNG_H */
