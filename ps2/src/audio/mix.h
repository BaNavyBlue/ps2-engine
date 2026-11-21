#ifndef PS2_MIX_H
#define PS2_MIX_H

/* (build 109a) The mix's gains and its last stage, shared by the game's
 * mixer (src/core/audio.c) and tools/q_demo.cpp, so the demo's files are
 * the game's level.
 *
 * Measured through this chain before 109a (docs/audio.md, "The mix's
 * level"): every song peaked between -18 and -23 dBFS, the loudest effects
 * alone at -15.6, the loudest effects over the loudest song at -13.2 -- the
 * music some 20 dB under full scale. The master is raised 12 dB, so that
 * worst case lands near -1 dBFS and the songs peak between -6 and -11.
 *
 * The last stage was a hard clip commented as a soft limit. It is a soft
 * knee now: nothing changes below -3 dBFS, and above it the level bends
 * smoothly toward full scale and never reaches it -- the slope is 1 at the
 * knee, so the bend is continuous. Only the rare effect over a loud passage
 * reaches it. */

#include <math.h>

#define MIX_MUSIC_GAIN  0.7f
#define MIX_SFX_GAIN    0.9f
#define MIX_MASTER_GAIN 3.2f          /* 12 dB above build 109's 0.8 */
#define MIX_KNEE        0.70794578f   /* -3 dBFS */

static inline float mix_soft_limit(float x)
{
    float a = fabsf(x);

    if (a <= MIX_KNEE) {
        return x;
    }
    float room = 1.0f - MIX_KNEE;
    float y    = MIX_KNEE + room * tanhf((a - MIX_KNEE) / room);

    return (x < 0.0f) ? -y : y;
}

#endif /* PS2_MIX_H */
