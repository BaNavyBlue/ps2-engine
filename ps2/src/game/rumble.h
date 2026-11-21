#ifndef PS2_RUMBLE_H
#define PS2_RUMBLE_H

#include "../core/audio.h"
#include "../core/ps2_types.h"
#include "gamedata.h"

#include <stdbool.h>

/* (build 160) The rumble: the port's own, as the Mega Drive pad had no
 * motors (Herzog Zwei's port has the same, its session 54). A gamepad
 * with them (a DualShock 3 or 4, a DualSense, an Xbox pad) shakes for
 * what happens to the party:
 *
 *   hurt   in a fight, a member's HP down, felt as the blow lands -- the
 *          hurt sound (SXFID_Sword, hit_target's loc_3308), not as the
 *          blow is reckoned, which comes first; on the map, HP lost any
 *          other way than a damage floor's point (Roron's cake, ten off
 *          everyone, loc_E5FE); harder for a bigger share of the member's
 *          max HP. HP lower anywhere else -- a new game's, a load's -- is
 *          no blow
 *   fell   a member down to 0 HP: in a fight, or on the map
 *   lost   the party lost: the game over screen
 *   floor  a damage floor bites (loc_7108): its white field and
 *          SXFID_DangerousFloor; the point of HP it takes off everyone
 *          standing eight fields on is the same bite, not felt again
 *          (but as a fall when it is the last)
 *
 * (build 161) And:
 *
 *   blast  an explosion: SXFID_Explosion starting (the Darum scene, the
 *          Climatrol's sixteen bursts, Gaira's control panel, Gaira's
 *          prison blast)
 *   shake  every tick the scene's camera shakes the planes (the
 *          Climatrol shaking, then exploding: CAMERA_JITTER and
 *          CAMERA_SHAKE_THEN_RELOAD), the pause aside
 *   flight every tick a ship flies, the pause aside: the spaceship from
 *          the Central Tower's roof (the ship fading in, SpaceShipLoop)
 *          and Tyler's ship (BUILDING_TYLER_SHIP)
 *   landed the spaceship's flight over (SpaceShipLoop's end)
 *   hole   a fall through a hole in a floor: SXFID_FellInHole starting
 *
 * Nothing here changes the game. rumble_watch looks at it once a tick,
 * after the tick -- the screen, each party member's HP, the ids the game
 * asked the sound for that tick (audio_take_asked), which say when a
 * blow lands and when a floor bites, the effect the sound driver started
 * that tick (Smps.sfx_started, sfx_starts: the driver plays an id asked
 * at its next frame, at the next tick's start, if its priority lets it)
 * and the game's motion (game_motion: the flight, the pause, the
 * building, the shake). What it finds starts voices: a peak on each
 * motor, held some ticks, then down to nothing; or (build 161) a shake
 * that follows a sound -- the blow's sword, each red flash's sound after
 * it, an explosion, the hole's fall, the ship's departure and landing:
 * each motor its peak by the effect's loudness while its tracks play
 * (its volume against where it began, 0.75 dB a step, 30 dB to nothing),
 * then a short tail; the next effect the driver starts cuts it, as
 * PlaySFX stops every effect track. The motors are the loudest of all
 * that, motor by motor, scaled by the Escape menu's RUMBLE.
 *
 * The motors: `low` the big one (a slow, heavy shake; a DualShock 3's
 * left motor, the only one it drives at a strength), `high` the small
 * one (a quick buzz; a DualShock 3's right motor, on or off), each 0 to
 * $FFFF, as SDL3's low and high frequency rumble. Every gamepad feels
 * the party: one player. */

typedef enum {
    RUMBLE_OFF = 0,
    RUMBLE_LOW,
    RUMBLE_MEDIUM,
    RUMBLE_HIGH,
    RUMBLE_LEVELS
} RumbleLevel;

#define RUMBLE_DEFAULT RUMBLE_MEDIUM

/* What a tick felt (Rumble.felt): RUMBLE_FELT_* bits. */
enum {
    RUMBLE_FELT_HURT   = 0x001,
    RUMBLE_FELT_FELL   = 0x002,
    RUMBLE_FELT_LOST   = 0x004,
    RUMBLE_FELT_FLOOR  = 0x008,
    RUMBLE_FELT_BLAST  = 0x010,     /* (build 161) */
    RUMBLE_FELT_SHAKE  = 0x020,
    RUMBLE_FELT_FLIGHT = 0x040,
    RUMBLE_FELT_LANDED = 0x080,
    RUMBLE_FELT_HOLE   = 0x100
};
#define RUMBLE_FELT_KINDS 9

/* (build 161) The game's motion, each tick (game_motion, src/game/game.h). */
enum {
    RUMBLE_FLIGHT_NONE = 0,
    RUMBLE_FLIGHT_FADE_OUT,     /* the roof fading, before the ship */
    RUMBLE_FLIGHT_FADE_IN,      /* the ship fading in, its sound asked */
    RUMBLE_FLIGHT_LOOP,         /* SpaceShipLoop's 120 frames */
    RUMBLE_FLIGHT_DONE          /* landed: the level's screen asked for */
};
enum {
    RUMBLE_SHAKE_NONE = 0,
    RUMBLE_SHAKE_JITTER,        /* CAMERA_JITTER: 0-1 pixels */
    RUMBLE_SHAKE_HARD           /* CAMERA_SHAKE_THEN_RELOAD: 0-7 pixels */
};
typedef struct {
    int  flight;                /* RUMBLE_FLIGHT_*: the spaceship's */
    bool paused;                /* paused_flag */
    int  building;              /* BUILDING_*, -1 off the building screen */
    int  shake;                 /* RUMBLE_SHAKE_*, on the level's screen */
} RumbleMotion;

/* A blow reckoned but not yet landed is felt anyway after this many
 * ticks (none should wait so long: tools/rumble_check.py counts them). */
#define RUMBLE_PENDING_MAX 120
/* The longest shake, lost's: held 60, down over 90. */
#define RUMBLE_LONGEST 150
/* A damage floor's point comes eight ticks after its white field: a point
 * lost within this many is the floor's. */
#define RUMBLE_FLOOR_TICKS 12
/* (build 161) A followed effect's tail, down to nothing after it ends. */
#define RUMBLE_TAIL 6
/* (build 161) A red flash's sound is a blow's within this many ticks of
 * it (they start 16, 32 and 48 after it lands); a blow that took no HP
 * flashes too, and is no blow felt. */
#define RUMBLE_RED_SPAN 64

#define RUMBLE_CHARS  8
#define RUMBLE_VOICES 8

typedef struct {
    u16 low, high;
} RumbleMotors;

/* One shake: its peaks, held `hold` ticks, then down to nothing in a
 * straight line over `decay`; `age` counts its ticks from 0. */
typedef struct {
    u16 low, high;
    u8  hold, decay;
    u8  age;
} RumbleVoice;

/* (build 161) A shake that follows the effect the driver started: the
 * motors at its loudest, the small one only its first `kick` ticks (255
 * all along), while its tracks (6-8, the effect tracks it took) play. */
typedef struct {
    u8   id;                    /* the effect; 0 none */
    u8   tracks;                /* bit t - 6 for each of its tracks */
    u8   volume0[SMPS_SFX_TRACKS];  /* each one's volume as it began */
    u8   kick;
    u8   age;
    u16  loud;                  /* its loudness this tick, 0-1000 */
    u16  low, high;
    RumbleMotors now;           /* this tick's motors */
} RumbleFollow;

typedef struct {
    bool primed;                /* last tick's state is below */
    int  screen;                /* ScreenId */
    bool in_party[RUMBLE_CHARS];
    u16  hp[RUMBLE_CHARS];      /* by character index, last tick's */
    u16  hp_landed[RUMBLE_CHARS];   /* at the last blow felt: the HP a
                                       blow not yet landed is below */
    int  pending[RUMBLE_CHARS]; /* ticks a blow has waited to land */
    int  floor_ticks;           /* since the last floor felt */
    u8   floor_owed;            /* bit a member standing at it, its point
                                   not yet taken */

    /* This tick's: what the game asked the sound for (audio_take_asked),
     * what was felt, and for whom. */
    u32  sfx[8];
    u16  felt;
    u8   hurt_who;              /* bit per character: hurt this tick */
    u8   fell_who;              /* fell this tick */
    u16  damage[RUMBLE_CHARS];  /* the blow's HP, hurt this tick */
    u16  max_hp[RUMBLE_CHARS];
    int  late;                  /* blows felt by RUMBLE_PENDING_MAX, ever */

    u32  felt_count[RUMBLE_FELT_KINDS];   /* since the reset, by kind */
    RumbleVoice voice[RUMBLE_VOICES];

    /* (build 161) */
    u8           sfx_starts;    /* Smps.sfx_starts as last seen */
    u8           started;       /* the effect started this tick, 0 none */
    RumbleFollow follow;
    u16          sword_low;     /* the big motor a blow's sword follows */
    u8           sword_kick;    /* its small motor's ticks */
    bool         sword_due;     /* a blow felt last tick: its sword
                                   starts this tick, else is unheard */
    u32          unfollowed;    /* blows whose sword did not start: shaken
                                   its 13 ticks, ever */
    u16          since_blow;    /* ticks since a blow was felt */
    int          flight;        /* RumbleMotion.flight as last seen */
    u32          flight_age;    /* the flights' unpaused ticks: the swell */
    RumbleMotors scene;         /* the flight's or the shake's, this tick */
} Rumble;

/* Nothing felt, nothing held. The next watch primes. */
void rumble_reset(Rumble *r);

/* Every voice stopped (the Escape menu opening), the state kept. */
void rumble_quiet(Rumble *r);

/* Once a tick, after game_tick, with what it reads: the save block's
 * party (game_debug_data), the screen (Game.screen, a ScreenId), the
 * game's Audio (NULL: no sound, so no blow is heard landing and no sound
 * followed) and (build 161) the game's motion (game_motion). The voices
 * age a tick, and what the tick did starts new ones. Takes the sound's
 * record (audio_take_asked), so one watcher a game. Unprimed, it only
 * looks: nothing is felt. */
void rumble_watch(Rumble *r, const GameData *gd, int screen, Audio *audio,
                  const RumbleMotion *motion);

/* Whether the tick asked the sound for `id`. */
bool rumble_asked(const Rumble *r, u8 id);

/* (build 161) The effects a shake follows, and with what: the big motor's
 * peak and the small one's ticks (0 none). False for any other. A
 * blow's sword and a red flash's sound are followed only in a fight, and
 * the sword only when a blow was felt the tick before. */
bool rumble_follows(u8 id, u16 *low, u8 *kick);

/* The motors now, at HIGH. */
RumbleMotors rumble_motors(const Rumble *r);

/* The motors at a level: LOW a half, MEDIUM three quarters, HIGH all. */
RumbleMotors rumble_scale(RumbleMotors m, RumbleLevel level);

/* A set of voices of the caller's own (the Escape menu's test). */
void         rumble_voice_start(RumbleVoice *set, int count, u16 low,
                                u16 high, int hold, int decay);
void         rumble_voices_age(RumbleVoice *set, int count);
RumbleMotors rumble_voices_motors(const RumbleVoice *set, int count);

/* The Escape menu's test buzz, into one voice set. */
void rumble_test(RumbleVoice *set, int count);

/* RUMBLE's names: upper case for the menu, lower for settings.json; a
 * felt kind's for the logs and the route probe (by bit index). */
const char *rumble_level_name(RumbleLevel level);
const char *rumble_level_key(RumbleLevel level);
RumbleLevel rumble_level_from_key(const char *key, RumbleLevel fallback);
const char *rumble_kind_name(int kind);
int         rumble_kind_from_name(const char *name);

#endif /* PS2_RUMBLE_H */
