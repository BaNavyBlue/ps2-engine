#include "rumble.h"

#include "building.h"
#include "game.h"
#include "rom_ids.h"

#include <string.h>

/* (build 160) The rumble (rumble.h). The shakes, at HIGH, in ticks:
 *
 *   hurt   (build 161) in a fight: as the blow lands the small motor full
 *          4 ticks, then the sword's sound followed (13 ticks): the big
 *          motor 50% + 50% by the share of the member's max HP the blow
 *          took (all of it at half), the small one along with it for a
 *          quarter or more; each red flash's sound after it followed
 *          (36 ticks, the next cutting it; within RUMBLE_RED_SPAN of the
 *          blow), the big motor 40%. On the
 *          map (no sword), the same held 13, down over 6. (Build 160:
 *          the big motor 35% + 65%, held 4, down over 8-20, the small
 *          50% for a quarter: too short and too weak for a DualShock 3's
 *          big motor to come up to speed.)
 *   fell   both motors full, the small 10, the big held 16 and down over
 *          32
 *   lost   both full, the small 24, the big held 60 and down over 90:
 *          the game over's erase
 *   floor  (build 161) a buzz with the white field: the small motor full,
 *          the big 60%, 8 + 6 (build 160: 60% and 30%, 6 + 6)
 *   blast  (build 161) the explosion's sound followed (49 ticks, 6 dB
 *          down every 8): the big motor 85%, the small full its first 8
 *   hole   (build 161) the fall's sound followed: the big 70%, the small
 *          full its first 6
 *   shake  (build 161) while the camera shakes: the jitter the big motor
 *          50%; the hard shake the big 90% and the small full
 *   flight (build 161) the ship's departure followed (the big 90%, the
 *          small its first 10), then while it flies the big motor 50%
 *          swelling 15% either way over 128 ticks (Tyler's ship 40% and
 *          10%), stilled by the pause; landed: both full 4 + 24, and the
 *          landing's sound followed (the big 80%, the small its first 6)
 *
 * Several members hurt in one tick (an enemy's technique on the whole
 * party) are one blow, the hardest's. */

#define FULL 0xFFFF

/* (build 161) */
#define HURT_KICK     4
#define KICK_ALL      255
#define SWORD_TICKS   13        /* SXFID_Sword's length, unfollowed */
#define SWELL_TICKS   128
#define SFX_TRACK0    SMPS_MUSIC_TRACKS
#define SFX_TRACK_N   SMPS_SFX_TRACKS

/* A share of a motor's full, in thousandths. */
static u16 part(int permille)
{
    if (permille <= 0) {
        return 0;
    }
    if (permille >= 1000) {
        return FULL;
    }
    return (u16)((u32)FULL * (u32)permille / 1000u);
}

static int voice_left(const RumbleVoice *v)
{
    int left = v->hold + v->decay - v->age;
    return (left > 0) ? left : 0;
}

/* A peak's value at the voice's age. */
static u16 voice_value(const RumbleVoice *v, u16 peak)
{
    if (v->age < v->hold) {
        return peak;
    }
    int left = voice_left(v);
    if (left == 0) {
        return 0;
    }
    return (u16)((u32)peak * (u32)left / (u32)(v->decay + 1));
}

void rumble_voice_start(RumbleVoice *set, int count, u16 low, u16 high,
                        int hold, int decay)
{
    /* A free voice, else the one closest to its end. */
    int pick = 0;
    for (int i = 0; i < count; i++) {
        if (voice_left(&set[i]) < voice_left(&set[pick])) {
            pick = i;
        }
    }
    RumbleVoice *v = &set[pick];
    v->low   = low;
    v->high  = high;
    v->hold  = (u8)hold;
    v->decay = (u8)decay;
    v->age   = 0;
}

void rumble_voices_age(RumbleVoice *set, int count)
{
    for (int i = 0; i < count; i++) {
        if (voice_left(&set[i]) > 0) {
            set[i].age++;
        }
    }
}

RumbleMotors rumble_voices_motors(const RumbleVoice *set, int count)
{
    RumbleMotors m = { 0, 0 };
    for (int i = 0; i < count; i++) {
        u16 low  = voice_value(&set[i], set[i].low);
        u16 high = voice_value(&set[i], set[i].high);
        if (low > m.low) {
            m.low = low;
        }
        if (high > m.high) {
            m.high = high;
        }
    }
    return m;
}

void rumble_test(RumbleVoice *set, int count)
{
    rumble_voice_start(set, count, part(800), part(600), 10, 14);
}

void rumble_reset(Rumble *r)
{
    memset(r, 0, sizeof *r);
}

void rumble_quiet(Rumble *r)
{
    memset(r->voice, 0, sizeof r->voice);
    memset(&r->follow, 0, sizeof r->follow);        /* (build 161) */
    r->sword_due = false;
    r->scene.low  = 0;
    r->scene.high = 0;
}

bool rumble_asked(const Rumble *r, u8 id)
{
    return (r->sfx[id >> 5] & (1u << (id & 31))) != 0;
}

static void start(Rumble *r, u16 low, u16 high, int hold, int decay)
{
    rumble_voice_start(r->voice, RUMBLE_VOICES, low, high, hold, decay);
}

/* ---- (build 161) the shakes that follow a sound ---- */

bool rumble_follows(u8 id, u16 *low, u8 *kick)
{
    switch (id) {
        case SFX_EXPLOSION: {
            *low  = part(850);
            *kick = 8;
            return true;
        }
        case SFX_FELL_IN_HOLE: {
            *low  = part(700);
            *kick = 6;
            return true;
        }
        case SFX_SPACESHIP_DEPARTED: {
            *low  = part(900);
            *kick = 10;
            return true;
        }
        case SFX_SPACESHIP_LANDED: {
            *low  = part(800);
            *kick = 6;
            return true;
        }
        case SFX_DAMAGE_RED_SCREEN: {
            *low  = part(400);
            *kick = 0;
            return true;
        }
        case SFX_SWORD: {
            *low  = part(500);          /* the least: the blow's own */
            *kick = 0;
            return true;
        }
        default: {
            return false;
        }
    }
}

/* The followed shake's end: down to nothing from where it was. */
static void follow_end(Rumble *r)
{
    RumbleFollow *f = &r->follow;
    if (f->id != 0 && f->now.low != 0) {
        start(r, f->now.low, 0, 0, RUMBLE_TAIL);
    }
    memset(f, 0, sizeof *f);
}

static void follow_start(Rumble *r, const Audio *audio, u8 id, u16 low,
                         u8 kick)
{
    RumbleFollow *f = &r->follow;
    follow_end(r);
    u16 tracks = smps_sfx_tracks(&audio->smps, id);
    f->id     = id;
    f->tracks = (u8)((tracks >> SFX_TRACK0) & ((1u << SFX_TRACK_N) - 1));
    for (int i = 0; i < SFX_TRACK_N; i++) {
        f->volume0[i] = audio->smps.tracks[SFX_TRACK0 + i].f.volume;
    }
    f->kick = kick;
    f->age  = 0;
    f->low  = low;
    f->high = FULL;
}

/* The followed effect's loudness this tick: the loudest of its tracks
 * playing and not at rest, by its volume against where it began (0.75 dB
 * a step, 30 dB to nothing); once none of them plays, the shake ends. */
static void follow_tick(Rumble *r, const Audio *audio)
{
    RumbleFollow *f = &r->follow;
    if (f->id == 0) {
        return;
    }
    if (audio == NULL || !audio->smps_ready) {
        follow_end(r);
        return;
    }
    bool playing = false;
    int  loud    = 0;
    for (int i = 0; i < SFX_TRACK_N; i++) {
        if (!(f->tracks & (1u << i))) {
            continue;
        }
        const SmpsTrack *t = &audio->smps.tracks[SFX_TRACK0 + i];
        if (!(t->f.flags & SMPS_F_IN_USE)) {
            continue;
        }
        playing = true;
        if (t->f.flags & SMPS_F_HOLD) {
            continue;                   /* at rest */
        }
        int l = 1000 - ((int)t->f.volume - (int)f->volume0[i]) * 25;
        l = (l < 0) ? 0 : (l > 1000) ? 1000 : l;
        loud = (l > loud) ? l : loud;
    }
    if (!playing) {
        follow_end(r);
        return;
    }
    f->loud     = (u16)loud;
    f->now.low  = (u16)((u32)f->low * (u32)loud / 1000u);
    f->now.high = (f->age < f->kick)
                ? (u16)((u32)f->high * (u32)loud / 1000u) : 0;
    if (f->age < 255) {
        f->age++;
    }
}

/* The effect the driver started this tick, if any: it cut the one before
 * (PlaySFX stops every effect track); a followed one is followed. A blow
 * felt last tick has its sword now, or is shaken without it. */
static void effect_started(Rumble *r, Audio *audio, int screen)
{
    r->started = 0;
    if (audio != NULL && audio->smps_ready) {
        u8 starts = audio->smps.sfx_starts;
        if (starts != r->sfx_starts) {
            r->started = audio->smps.sfx_started;
        }
        r->sfx_starts = starts;
    }
    u8 id = r->started;
    bool battle = (screen == SCREEN_BATTLE);
    bool sword_on = false;
    if (id != 0) {
        follow_end(r);
        u16 low  = 0;
        u8  kick = 0;
        if (id == SFX_SWORD) {
            if (r->sword_due) {
                follow_start(r, audio, id, r->sword_low, r->sword_kick);
                sword_on = true;
            }
        } else if (id == SFX_DAMAGE_RED_SCREEN) {
            if (battle && r->since_blow <= RUMBLE_RED_SPAN
                && rumble_follows(id, &low, &kick)) {
                follow_start(r, audio, id, low, kick);
            }
        } else if (rumble_follows(id, &low, &kick)) {
            follow_start(r, audio, id, low, kick);
            if (id == SFX_EXPLOSION) {
                r->felt |= RUMBLE_FELT_BLAST;
            }
            if (id == SFX_FELL_IN_HOLE) {
                r->felt |= RUMBLE_FELT_HOLE;
            }
        }
    }
    if (r->sword_due && !sword_on) {
        start(r, r->sword_low, 0, SWORD_TICKS, RUMBLE_TAIL);
        r->unfollowed++;
    }
    r->sword_due = false;
}

/* ---- (build 161) the flights and the shake ---- */

/* sin(2 pi k / 32) in thousandths, k 0-32. */
static const s16 SWELL[33] = {
       0,   195,   383,   556,   707,   831,   924,   981,
    1000,   981,   924,   831,   707,   556,   383,   195,
       0,  -195,  -383,  -556,  -707,  -831,  -924,  -981,
   -1000,  -981,  -924,  -831,  -707,  -556,  -383,  -195,
       0
};

static void motion(Rumble *r, const RumbleMotion *mo)
{
    r->scene.low  = 0;
    r->scene.high = 0;
    if (mo == NULL) {
        return;
    }
    if (r->flight == RUMBLE_FLIGHT_LOOP && mo->flight == RUMBLE_FLIGHT_DONE) {
        r->felt |= RUMBLE_FELT_LANDED;
        start(r, FULL, FULL, 4, 24);
    }
    r->flight = mo->flight;
    if (mo->paused) {
        return;                         /* stilled, as the sound is */
    }
    bool ship  = mo->flight == RUMBLE_FLIGHT_FADE_IN
              || mo->flight == RUMBLE_FLIGHT_LOOP;
    bool tyler = mo->building == BUILDING_TYLER_SHIP;
    if (ship || tyler) {
        r->felt |= RUMBLE_FELT_FLIGHT;
        int at    = (int)(r->flight_age % SWELL_TICKS);
        int k     = at / 4;
        int swell = SWELL[k] + (SWELL[k + 1] - SWELL[k]) * (at % 4) / 4;
        int permille = ship ? 500 + 150 * swell / 1000
                            : 400 + 100 * swell / 1000;
        r->scene.low = part(permille);
        r->flight_age++;
    }
    if (mo->shake == RUMBLE_SHAKE_JITTER) {
        r->felt |= RUMBLE_FELT_SHAKE;
        r->scene.low = (part(500) > r->scene.low) ? part(500) : r->scene.low;
    } else if (mo->shake == RUMBLE_SHAKE_HARD) {
        r->felt |= RUMBLE_FELT_SHAKE;
        r->scene.low  = part(900);
        r->scene.high = FULL;
    }
}

/* A blow felt: hurt, and fell when it left nothing -- HP lost since
 * hp_landed. */
static void felt_blow(Rumble *r, int c, u16 hp)
{
    r->felt     |= RUMBLE_FELT_HURT;
    r->hurt_who |= (u8)(1u << c);
    r->damage[c] = (u16)(r->hp_landed[c] - hp);
    if (hp == 0) {
        r->felt     |= RUMBLE_FELT_FELL;
        r->fell_who |= (u8)(1u << c);
    }
    r->hp_landed[c] = hp;
    r->pending[c]   = 0;
}

/* What a tick's felt bits start. */
static void start_felt(Rumble *r, int screen)
{
    int most = -1;
    for (int c = 0; c < RUMBLE_CHARS; c++) {
        if (!(r->hurt_who & (1u << c))) {
            continue;
        }
        int max   = r->max_hp[c];
        int share = (max > 0) ? (int)((u32)r->damage[c] * 1000u / (u32)max)
                              : 1000;
        most = (share > most) ? share : most;
    }
    if (most >= 0) {
        /* (build 161) the blow's kick now, its sword's shake next tick */
        int s = (most > 500) ? 500 : most;
        u16 low  = part(500 + 500 * s / 500);
        u8  kick = (most >= 250) ? KICK_ALL : 0;
        start(r, 0, FULL, HURT_KICK, 0);
        r->since_blow = 0;
        if (screen == SCREEN_BATTLE) {
            r->sword_due  = true;
            r->sword_low  = low;
            r->sword_kick = kick;
        } else {
            start(r, low, (kick != 0) ? FULL : 0, SWORD_TICKS, RUMBLE_TAIL);
        }
    }
    if (r->felt & RUMBLE_FELT_FELL) {
        start(r, FULL, 0, 16, 32);
        start(r, 0, FULL, 10, 0);
    }
    if (r->felt & RUMBLE_FELT_LOST) {
        start(r, FULL, 0, 60, 90);
        start(r, 0, FULL, 24, 0);
    }
    if (r->felt & RUMBLE_FELT_FLOOR) {
        start(r, part(600), FULL, 8, 6);
    }
}

void rumble_watch(Rumble *r, const GameData *gd, int screen, Audio *audio,
                  const RumbleMotion *mo)
{
    rumble_voices_age(r->voice, RUMBLE_VOICES);
    r->felt     = 0;
    r->hurt_who = 0;
    r->fell_who = 0;
    memset(r->damage, 0, sizeof r->damage);
    if (audio != NULL) {
        audio_take_asked(audio, r->sfx);
    } else {
        memset(r->sfx, 0, sizeof r->sfx);
    }

    bool in_party[RUMBLE_CHARS];
    memset(in_party, 0, sizeof in_party);
    for (int i = 0; i <= gd->party_members_num && i < GD_PARTY_MAX; i++) {
        in_party[gd->party_member_id[i] & 7] = true;
    }

    if (!r->primed) {
        for (int c = 0; c < RUMBLE_CHARS; c++) {
            r->hp[c]        = gd->chr[c].hp;
            r->hp_landed[c] = gd->chr[c].hp;
            r->pending[c]   = 0;
            r->in_party[c]  = in_party[c];
        }
        r->screen      = screen;
        r->floor_ticks = RUMBLE_FLOOR_TICKS + 1;
        r->floor_owed  = 0;
        if (audio != NULL && audio->smps_ready) {
            r->sfx_starts = audio->smps.sfx_starts;     /* (build 161) */
        }
        r->flight = (mo != NULL) ? mo->flight : RUMBLE_FLIGHT_NONE;
        r->since_blow  = 0xFFFF;
        r->primed      = true;
        return;
    }

    /* (build 161) the effect the driver started, at this tick's start */
    if (r->since_blow < 0xFFFF) {
        r->since_blow++;
    }
    effect_started(r, audio, screen);

    /* In a fight a blow is reckoned (the HP down) some ticks before it
     * lands (the hurt sound, the target blinking): owed until then. On the
     * map HP lost is felt at once, but for a damage floor's point, the
     * floor's bite felt as it flashed (a fall, when it is the last).
     * Anywhere else the HP is not a blow -- a new game's or a load's --
     * and nothing is felt. */
    bool battle = (screen == SCREEN_BATTLE);
    bool map    = (screen == SCREEN_LEVEL);
    bool landed = battle && rumble_asked(r, SFX_SWORD);
    if (r->floor_ticks < RUMBLE_FLOOR_TICKS + 1) {
        r->floor_ticks++;
    }
    for (int c = 0; c < RUMBLE_CHARS; c++) {
        u16 hp = gd->chr[c].hp;
        r->max_hp[c] = gd->chr[c].max_hp;
        if (!in_party[c] || hp >= r->hp_landed[c] || !(battle || map)) {
            r->hp_landed[c] = hp;
            r->pending[c]   = 0;
        } else if (battle) {
            r->pending[c]++;
            if (landed || r->pending[c] > RUMBLE_PENDING_MAX) {
                if (!landed) {
                    r->late++;
                }
                felt_blow(r, c, hp);
            }
        } else {
            bool point = (r->floor_owed & (1u << c)) != 0
                      && r->floor_ticks <= RUMBLE_FLOOR_TICKS
                      && hp + 1 == r->hp_landed[c];
            if (point) {
                r->floor_owed &= (u8)~(1u << c);
                if (hp == 0) {
                    r->felt     |= RUMBLE_FELT_FELL;
                    r->fell_who |= (u8)(1u << c);
                }
                r->hp_landed[c] = hp;
            } else {
                felt_blow(r, c, hp);
            }
            r->pending[c] = 0;
        }
        r->hp[c]       = hp;
        r->in_party[c] = in_party[c];
    }
    if (screen == SCREEN_GAMEOVER && r->screen != SCREEN_GAMEOVER) {
        r->felt |= RUMBLE_FELT_LOST;
    }
    if (screen == SCREEN_LEVEL && rumble_asked(r, SFX_DANGEROUS_FLOOR)) {
        r->felt       |= RUMBLE_FELT_FLOOR;
        r->floor_ticks = 0;
        r->floor_owed  = 0;
        for (int c = 0; c < RUMBLE_CHARS; c++) {
            if (in_party[c] && gd->chr[c].hp != 0) {
                r->floor_owed |= (u8)(1u << c);
            }
        }
    }
    r->screen = screen;
    motion(r, mo);                      /* (build 161) */
    for (int k = 0; k < RUMBLE_FELT_KINDS; k++) {
        if (r->felt & (1u << k)) {
            r->felt_count[k]++;
        }
    }
    start_felt(r, screen);
    follow_tick(r, audio);              /* (build 161) */
}

RumbleMotors rumble_motors(const Rumble *r)
{
    RumbleMotors m = rumble_voices_motors(r->voice, RUMBLE_VOICES);
    if (r->follow.id != 0) {                        /* (build 161) */
        if (r->follow.now.low > m.low) {
            m.low = r->follow.now.low;
        }
        if (r->follow.now.high > m.high) {
            m.high = r->follow.now.high;
        }
    }
    if (r->scene.low > m.low) {
        m.low = r->scene.low;
    }
    if (r->scene.high > m.high) {
        m.high = r->scene.high;
    }
    return m;
}

RumbleMotors rumble_scale(RumbleMotors m, RumbleLevel level)
{
    static const u32 SHARE[RUMBLE_LEVELS] = { 0, 2, 3, 4 };    /* quarters */
    u32 q = ((unsigned)level < RUMBLE_LEVELS) ? SHARE[level] : 0;
    m.low  = (u16)(m.low * q / 4u);
    m.high = (u16)(m.high * q / 4u);
    return m;
}

static const char *const LEVEL_NAME[RUMBLE_LEVELS] = {
    "OFF", "LOW", "MEDIUM", "HIGH"
};
static const char *const LEVEL_KEY[RUMBLE_LEVELS] = {
    "off", "low", "medium", "high"
};
static const char *const KIND_NAME[RUMBLE_FELT_KINDS] = {
    "hurt", "fell", "lost", "floor", "blast", "shake", "flight", "landed",
    "hole"
};

const char *rumble_level_name(RumbleLevel level)
{
    return ((unsigned)level < RUMBLE_LEVELS) ? LEVEL_NAME[level] : "?";
}

const char *rumble_level_key(RumbleLevel level)
{
    return ((unsigned)level < RUMBLE_LEVELS) ? LEVEL_KEY[level] : "?";
}

RumbleLevel rumble_level_from_key(const char *key, RumbleLevel fallback)
{
    for (int i = 0; key != NULL && i < RUMBLE_LEVELS; i++) {
        if (strcmp(key, LEVEL_KEY[i]) == 0) {
            return (RumbleLevel)i;
        }
    }
    return fallback;
}

const char *rumble_kind_name(int kind)
{
    return ((unsigned)kind < RUMBLE_FELT_KINDS) ? KIND_NAME[kind] : "?";
}

int rumble_kind_from_name(const char *name)
{
    for (int k = 0; name != NULL && k < RUMBLE_FELT_KINDS; k++) {
        if (strcmp(name, KIND_NAME[k]) == 0) {
            return k;
        }
    }
    return -1;
}
