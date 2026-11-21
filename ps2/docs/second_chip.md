# A second chip for the effects (build 116a): the design

The owner's answer 3 (handover_113_windows.txt): extra voices on the CHIP -- a
second Nuked-OPN2 for the sound effects -- yes, with a design first. This page
is that design. The choices for the owner are marked **Choose**.

**(build 124) Built**, after the owner's "I want the option for both chip and
Q": VOICES EXTRA and LIMITED on either synth, one setting for both, EXTRA the
default, B always running (the two recommendations). As built, and where it
differs from the design below:

* `src/audio/synth.c`'s chip backend holds A and B (two `Fm`s, fm.c
  unchanged). Each renders through its own DC blocker and resampler, A at
  MUSIC's gain and B at EFFECTS', summed into the bus: both filters are
  linear, so that is the design's one-blocker sum up to float rounding. B
  runs two seconds at start-up (its standing DC through its blocker) and is
  then set back in step with A's resampler; under LIMITED it is rendered and
  its output let go.
* LIMITED is A exactly as the one chip was: the plain writes, the hooks'
  marks, fm_render_split. EXTRA: the hooks route every channel write (music
  tracks to A, whether the one chip got them or not; an effect's to B on the
  channel it claimed, to A elsewhere; the driver's to both); the globals
  ($22, $27, $2B) go to both from the plain write. Of the three things B must
  copy, the first (a claimed channel starts from the one chip's registers,
  pitch and FM3's special-mode pitches included) and the third (once the
  effect has ended the music's writes land under B's releasing note too,
  and the music's first strike there ends it: every operator to the fastest
  release, keyed off) are built; the second (the envelope an effect starts
  from) is not, as measured below.
* audio.c resets and replays the chip when VOICES changes with it playing
  (`voices_now`); the AUDIO page's VOICES has no Q ONLY any more.
* Measured (a fight's 40 s, headless, EFFECTS on and off at each way): the
  effects on B within about 1 dB of the one chip's, window by window (20 ms),
  2-3 dB apart in the first window of some strikes (the second thing); the
  music LIMITED loses, 59 dB against the music's 68, gone under EXTRA; the
  headless attract's WAV byte for byte as before at LIMITED and at EXTRA (no
  effect in it); the Escape menu's sound over the title theme: under EXTRA
  the WAV differs from the untouched one only while the sound plays. B costs
  about 3.5% of one core (the attract: 4.7 s against 3.3 s for 40 s).
* Not done from the design's proof list: fm_probe --steal's split, and the
  listening FLACs (The owner: only when something needs his ears).

## What the chip does today

The music is five FM tracks (FM1-FM5) and the drums on FM6's DAC. The game's
56 effects take FM3, FM4 and FM5 only (FM5 in 51 of them, FM4 in 34, FM3 in 18;
read from the sequencer's image, every effect's header). Nothing takes FM1,
FM2 or FM6. Four effect tracks play them: three, plus the special one.

When an effect starts (smps.c `play_sfx`, PlaySFX), the sequencer marks the
music track it displaces as overridden. SetMaxRelRate writes `$FF` into that
channel's release registers, so the music's note there dies at once, and the
effect's writes go to the channel. The music track runs on silently: its
writes stop short of the chip (the hooks pass them with `to_chip` false).
When the effect ends, RestoreBGMChn uploads the music track's instrument to
the channel, and the music's next note sounds there again.

So a chord note or a part drops out for as long as the effect plays. That is
the Mega Drive's own sound. Measured in build 111 with `fm_probe --steal`, over
the battle music: the music loses +6.4 dB against the effect's own energy
under Slasher (whole chord notes gone), -6.4 under Whip, +0.3 under Deban.

Q has had the other way since build 111: an effect on voices of its own, the
music playing on under it (VOICES EXTRA, the default), or taking the channel
as the chip does (VOICES LIMITED, build 112). Q under an effect loses -112,
-121 and -60 dB: nothing.

## The second chip

The chip backend holds two Nuked-OPN2 instances: **A**, the music's, and
**B**, the effects'. Each is an `Fm` as fm.c has it now (the write queue, the
DAC player, the resampler), and fm.c does not change. The backend routes by
the hooks (`track_write`), as Q does (`takes_tracks`):

| Writes | Go to |
|---|---|
| a music track's (0-5), whether the chip gets them or not | A: the music never loses a channel |
| an effect track's (6-9), to the channel it claimed | B, on the same channel |
| an effect track's to any other channel (the pan flag's always lands on FM1) | A, as on the Mega Drive: FM1 is the music's |
| the driver's own (SilenceFM; `$2B`, the DAC switch) | both |
| `$22`, `$27` (global: the LFO, FM3's mode and the timers) | both |
| the DAC's bytes (the drums) | A |

EFFECTS weights B and MUSIC weights A. The two native outputs are summed per
side before the one DC blocker and resampler, so there is no per-channel,
per-clock weighting any more (build 111's `fm_render_split`).

Summing two chips changes nothing in level. The YM2612 puts each channel out
in its own time slot, with no clamp across channels, so the music on A is what
the chip plays with nothing stolen, and the effect on B is what it plays on
its channel. B's standing DC (a silent channel still sits +4 steps above zero)
comes out through the DC blocker as A's does.

## Three things the chip does that B must copy

Build 111c found each of them from the other side, on Q:

1. **The pitch an effect never writes.** Some effects set FM3's special-mode
   flag on FM4 or FM5 and never write their pitch there: the dam opening,
   Sword, Eijia, Gaj, the Pulser's and the Terakite's attacks (docs/audio.md,
   "Battle effects"). On the chip they sound at whatever the music's chord last
   left on that channel. So when an effect claims a channel (the hook's
   `SMPS_HOOK_CLAIM`), B's channel is first given A's registers for it, from
   the chip shadow audio.c already keeps (frequency, then the rest). Q does
   the same.
2. **The envelope an effect starts from.** On the chip, the effect's first
   strike lands on a channel whose operators are falling out of the music's
   note (SetMaxRelRate). A YM2612 attack starts from the level it finds, not
   from silence. On B the channel starts from its last effect's release, or
   from silence. The difference is the first few milliseconds of an effect's
   first note. To be measured once built; if it is audible, B's operators
   could be seeded with A's levels too (a write of each TL, then the key-on).
3. **The effect's end.** RestoreBGMChn uploads the music's instrument under
   the effect's releasing note, which then takes the music's rates and levels
   (at the victory, instrument 0's: silent at once), and the music's next
   strike there ends it outright: one note a channel. Without this, B lets an
   effect ring its own release, as Q's level-up did in 111b, which the owner
   heard as a tone that sustains. So from the claim's end until B's note is
   gone, the music's writes to that channel go to B as well, and the music's
   first key-on there keys B's channel off at the fastest release. Q's 111c
   does the same.
   (build 143) The upload itself reaches B alone. A has played the music
   track all along, so it holds that instrument already, and the upload --
   SetFMIns's levels, then RefreshVolume's -- put the carriers at the
   instrument's own levels, up to 27 dB over the track's volume, for the
   few write slots between: a click on A's sounding note, about a tick
   after every effect's end where the music's part was holding a note (the
   selection sound at each dialog stop: 16 of 39 continues in the new
   game's dialogs). smps.c brackets the upload (SMPS_HOOK_RESTORE) and
   `chip_track_write` keeps it off A. LIMITED, and the chip's writes, are
   as before. docs/audio.md, "Build 143".

## The AUDIO page

VOICES stops being Q ONLY: EXTRA puts effects on voices of their own on
either synth (Q's own voices, the chip's B), and LIMITED is the Mega Drive's
way on either (on the chip, today's one chip exactly: B idle, build 111's
weighting).

**Choose** the chip's default. EXTRA is recommended: it is what the owner asked
for, it matches Q's default, and the chip still plays exactly the ROM's notes
and timbres. LIMITED stays a row away for the hardware's sound. The other
way, LIMITED by default, keeps the chip the unaltered reference.

## Switching

audio.c already replays the chip shadow and each track's shadow on a switch
to a backend that takes tracks. With EXTRA, a switch to the chip replays the
music's (held-back writes included) into A and each sounding effect's into
B. VOICES changed mid-song does the same replay: from LIMITED to EXTRA, the
music's part comes back on A from its attack; from EXTRA to LIMITED, the
effect moves onto A's channel as the chip would have had it.

## Cost

One Nuked-OPN2 takes about 42 ms of CPU a second of audio here. The headless
attract sequence, 60 s rendered: 4.2 s with the audio, 1.7 s without. B could
be clocked only while an effect sounds, but stopping and starting it would
move its standing DC, a thump through the DC blocker. **Choose**: B always
running, about 4% of one core more (recommended), or stopped when idle with
its silent DC subtracted.

## What changes where

* `src/audio/synth.c`: the chip backend's instance becomes the two `Fm`s and
  the routing state (the channels an effect holds, the tails being ended);
  `set_option(VOICES)`; `takes_tracks` when EXTRA.
* `src/audio/fm.c`: nothing. An `Fm` is one chip.
* `src/core/audio.c`: the replay for the chip with EXTRA (above).
* `src/game/sysmenu.c`: VOICES without Q ONLY; its default by synth, if the
  choice is EXTRA for Q and LIMITED for the chip.
* Unchanged: the sequencer and every write it makes to the chip (the write
  hash), the routes' files, LIMITED's sound.

## Proof, once built

* The chip with LIMITED: its WAVs byte for byte as before (the headless
  attract, `fm_probe` on the chip), the routes and the write hash unchanged.
* (build 129b: measured -- `fm_probe --steal ID --over SONG --chip both`,
  docs/audio.md, "Build 129b": the music's loss 0.0 dB under EXTRA, as
  Q's; the effect's tail still ends where the music takes the channel back.)
  The chip with EXTRA, over the music, with `fm_probe --steal` (the queued
  split into the music's loss and the effect's cut fits here): the music's
  loss near nothing, as Q's; the effect on B against the effect on today's chip
  over the same music, 20 ms energy windows (item 2 above aside); its end
  within the chip's time (item 3). Effects to take: Gaj over the battle music,
  the level-up over the victory music, the heal over Step Up, and one of the
  effects with no pitch of their own (item 1).
* A route: VOICES on the chip, EXTRA and LIMITED, `expect sound playing`.
* Listening FLACs: those three effects over their music, the chip LIMITED,
  the chip EXTRA and Q EXTRA.

## Questions for the owner

1. The chip's VOICES default: EXTRA (recommended) or LIMITED.
2. B always running (recommended), or stopped while idle.
