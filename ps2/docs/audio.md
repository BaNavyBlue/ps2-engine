# Audio

Moved verbatim from the README in build 81c.

## Audio

The output path is complete: SDL3 opens an `SDL_AUDIO_F32` stereo stream
at 48 kHz and pulls from the mixer on its own thread; the headless target
writes the same mixer output to a 16-bit stereo WAV (`PS2_WAV=out.wav`),
so a run can be measured without a sound card.

**Stereo** (build 57; before it everything was folded to one channel).
The YM2612 has a left and a right output, and each channel's register
`$B4` has a bit for each (7 left, 6 right). The game sets them in every
instrument upload (`$B4` is one of `SetFMIns`'s 26 registers), with
`cfFF_Pan` (keeps AMS/FMS, ORs in the stream byte's L/R bits, writes
`$B4`) and with `cfE3_PanAnim`. Nuked honours them, so `fm_render` keeps
its two outputs apart -- a DC blocker and a resampler per side -- and
writes interleaved left/right frames. The drums are FM6's DAC, so they
pan with FM6. The SN76489 has one output, which the Mega Drive sends to
both sides, so `audio_render` renders it mono and adds it to each. The
per-side scale is the old mono mix's: a voice on both sides is exactly
as loud in each speaker as before, a voice panned hard twice what the
fold-down gave it. `./fm_demo pan.wav smoke` plays a sine on both
speakers, then left only, then right only.

`src/core/psg.[ch]` is a real SN76489 -- three square channels and an LFSR
noise channel, 2 dB attenuation steps, the noise rate optionally slaved to
tone channel 2. Measured output is within 0.2% of the requested pitch.

### FM

`src/audio/fm.[ch]` wraps Nuked-OPN2. Copy `ym3438.c` and `ym3438.h` into
`src/audio/` -- the Makefile's wildcard picks them up, and `fm.c` detects
them with `__has_include` -- then `make fm_demo && ./fm_demo out.wav`
plays real instruments from the game's own bank, and `make && ./ps2`
plays the title theme.

Two details this build of Nuked forces. It exposes only `OPN2_Clock`, so a
sample means clocking 24 times and summing; that is the native rate,
master / 144 = 53267 Hz on NTSC, resampled to the output rate. And
register writes cannot be issued back to back -- the chip latches over
several cycles and an early write is dropped -- so they go through a queue
drained one per generated sample.

`SetFMIns` (ps2.asm:92309) uploads 26 register/value pairs per instrument,
the register numbers coming from the register-order table (`loc_BA818`)
(`$B0,$B4,$40,$48,$44,$4C,$30...$8C`) and the values from a 26-byte block
in the bank `FMInsPtrs` points at. 103 instruments extract. (build 111) 105:
two whose last four bytes are the next one's first had been cut short and
dropped. (build 136d) Both are `assets/music/fm_instruments.json` now, each
register's fields named (README, "Asset formats"), which `tools/fm_demo.c`
and `q_demo` read through `fm_load_instruments`; the game reads its
instruments out of the sequencer's image, as below.

The older note said: YM2612 timbre comes from that chip's
envelope rates, sine quantisation and DAC ladder, and an approximation is
audibly wrong in a way no amount of tuning fixes. The mixer's FM bus is
the seam for a real core (Nuked-OPN2 or ymfm); nothing above it changes.

`make psg_demo && ./psg_demo out.wav` writes a diagnostic: each tone
channel alone, the three together, the 16 attenuation steps, the noise
modes, the noise rate slaved to tone channel 2, and a pitch sweep.

The sound driver (from line 89234) is **SMPS68k, not a Z80 driver**. The
code there is 68000 -- `move.b d1, $1F(a3)`, `jmp loc_BA8BC(pc,d5.w)` --
running on the main CPU and writing the YM2612 and PSG directly. The
`z80ptr` macro only byte-swaps pointers, and `LoadPCMDrums` copying into
Z80 RAM means the Z80 does DAC sample playback and nothing else. So
porting the sequencer is ordinary 68k work with no Z80 emulation needed.

### The sequencer

`src/audio/smps.[ch]` is the driver: the note-stream parser, the
per-frame tick, all 32 coordination flags, the modulation and volume
envelopes, priority and the sound queue. Everything it does to the
outside world goes through four function pointers -- two YM2612 ports,
one DAC byte, one PSG register -- so it drives a real chip or a trace
sink without knowing which.

**There are no PSG tracks.** `UpdateTrack` tests the channel byte and
dispatches to exactly two places, DAC when it is $80 and FM otherwise,
and every track header in the ROM names $80 or 0-6. The six music
voices are one DAC and five FM. `cfF3_NoiseMode` and `cfF5_SetPSGIns`
survive from stock SMPS but nothing drives them: `cfF5` stores a byte
no one reads, and `cfF3` pokes the noise register with the $E0 that
selects it masked off. `SilencePSG` quiets a chip the music never uses.
So the checkpoint of "play the melody on PSG first" was not available;
the equivalent is `smps_trace`, which runs the real streams and prints
what they do without needing a synthesiser at all.

**Data layout.** The driver addresses all of its own data as 16-bit
offsets from `SoundDriverBase` ($B8000), so `extract_music.py` emits one
flat image with those offsets intact rather than relocating songs into
separate files. Nothing is moved, so nothing can be moved wrongly, and
`cfF6_GoTo`, `cfF8_GoSub` and the pointer tables all still work on the
bytes the ROM had. The image is 23593 bytes, of which 4673 are the
driver's own code and are left as zero -- nothing points into them.

**Track state** is the $30-byte block addressed through `a3`,
reconstructed in `smps.h` from every site that touches it. Two of its
fields are aliased on purpose: the `cfF7` loop counters start at $22 and
the `cfF8` return stack grows down from $30, so a third nested gosub
overwrites loop counter 1. (build 139: read again, cfF8 stores a four-byte
return address at $2C, then $28, then $24, and cfF7's counter N sits at
$22 + N -- so the third nested gosub overwrites counters 2 to 5, and a
fourth the algorithm byte, the chord count and counters 0 and 1. No song
nests deeper than two; smps.h's comment says counter 1 too.) Track RAM is one contiguous array for the
same reason -- `PlaySFX` reaches its SFX track as `bgm_track + $90` and
`cfF8_GoSub` on a track whose stack byte was never initialised indexes
$FC past its own block.

**Stream format.** $80-$DF are notes ($80 is a rest), bytes under $80
are durations, $E0+ are coordination flags. A duration byte where a note
would be expected -- which is what `Music_Phantasy` has -- is neither a
rest nor a misparse: `loc_B9A40` tests the byte before calling
`GetFrequency_A` and skips the frequency read entirely, so the track
keeps the pitch it had, takes the new duration, and is keyed off and on
again around it. It is a repeated note written without repeating the
note byte. The mirror case, two note bytes in a row, pushes the second
back and plays the first for whatever length the previous note had.

**Verified** against the real data: all 23 songs run two minutes each
under ASan and UBSan with no stream pointer leaving the image, plus a
sweep of all 56 sound effects over a playing song and a fade-out.
`Music_Phantasy` reads out as a melody on FM1, whole-note bass on FM2
and three-part chords on FM3 -- chord mode is why a four-channel header
fills six voices, since `cfE5` makes tracks 4 and 5 shadow FM3.

Four things in the original are wrong and are kept, because the music
was written against them: `cfEA_SetFMS` and `cfEB_SetAMS` use `and.b`
where SMPS has `or.b`, so they can only clear bits; `cfF3_NoiseMode`
does the same with `andi.b`; and `DoModEnv` sign-extends a negative
envelope byte to $FFFF instead of extending it, so every negative
vibrato delta comes out as -1 before the multiply. Fixing any of them
changes how the game sounds.

    make smps_trace
    ./smps_trace --list
    ./smps_trace --song 0 --frames 600
    ./smps_trace --song 0 --frames 120 --regs

**Wiring.** `audio_music_tick` runs `smps_tick` from `game_tick` every
field, before the fade early-return, because `SoundDriverInput` is
called from the VBlank handler and keeps running through the blocking
fades. The sink pushes chip writes into the same ring the beeps use;
the audio thread drains them into the FM core. `SegaScreen` queues `$E0`
(fade out) as its first instruction and `TitleScreen` queues `$81`
right after loading its palette, before `PaletteFadeTo`, so the music
starts under the fade-in -- both are in `enter_sega` and `enter_title`.
`audio_play_sound` keeps `UpdateSoundQueue`'s last-id latch.

What is still missing for it to be audible is the FM core itself:
`src/audio/fm.c` wants Nuked-OPN2's `ym3438.c`/`.h`, which are not in
the tree. Without them `fm.c` now compiles to a stub, `fm_init` reports
the core as absent (there is a message on stderr at startup), and the
FM bus stays silent -- so `make headless` builds again, and so does
`ps2`, but there is nothing to hear until the two files are copied in.
Verified with a stand-in core that records writes: the title screen's
opening chord arrives as key-ons on FM1, FM2 and FM3+4+5, then the
melody on FM1.

### Drums

The Z80 program (`ps2_pcm_drums.asm`, copied into Z80 RAM by
`LoadPCMDrums` at boot) is the whole of what the Z80 does, and it is
short enough to read in full. It waits on `$1FFF` for a drum byte from
the 68k, tests bits 0-6 in order and takes the first one set -- `$80`
with no bits is an entry of its own -- which picks a sample address, a
length and a `djnz` count. Then, per byte: spin the count, halve the
byte (`srl a`), write it to register `$2A`, re-read `$1FFF`. A new drum
byte interrupts the one playing.

So the samples are raw unsigned 8-bit PCM at half amplitude, and the
rate is instruction timing: 240 + 13 x delay T-states per byte, counted
instruction by instruction in `fm_dac_play`, at the Z80's 3.58 MHz.
That gives 13.5 kHz for the fastest drum and 10.4 kHz for the slowest.
On real hardware the 68k also halts the Z80 around every one of its own
register writes, which stretches these unpredictably; that is not
modelled.

    $80  high tom       $84  snare, 189 ms
    $81  mid tom        $88  kick, 33 ms, hard and clipped -- the most used
    $82  low tom        $A0  open hat, 125 ms of noise, quiet
                        $C0  closed hat, 42 ms of noise
                        $90  unused -- see below

The tom names are the disassembly's own (`$80` "High Tom", `$82` "Low
Tom", the same sample at three delays). (build 130) The rest are named for
what the samples measure as, the whole sample through one window, halved
as the Z80 halves it:

| Drum | Length | Energy below 200 Hz / 200 Hz-2 kHz / 2-6 kHz / above | Centroid | Flatness |
|---|---|---|---|---|
| snare ($84) | 189 ms | 9 / 53 / 39 / 0 % | 1.6 kHz | 0.28 |
| kick ($88) | 33 ms | 86 / 8 / 5 / 0 % | 340 Hz | 0.04 |
| open hat ($A0) | 125 ms | 3 / 39 / 54 / 5 % | 3.2 kHz | 0.31 |
| closed hat ($C0) | 42 ms | 2 / 29 / 62 / 7 % | 3.4 kHz | 0.50 |

A pitched body under broadband noise is a snare; a short burst nearly all
below 200 Hz a kick (The owner heard it as one: build 110b's voicing); the two
noises, with nothing much below 200 Hz and the one three times the other's
length, the open and the closed hi-hat. Measured, not heard: the names
could still be argued. They are `DRUM_*` in `src/audio/drums.h` (the
sequencer, both backends, the sound log's `DAC <-` lines, drum_hits), and
`tools/extract_drums.py --wav DIR` writes each one out at its rate as
`drum_84_snare.wav` and so on.

`$90` is broken in the ROM: its sample is 18 bytes long and the program
plays 513, so it reads past what `LoadPCMDrums` copied into RAM nothing
initialised. Emulators leave that zeroed, so the container is the full
8 KB of Z80 RAM padded the same way and `$90` plays 18 bytes near the
midpoint and then drops to the floor. No song uses it.

(build 136b) The container (`PCMDrums.bin`) is gone: the samples are
`assets/music/drums/<sample>.wav`, the ROM's bytes, and
`assets/music/drums.json` places each in Z80 RAM and gives each drum its
byte, sample, length and delay (README, "Asset formats"). `drums_load`
(src/audio/drums.c) rebuilds the same 8 KB image and drum table from them
for both backends, zero where no sample is, so `$90` still runs on into
the zeroed RAM.

**Player.** The DAC is register `$2A` of the same chip, so the drum
bytes are chip writes and go through `fm_native_sample` -- but not
through the write queue, which moves one write per two samples and
would starve a 12 kHz byte stream. The scheduling matters. Nuked
commits an FM register write when its cycle reaches that register's
slot, up to 24 clocks after the data byte, and cancels the commit if
any address write arrives first. That is the race the 68k driver's
`StopZ80` exists for, and the first version of the player had it:
the DAC's `$2A` address landed between a queued write's address and
its data, so the queued value went to the DAC and its register was
never written. Now a data write at the end of sample N owns sample
N+1; the DAC takes the end-of-sample slot for its address only when
the queue is not mid-pair and puts its data out at clock 12 of the
next sample, where `$2A` commits at once. A byte waits at most one
native sample and the phase accumulator keeps the rate exact.
Measured with a latch-aware stand-in core: 513 of 513 bytes per `$C0`
at 4.344 native samples per byte against 4.345 computed, no cancelled
FM writes.

`$2B` = `$80` (DAC on) is written at init, as the Z80 does after reset;
nothing ever turns it off, so FM6 is the drum channel from boot.

The cursor and confirm sounds are placeholders on PSG channel 2, not
game data.

## The mix's level (build 109a)

The owner: 20 dB below clipping is far too quiet for a final mix. Measured
through the game's chain as build 109 had it -- Nuked-OPN2, the FM bus at
0.7, the master at 0.8 -- with every song rendered for 60 s and every
effect to its end (scratch tools, not packed):

| | peak | RMS |
|---|---|---|
| the 23 songs | -18.2 dBFS (Advanced) to -22.6 | -33.3 to -40.6 dBFS |
| the effects alone | -15.6 dBFS at the loudest ($C1, $C0, $AC) | |
| those three every 2 s over Advanced | -13.2 dBFS | |

The level was lost in the staging, not the chip: fm.c scales the six
channels as though all could peak together, and the two gains took 5 dB
more. `src/audio/mix.h` now holds the mix's gains and its last stage for
the game's mixer and `tools/q_demo.cpp` both:

* **The master 12 dB up**, 0.8 to 3.2 (`MIX_MASTER_GAIN`). The worst case
  above lands at -1.1 dBFS, the songs peak between -6 and -11 dBFS and
  average -21 to -29; the title's theme through the headless game's own
  mixer peaks at -10.0 and averages -25.5 (it was -22.0 and -37.5).
* **A soft knee in place of the clip** (`mix_soft_limit`). The old last
  stage clamped at full scale under a comment calling it a soft limit; at
  the old master nothing reached it. Now nothing changes below -3 dBFS,
  and above it the level bends toward full scale along a tanh with slope 1
  at the knee, never reaching it. The worst case above comes out at -1.3
  dBFS: the knee takes 0.2 dB off the rare effect over a loud passage and
  touches nothing else.

`make audio_check` (`tools/audio_level_check.py`) runs the headless attract
sequence with PS2_WAV and holds the title's theme to a peak between -12 and
-1 dBFS with no sample at full scale: 109's mixer fails it (-22.0 dBFS),
109a's passes (-10.0); route_out/audio_level_check/report.txt. The volume
control the owner wants on an AUDIO page will scale this master, 0 dB the
default.

## The Q test (build 109)

The README's "Not done yet" names swappable synth backends behind the
sequencer: Nuked-OPN2 or ymfm for accuracy, Cycfi's Q for a re-voiced
version. Build 109 is the first, preliminary test of Q; the game's sound
does not change and nothing in it is wired into the game. In order: what
the sequencer hands the chips and where a note-level sink could sit
(written before any design), the build, the demo, what it measured, Q's
strengths and weaknesses, the cost, and a design for a re-voiced backend
for the owner's decision.

Read for it, in full: this file, `src/audio/smps.[ch]` and `fm.[ch]`,
`src/core/audio.[ch]` and `psg.[ch]`, `tools/fm_demo.c` and
`tools/smps_trace.c`; in ps2.asm the two volume flags (cfEC_ChangeVol,
cfF0_SetVol), RefreshVolume, the FM3 special-mode flag (cfFE_FM3SpcMode)
and the unreferenced LFO flag (cf_SetLFOSpd); in Q, the headers the demo
includes and what they include (the oscillators, `phase`, the envelope
generator and its ramps, `svf`, the clippers, `dc_block`, `noise_gen`,
the units and literals), the `poly_synth` and `square_synth` examples,
and the docs' pages on the envelope generator, the svf and setup. The
counts are from a scratch census, not packed: every song run for two
minutes and every effect to its end through a sink that classifies each
write.

### What the sequencer hands the chips

**The path.** The sequencer runs on the game thread, once a field. All it
does to the outside world goes through `SmpsSink`'s three calls: an FM
register write (port, register, value), a drum byte, a PSG byte. audio.c's
sink turns each into a command on the ring to the audio thread, which hands
FM writes to fm.c's queue (one address or one data write per native sample,
53267 Hz), a drum byte to the DAC player and a PSG byte to psg.c. Nuked-OPN2
renders at the native rate; a DC blocker and a linear resampler a side take
it to 48 kHz; the PSG is added mono to both sides; master gain; clip. A
song's first tick is a burst -- the title's theme sends 187 writes -- which
the queue takes about 7 ms to put out.

**Voices.** Music has six tracks: one drum track and up to five FM. Every
song is the drums with FM1, FM2 and FM3, bar My Home ($8D) and Movement
($94), which have FM4 and FM5 as tracks of their own. In the other 21,
FM3's track is a chord of three (the chord-mode flag, cfE5): one stream
drives FM3, FM4 and FM5, each note event three notes read from successive
stream bytes, each key event repeated on all three. FM6 is the drums' DAC.
Sound effects have tracks of their own (three, and a special one) but no
channels of their own: an effect takes FM3, FM4 or FM5 -- none of the ROM's
effects uses FM1, FM2 or FM6 -- by setting the music track's *overridden*
bit, and the music track runs on silently (its writes are dropped) until
the effect ends and gives the channel back with the music's instrument
re-uploaded (RestoreBGMChn). So the SFX bus is not a bus: an effect is a
voice stolen from the music. The mixer's music and SFX gains split all of
the FM, effects included, from the PSG.

**The PSG plays nothing.** No track is a PSG track (docs above). The chip
gets the four silencing writes at every song start and stop (SilencePSG),
and the noise flag's write with its register-select bits masked off (the
`andi.b`, kept); the placeholder beeps (`audio_beep`) have no callers. A
note sink has nothing to take from it.

**A note, register by register,** per FM voice:

* *Instrument.* The instrument flag (cfEF) uploads 26 bytes to the channel
  in the order $B0 (algorithm, feedback), $B4 (pan, AMS, FMS), the four
  total levels, then detune/multiple, rate scaling/attack, AM/decay,
  sustain rate and sustain level/release for the four operators -- no
  SSG-EG. 103 instruments. The index stays in the track block; the chip
  only ever sees the bytes. The title's theme uploads 3 on FM1 in two
  minutes, 1 on FM2, 2 on the chord.
* *Pitch.* A frequency word, block in bits 11-13 and F-number in 0-10,
  written $A4 then $A0. From a note byte: (note - $80 + transpose) & $3F
  indexes FMFreqs (85 entries, entry 0 silence; the mask reaches 64 of
  them). Hz = F-number x 2^(block - 1) x 53267 / 2^20. The table is equal
  tempered but not A440: every entry is 25-27 cents above a note (entry 1
  is B0 + 26 cents, which smps_trace called C0 until build 136c), so a
  re-voiced pitch should come from the word, not a note name. (build 136c)
  The table is music/note_frequencies.json, each entry's block and F-number
  with its concert name, Hz and cents; smps_trace names notes the same way
  (entry 1 B0, each entry a semitone up), so its names are what you hear,
  a quarter of a semitone flat of the truth.
* *Key on and off.* $28 = channel | $F0 on the tick a note starts, after
  its frequency; $28 = channel to release. Every new note is released then
  struck, so a repeated pitch (a duration byte with no note byte) is struck
  again, and the note-stop check releases it three ticks early -- a 50 ms
  gap -- unless the next stream byte is the hold flag. Hold (cfE6, cfEE, and
  a rest) suppresses both: a held note takes its next frequency with no new
  strike, which is legato. A rest ($80) releases and holds. The title's
  theme's melody: 243 strikes and 509 releases in two minutes.
* *Volume.* An attenuation byte added to the carriers' total levels -- the
  algorithm decides which operators are carriers (op 4; ops 2 and 4; ops 2-4;
  all four) -- in the chip's 0.75 dB steps. The set-volume flag (cfF0) sets
  it, the change-volume flag (cfEC) adds to it, each on its own track only:
  in a chord a volume change reaches FM3 and not FM4 or FM5 (the ROM's
  RefreshVolume works on the one track, read). The fade-out adds 1 to
  tracks 1-5 every 5 ticks from the fourth after its command, 39 times,
  and silences everything where the fortieth would be, 199 ticks after the
  command (build 183, from the chip's writes; this said every 4 ticks for
  40 steps -- and see "The fade-out's overrun" below); the
  pause flag (cfE8) sets each music voice to 255 minus its volume and back.
  The title's theme: 136 volume writes on the melody.
* *Pitch within a note.* The vibrato envelope (cfF4; tables 1-4, the songs
  use 1 and 2) rewrites the frequency every tick while a note sounds: the
  word plus a table delta times a multiplier the table's $84 command grows,
  restarted at each note, every negative delta taken as -1 (the ROM's broken
  sign extension, kept). It is in F-number units, so its depth in cents
  depends on where the note sits in its octave. The title's theme's melody
  gets two writes a tick all song, 14,400 in two minutes. Pitch slides
  (cfFC: a speed a tick added to the word, the octave folded by hand at
  $4D1 and $26C), raw frequency words from the stream (cfFD) and FM3's
  special mode (cfFE: four operator frequencies, $AD/$A9, $AE/$AA, $AC/$A8,
  $A6/$A2) are for effects only: no song uses them; 45 of the effects
  slide, 4 use raw words, 16 the special mode. Nothing in the port's driver
  writes $27 (channel 3's mode) or $22 (the LFO), and in the ROM a grep
  finds only an unreferenced flag writing $22 -- so on the chip the special
  mode moves only FM3's own frequency (the last pair) and $B4's AMS and FMS
  do nothing. Not read: the boot's chip set-up, to rule out a table-driven
  write of either.
* *Pan.* $B4's left and right bits, from every instrument upload, the pan
  flag (cfFF) and the pan animation (cfE3), which restarts at each note:
  type 1, in every song, starts a note on the right alone, moves it to the
  left alone after the flag's count of ticks, then to both; type 2 (Never
  Dream, $97) steps left, both, right, both. A chord's three parts are all
  sent the first part's pan byte.

**Drums.** The drum track sends, per hit, FM6's pan ($B6: $80 left for the
high tom, $40 right for the low tom, both otherwise) and then the drum byte
($80-$82 the toms, $84, $88, $A0, $C0; section Drums above). The title's
theme: 299 hits in two minutes ($84 107, $88 125, $C0 63, $A0 4).

**Timing.** Everything the sequencer decides lands on a tick. Tempo is a
stall -- every N ticks each music track's countdown gains one -- so notes
fall on ticks, not on a beat grid. Within a tick: the queue, a new sound,
tempo, the fade, then the music tracks and the effect tracks in order; a
track either starts a note (flags, release, frequency, strike, the pan
animation's start) or sustains one (the pan animation, the early release,
a slide or the vibrato).

### Where a note-level sink could sit

Three places, with what each gets and what it costs; the choice waits for
Q.

1. **Beside the register sink, as a decoder.** A second consumer of the
   same writes, rebuilding voices per chip channel: $28 as key events,
   $A4/$A0 as pitch, a 26-byte upload as an instrument (matched against the
   bank for its index), total levels minus the instrument's as volume, $B4
   as pan, the drum byte. smps.c untouched, effects included for free. But
   it infers what the sequencer knew -- whether a channel is music or an
   effect, the note index, volume against an instrument's own levels -- and
   a stolen channel looks like one voice changing instrument.
2. **Inside the sequencer, at the points that mean something.** They are
   functions already: the strike and release (do_note_on, fm_note_off),
   every frequency sent (send_fm_freq, vibrato and slides included), the
   volume (refresh_volume), the instrument (set_fm_ins), the pan
   (send_pan and the pan flag), the drum (update_drum_track), the PSG
   (write_psg). A note sink called there, beside the register calls, gets
   exactly: the track (music 0-5 or effect 6-9), the chord part, note index
   and transpose, the frequency word, the instrument index, the volume byte,
   and whether the track is overridden. It costs hooks in smps.c, proved
   by routes, checksums and the trial identical with the note sink unset.
3. **Behind the ring, where a backend renders.** Either of the two feeds the
   audio thread through the same ring as new command kinds; its consumer
   (`audio_drain`) and `fm_render` are where a backend takes over, at 48 kHz
   with no native rate and no write queue. A re-voiced backend with no
   stolen channels (effects on voices of their own) is possible only with
   the second, which knows which track is which.

### The build

`make q_demo` is the only C++ in the tree, and optional. Q's q_lib is
header-only C++20 with the header-only infra submodule; the demo needs
neither q_io, PortAudio nor PortMidi. `Q_DIR` finds the clone the way
`PS2_DIR` finds the disassembly, `~/GIT/Q` unless given (`git clone
--recurse-submodules https://github.com/cycfi/Q.git`); the recipe stops
with a message when Q or its infra submodule is missing. The three audio C
files the demo links (smps.c, fm.c, ym3438.c) are compiled as C11 into
`build/q_demo/`, the demo with `$(CXX) -std=c++20`; Q's headers are
`-isystem`, so its own warnings (unused parameters, a strict-aliasing pun,
153 under `-Wpedantic`) stay out of ours, and the demo builds with none
under `-Wall -Wextra -Wpedantic`. `make clean` removes it and `build/`.

Checked here: g++ 13.3 at `-std=c++20` (at C++17 Q fails: it uses
concepts). With every C++ compiler on the path replaced by one that fails
and `Q_DIR` pointing nowhere, `make headless`, `route_probe`, `crt_ref`,
`smps_trace` and `fm_demo` build with no warnings and nothing calls a C++
compiler; `make -n ps2` names none. Q's setup docs ask for GCC 11 or Clang
11 or later: on Arch, `pacman -S gcc` has g++; on Windows, MSYS2 UCRT64's
`mingw-w64-ucrt-x86_64-gcc`, which the game's build already needs, has
it. Neither tried here.

### The demo

`tools/q_demo.cpp`. The note sink is the first of the three places above:
beside the register sink, as a decoder, so the sequencer runs unchanged.
Every write it makes goes to Nuked-OPN2 (fm.c, as in the game) and to a Q
bank, which keeps a shadow of the chip's registers and turns them into
voices: a key-on strikes a voice with the patch the shadow holds, $A4/$A0
retune it (vibrato and legato included), the carriers' total levels set
its loudness (volume flags, fades and the pause included), $B4 pans it,
and a drum byte strikes a drum voice with FM6's pan. The bank's C calls
have fm.c's shape -- `q_bank_write` (fm_write), `q_bank_drum`
(fm_dac_play), `q_bank_render` (fm_render, adding stereo frames) -- and
the sequencer's callbacks into C++ are `extern "C"`: C calls C++ through
nothing else.

    make q_demo && ./q_demo                  # phantasy_q.wav, phantasy_opn2.wav
    ./q_demo --song my_home --seconds 90
    ./q_demo --solo fm1                      # one voice alone, in both files
    ./q_demo --instrument 66                 # one patch, an arpeggio, both ways
    ./q_demo --bench                         # CPU time a voice

The mix is the game's (`src/audio/mix.h`): the FM bus at 0.7, the master
(3.2 from build 109a), the soft knee above -3 dBFS.

**The re-voicing.** Each carrier is a Q band-limited oscillator through an
`svf` low-pass, with the chip's envelope; its modulators decide the rest.
The frequency ratio of its strongest modulator picks the waveform, because
it decides which harmonics FM makes: 1, all of them (a saw); 2, the odd
ones (a square); k, every k-th missing (a pulse of width 1/k); weak
modulation, a sine; feedback on operator 1, a saw. The modulation index --
each modulator's total level (8 pi at TL 0: Nuked adds a 14-bit output,
halved, to a 10-bit phase) through its own envelope -- opens the filter
the way it widens the chip's spectrum. Every fundamental is in phase with
a sine, as the chip's carriers are (a rising saw against a square at one
pitch cancels: the title's chord did, until the saw was turned over), and
every phase is zeroed at a key-on, as the chip does, so partials that beat
start each note alike. Envelopes are the chip's rates as times: the
attenuation's 0.09375 dB steps, the generator's clock every three native
samples, the rate tables' average steps a clock, key scaling from the key
code; the sustain and release widths are the time to fall 40 dB. The
drums are synthesised to stand in for the six samples by what they measure
as: toms with bodies at 214, 195 and 165 Hz falling in pitch, a snare body
at 214 Hz with noise near 2.4 kHz, the short loud 60 Hz one a kick
through a soft clip, the two noises hats; one at a time, as the DAC.

Two things were measured on Nuked rather than read: the pitch is
F-number x 2^(block - 1) x 53267 / 2^20 Hz with a multiple of 0 as 1/2
(the demo's pitches agree with the chip's, below); a detune step is
53267 / 2^20 Hz (the title's bass patch held on Nuked beats at 0.258 Hz at
block 2 and 0.400 Hz at block 3, its detunes 5 and 8 steps apart), with
Nuked's own step table by key code. And one correction from a measurement:
one operator's attack timed to -1 dB on Nuked at rates 10-46 comes in
6-25% under the model, 15% at most rates, so attacks are 0.85 of it.

### What it measured

Every file scored against the Nuked-OPN2 file of the same run: RMS; the
correlation of the two 10 ms loudness envelopes in dB where either is
audible; the share of 50 ms frames whose pitches (autocorrelation) agree
within 50 cents, octaves not folded. After the fixes above and one level
for the FM voices and one for the drums:

| The title's theme | RMS, Q against the chip | envelope correlation | pitch within 50 cents |
|---|---|---|---|
| the whole mix, 60 s | +0.4 dB | 0.84 | (chords: not measurable) |
| FM1, the melody | -0.5 dB | 0.96 | 96.5% |
| FM2, the bass | +1.2 dB | 0.82 | 96.8% |
| FM3, a part of the chord | +0.3 dB | 0.52 | 99.3% |
| the drums | 0.0 dB | 0.42 | |

Four more songs, whole, 40 s: My Home +2.2 dB and 0.85, Under +1.4 and
0.94, Exciting Town +1.5 and 0.90, Never Dream +1.0 and 0.92. Nothing
clipped: at build 109's master the chip's files peaked at 0.08-0.10 of
full scale, the Q files at 0.10-0.14 -- some 20 dB of headroom, which is
what build 109a took back ("The mix's level"). At 109a's master the
title's theme peaks at -10.0 dBFS through the chip and -5.3 through Q: at
the same loudness the Q voices are peakier (the bass patch alone, 10 dB
more so -- its four partials zeroed together at every key-on), so a Q
backend wants its own trim, and a pass over every song, before it can be
chosen.

What the numbers cannot say is whether it sounds good; the files are for
listening (`phantasy_q.flac` beside `phantasy_opn2.flac`, the melody alone,
and the bass patch's arpeggio). What they do say: the notes, their timing,
their pitches, vibrato and loudness come through the register decoder
intact; the chord started negative (-0.2) until the saw was turned over
and the attacks corrected, and the bass's beating was twice the chip's
until the detune step was measured.

### What Q did well and badly

Well:

* Header-only, no dependencies beyond the infra submodule, compiled first
  time; the pieces compose as the examples promise (a voice is a few
  members).
* `svf`: stable and free of zipper noise with its cutoff moved every
  sample, as its docs say; one tick gives low-, band- and high-pass, which
  the hats and snare use.
* Band-limited saw, square and pulse (poly-BLEP) and a table sine, all
  driven by one fixed-point `phase_iterator`: 32-bit phase makes a key-on's
  phase reset, a pulse's offset and a retune (vibrato, legato) trivial.
* `envelope_gen` takes any sequence of ramp segments, so the chip's
  four-phase envelope is a vector of four; `attack()` retriggers from the
  current level, click-free.

Badly:

* `adsr_envelope_gen::sustain_level()` sets the level of the sustain
  segment (index 2), not the decay's target (index 1) that the constructor
  set from the same value -- a setter that does not do what its name says,
  and the docs show the same code. The demo builds its own envelope.
* The docs say `envelope_gen` is not retriggerable; the code retriggers
  (and says so in a comment). The code is the better behaviour; the docs
  are stale.
* A segment of zero width returns one sample of garbage (a linear ramp
  divides by it; an exponential one overshoots by 1/cv); the demo keeps
  every segment at 0.5 ms or more.
* A ramp's curvature (`cv`) cannot be set through `make_envelope_segment`.
* Each segment is a `shared_ptr` to a virtual ramp, called per sample, and
  copying an envelope copies the pointers, so two copies share one state:
  voices have to be built in place and never copied (the demo deletes
  Voice's copy). (build 139d) Q 1.5's copy clones each segment's
  ramp instead; the port builds its voices in place either way.
* `noise_gen.hpp` defines a non-inline variable (`white_noise`) in a
  header: two translation units that include it will not link.
  (build 139d) Q 1.5 makes it inline; it also reads the generator's bits
  signed, so its noise runs -1 to 1 -- before 1.5 it ran 0 to 2. The
  drums keep the old generator as their own (`DrumNoise`), below.
* `base.hpp`'s `fast_inverse` puns a float through `reinterpret_cast`
  (undefined under strict aliasing; g++ warns); C++20 only.

### The cost

`./q_demo --bench`, one voice held ten seconds a patch, here on one core
of a 2.1 GHz Xeon (The owner's machines are faster):

| patch | Q, filter retuned every sample | every 16 samples |
|---|---|---|
| 64, the bass: algorithm 7, three sines and a saw | 91 ns a sample | 58 |
| 65, the melody: algorithm 4, two filtered carriers | 101 | 57 |
| 66, the chord: the same | 101 | 59 |
| 68, the later melody: algorithm 1, one carrier | 88 | 54 |

About 100 ns a voice-sample, 0.5% of a core at 48 kHz; the filter's
retune (a `fast_tan` and a divide) is 40% of it, so retuning at a control
rate of 16 samples (3 kHz) takes a voice to about 0.3%. In the title's
theme the whole bank -- 4.3 FM voices sounding on average, the drums -- is
365 ns a sample, 1.8% of a core; the Nuked chip is 1402-1435 ns a sample,
6.9%, whatever it plays. A re-voiced backend is several times cheaper than
the accurate one, and the accurate one is affordable anyway.

### A re-voiced backend: a design, for decision

(Build 110 built it; "The Q backend" below has what was decided and how.)
What the test suggested, and the choices it left:

1. **Where the notes come from.** The decoder (place 1) worked for the
   music: nothing it needs was missing from the writes, and the game is
   untouched. It stays blind to which track owns a channel, so an effect
   takes its music voice exactly as on the chip. Hooks in the sequencer
   (place 2) know that, and would let effects have voices of their own --
   no stolen chord notes -- at the cost of calls in smps.c, proved by the
   routes. Recommended: the decoder first; hooks only if effects on their
   own voices are wanted. **Choose.**
2. **Where it sits.** Behind the ring, where fm.c sits: `audio_drain`
   hands each FM write to the chosen backend (`fm_write` or
   `q_bank_write`), each drum byte likewise, and `audio_render` calls its
   render. `fm.h`'s four calls become a small table of function pointers
   (`FmBackend`: write, drum, render, shutdown) chosen at start-up; the
   game thread, the sequencer and every route are unchanged. The Q
   backend is C++ behind that `extern "C"` table, in its own directory
   (`src/audio/q/`), built only when `Q_DIR` has Q (`make Q=1`); without
   it the table has Nuked alone, as now. ymfm, when it comes, is a third
   entry the same way.
3. **Choosing it.** A line on the Escape menu's pages -- SOUND: CHIP / Q --
   kept in settings.json, switched between ticks (the new backend is given
   the register shadow, so a note held across the switch keeps sounding).
   **Choose** whether it belongs in the menu or only in settings.json.
4. **Patches.** Derived from the registers, as the demo does, for every
   instrument the game has; a hand-voiced table (`assets/data/q_patches.json`:
   by instrument number, the waveform, filter, envelope times, levels)
   overriding it where the derivation sounds wrong -- which only listening
   can say. The instrument number is not in the writes: the decoder would
   match an upload's 26 bytes against the bank (103 instruments, 93
   distinct: the ten repeats are byte for byte the same sound, so a table
   keyed by the bytes loses nothing), or the hooks would pass it. **Choose** whether overrides are
   wanted before listening.
5. **Drums.** Synthesised (as the demo) or the ROM's six samples played
   through Q's interpolators with its filters. **Choose** after listening.
6. **What it will not do.** FM3's special mode (effects only, and nothing
   sets channel 3's mode register anyway), the LFO (nothing sets it), and
   SSG-EG (no instrument uses it) have no Q equivalent to build; the
   effects that use FM3's special mode play its one frequency, as the
   chip does.

The cost of building it: the backend table and the drain's two lines
(C, proved by the routes and the trial with Nuked chosen), the Q backend
(the demo's bank, with the patch overrides and the pan and level
smoothing it already has), the menu line, and a route that switches the
backend while a song plays and checks the chip's writes are unchanged.

## The Q backend (build 110)

Build 109's design, built, with the owner's answers to its choices: (1) where
the notes come from -- unsure, wanting the trade-offs (below); the backend
takes them from the register decoder, as q_demo did, until he picks hooks;
(2) switching -- an AUDIO page on the Escape menu; (3) patches -- not
answered, so derived only, no override file yet; (4) drums -- examples of
both, so both are built, one chosen by `PS2_Q_DRUMS` until he picks.

### The trade-off in (1), for the owner

Neither choice changes what Nuked-OPN2 hears: the sequencer's register
writes to the chip are the same either way (hooks would be calls beside
them, proved identical when nothing listens). The choice is what Q knows.

* **The decoder** (built): Q rebuilds the notes from the writes the chip
  gets. smps.c is untouched, and Q plays exactly what the chip plays --
  including an effect taking one of the music's channels (FM3-FM5) while it
  sounds, a chord note dropping out as on the Mega Drive.
* **Hooks**: calls in the sequencer where a note starts, stops, retunes or
  changes level. Q would know which track owns each channel, so an effect
  could have a voice of its own and the chord would keep all its notes; it
  would also have the instrument numbers, which makes a hand-voiced patch
  file simpler. The cost is edits to smps.c, proved by the routes and the
  trial with no note sink set.
* **A third way, found while building the page** (not built): the effects
  can be told apart without hooks. After each tick audio.c can read which
  channels the sequencer has lent to an effect track and weight those
  channels separately -- in Q per voice, on the chip per clock, since
  Nuked puts out one channel a clock (OPN2_ChOutput). That gives an EFFECTS
  volume with smps.c untouched; effects would still steal chord notes.

Without one of the last two, MUSIC and MASTER both turn everything down:
the effects play on the music's channels, and the PSG plays nothing.

### What was built

* `src/audio/synth.[ch]`: the backend table (create, load_drums, reset,
  write, drum, render, shutdown). CHIP is Nuked-OPN2 through fm.c; Q is
  `q_synth_backend`, linked only by `make Q=1`.
* `src/audio/q/q_synth.[ch]`: q_demo's bank moved out of the demo, C++20
  behind extern "C" (`q_bank_*` and the table's entry). Moved unchanged:
  q_demo's files before and after the move are identical byte for byte
  (the title's theme and My Home, 20 s each, both paths; instrument 66).
  New in the move: `reset`, `load_drums`, the second drum mode, the trim.
* **The ROM's drums through Q** (`Q_DRUMS_SAMPLES`): the six samples from
  PCMDrums.bin ((build 136b) drums.json and its WAVs) at the Z80's own rates (240 + 13 x delay T-states a byte,
  10.4-13.5 kHz), each byte halved as `srl a` does, read with Q's
  `hermite_interpolate` and a low-pass (`svf`) at 0.45 of the sample's rate
  -- where the chip holds each byte for about four native samples and lets
  the images ring. One at a time, a new hit cutting the last, as the DAC.
  Their gain, +0.8 dB, matches the chip's drums by RMS (`--solo drums`, the
  title's theme and My Home, 60 s). The drums alone over 60 s: the chip
  -47.3 dB, the samples -47.7, the synthesised -45.6.
* **The trim** (`kQTrim`, one a drum mode): `q_demo --trim-pass` plays all
  23 songs for 60 s both ways with the trim at 1 and takes the median of
  Q's peak over the chip's. Synthesised drums +2.4 dB, so 0.760; the ROM's
  samples +0.6 dB, so 0.929 (the synthesised kick goes through a clip, which
  makes the whole mix peakier). The loudest song then peaks at -6.7 dBFS
  (-5.7 with the samples) -- every song under the knee at -3. Q's RMS ends
  about 1 dB under the chip's: at equal loudness its voices stay peakier.
* **audio.c**: every backend created at start-up, each loading the drums
  then; `synth_write` keeps a shadow of both ports and each channel's key
  bits as the writes go to the backend playing; `audio_set_synth` (game
  thread) queues the switch, and the audio thread resets the backend asked
  for and replays the shadow into it -- the mode registers, each channel's
  operators, algorithm and pan, the frequencies (high byte then low, and
  FM3's special-mode pairs), a key-on for each channel whose keys were
  down: about 200 writes, some 8 ms through the chip's queue. A note held
  across the switch sounds again from its attack. Asking for the backend
  playing resets and replays it all the same; a backend the build has not
  got falls back to the chip. `fm_reset` keeps the chip's DC blockers and
  resampler running: zeroing them put the chip's standing DC back as a
  -34 dBFS thump on every switch (measured, then fixed).
* The sequencer's writes are counted and hashed as they enter the sink
  (`audio_write_hash`), for the switch route (below).
* (build 124) **The second chip.** The chip backend is two Nuked-OPN2s:
  under VOICES LIMITED the one chip as ever, under EXTRA the music on A and
  the effects on B, so the chip, like Q, plays an effect over the music
  without losing a note of it (docs/second_chip.md, "Built").
* (build 120) **Muted, then back.** COMPUTER PLAYS's FAST mutes the sound
  and runs thousands of ticks a frame. Until build 120 the sequencer's
  writes still went into the 4,096-entry queue, which the audio thread only
  drains as it renders: seed 1's game to Arima at SPEED AUTO dropped over two
  million of them, so when the sound came back the chips were out of step
  with the music (a test build found Q's per-track state still wrong at the
  end). Now the game thread keeps its own shadow of everything the sequencer
  writes (`Audio.mirror`: both FM ports, the keys, each track's registers,
  claim and instrument, and the PSG's registers with its latch). While muted
  it holds the writes instead of queueing them. When the sound comes back
  it hands the whole shadow over in one `CMD_RESYNC`, before any later write,
  and the audio thread sets the PSG from it and the FM side the way a switch
  does: reset, then every register, key and track replayed. The same test
  build found the audio thread's shadow and the PSG equal to the mirror
  after AUTO, FAST and NORMAL games, and unequal with the old dropping.
  Never muted (the routes, the WAV, the window at NORMAL) nothing changes:
  the same writes, queued as before.
  (build 169) Equal registers were not the whole of it. Under VOICES EXTRA
  (the default) and Q, the replay keys each track from its own record, and
  until 169 a record kept its keys down until its own track wrote again.
  So the replay struck notes long ended in two ways:
  - SilenceFM's key-offs at a new song are the driver's own writes
    (`SMPS_TRACK_DRIVER`), no track's, so the last song's held notes stayed
    down in their records.
  - The drum track's key-off is $28 = $80 (cfF2_TrkEnd's FMNoteOff on the
    DAC channel, as the ROM writes it). That keys FM1's operator 4, and on
    the chip FM1's next key write ends it; in the record it never did.

  The owner heard it in COMPUTER PLAYS: "in the home a pitch rings when it
  exits the fast play". At AUTO's return at the knock in Rolf's house, the
  street's FM4 note rang under the house's music for five seconds, about as
  loud as the music (−28 dBFS of difference against the music's −25), and an
  old door effect's note came back on B. Now a key write ends every other
  track's keys down on its channel among the writes that reach the same
  chip: the music's and the driver's on A, the effects' and the driver's on
  B (audio.c, `shadow_track`). LIMITED's replay keys from the chip's own
  shadow and was right all along. Nothing played live changes: the records
  are read only by a replay. Route `audio_replay_silenced` stops the
  title's theme with $FE, the driver's silence, and switches: 168 struck
  the chord again (−16.9 dBFS, a FAIL); 169 is silent.
* `make audio_check_q`: `ps2_headless_q` (the headless game with Q, whatever
  Q= says) with `PS2_SYNTH=q`, the title's theme in the chip's window (-12
  to -1 dBFS), no sample at full scale, and within 3 dB of the chip's peak:
  Q -11.6 dBFS against the chip's -10.0, RMS -28.0 against -25.5. `make
  audio_check` names the chip (`PS2_SYNTH=chip`), so a settings.json that
  chose Q cannot move it.
* `q_demo`: links the backend; `--drums synth|samples`, `--trim-pass`.

What it will not do is as in the design: no FM3 special mode beyond its
one frequency, no LFO, no SSG-EG (nothing in the game uses them).

Listening (beside the tarball, not in it; FLAC, the game's level): the
title's theme 60 s through the chip, through Q with the synthesised drums,
through Q with the ROM's samples; and the drums alone, 60 s, all three ways.

### The AUDIO page

On the Escape menu after CRT; its value is the synth that plays. C opens
the page, drawn as the CRT page is:

| Row | Values | settings.json `"audio"` |
|---|---|---|
| SYNTH | CHIP, Q -- NOT BUILT under Q where the build has none, and the chip plays | `"synth"`: `"chip"`, `"q"` |
| MASTER | OFF, -30 dB ... 0 dB a dB at a time, 0 the default | `"master"`: dB or `"off"` |
| MUSIC | the same | `"music"` |

Left and right step a value and stop at its ends; C steps on and goes round
(0 dB to OFF). MASTER scales the mix's master gain (mix.h), MUSIC the FM
bus -- the music and, until the effects are told apart (above), the effects
riding its channels. The top is 0 dB: the mix's worst passage already comes
near full scale there, and the game's font has no '+'. No EFFECTS row: the
notes come from the decoder. A file from before 110 has no `"audio"` and
reads as the chip at 0 dB; a synth name it does not know keeps the chip; a
volume above 0 reads as 0, below -30 as OFF. Q chosen in a build without it
is kept in the file (the build with it will play it).

main.c hands the page to the mixer when it changes and writes the file;
the start-up line says what plays (`audio: chip, master 0 dB, music 0 dB`,
or `chip (q asked for: not built)`), and a switch prints `audio: synth q`.
`PS2_SYNTH=chip|q` picks the synth for one run whatever the file says (the
level checks use it).

Measured in the headless game with Q, switched from the page while the
title's theme plays: 25 ms frames through the switch hold their level
(RMS -20.0 to -21.2 dBFS either side), no gap.

Noticed, not changed: UBSan flags Nuked-OPN2's attack step (ym3438.c, `~level
<< eg_inc`, a left shift of a negative value) now that a route clocks the
chip (audio_switch under `route_probe_asan`); upstream code, well defined on
every compiler the game builds with.

### The drums re-voiced (build 110b)

The owner on 110a's files: the kick has far more low bass than the chip's, the
snare and the hi-hat are far quieter -- is the synth's decay too fast? He
likes sub bass in a kick, but it wants gated noise for its attack.

Measured with `tools/drum_hits.cpp` (`make drum_hits`): each drum struck
alone through the chip and through Q, the second hit of two so the chip's
DAC is settled as in a song (a first hit from silence carries a DC step),
level over 300 ms, the time to fall 20 dB, and four bands through a
rectangular window from the strike (a Hann window there hides every
attack; the first figures 110b took used one). By loudness (BS.1770) all
three drum tracks were alike; the difference was where the energy sat:

| 110a's synthesised drum | against the chip |
|---|---|
| snare | 5 dB quieter, 20 dB fall in 80 ms against 155, 10 dB short above 2 kHz |
| open hat | 20 dB fall in 95 ms against 175; its noise high-passed at 5 kHz, an octave above where the ROM's hats (played at ~13 kHz) have theirs |
| closed hat | the same band, 13 dB short at 2-6 kHz |
| the short hard drum | 4 dB louder, twice as long, 13 dB more below 200 Hz and nothing above 2 kHz -- the chip's is a 30 ms crack |
| toms | 40% shorter, pure sines with nothing above 2 kHz |

The drum voice gained a gated noise layer for the attack (band-passed
noise held at its level for a few milliseconds, cut over the last one), and
the table was fitted to the chip in three passes. After 110b, Q against the
chip (route_out/drum_hits/report.txt): every drum within 1 dB in level (the
closed hat 0.5 under), the snare's and hats' 20 dB falls within 5 ms, the
toms' within 10-25 ms, the bands within about 4 dB above 200 Hz. The short
hard drum is now a kick as the owner put it: its sub-bass body 1-1.5 dB above
the chip's below 200 Hz and 60 ms long, an 8 ms gated noise burst at 1 kHz
for the attack. The ROM samples' mode is unchanged: it matched the chip on
every drum already, bar the chip's crunch above 6 kHz.

The synthesised mode's trim, from `q_demo --trim-pass` again: the median
+2.0 dB, so 0.798 (110a's 0.760, set with the peakier kick); the loudest
song -6.8 dBFS. `make audio_check_q` now holds Q's title theme to the chip's
own peak, +-3 dB, and under -1 dBFS, in place of the chip's absolute window:
Q's first 34 s of the title peak 2.7 dB under the chip (later passages 2.7
over; the title is the song where Q strays furthest), 0.7 dB past the
window's floor, which was set in 109a for the chip. Listening:
`title_q_drums_synth_110b.flac` and `title_drums_alone_q_synth_110b.flac`,
beside 110a's.

## Build 111: the hooks, three sounds described, the PM voicing

The owner's answers to 110b's choices: (1) Q's voices "as hardcore as it can
without burning up the cpu and gpu" -- real phase modulation; (2) effects
hooked into the sequencer; (3) keep the re-voiced synthesised drums; (4) the
patch override file: low priority, not built; (5) parallel voices: yes. And
on the toms: a tom is usually a little higher at the strike and falls lower.

### The hooks

`SmpsSink.track_write` (smps.h): every YM2612 write a second time, by the
track that made it, with the track's own channel id -- including the writes
the chip never gets, because the sequencer runs a music track on silently
while an effect holds its channel and only drops its writes. The driver's
own writes (SilenceFM, the DAC switch) come as `SMPS_TRACK_DRIVER`; an effect
starting says so with `SMPS_HOOK_CLAIM` (no chip register) and the channel it
takes. They are called just before the chip write and change nothing the chip
gets: the routes, their files, the write hash, the headless WAV and the trial
are identical (docs/history.md, Build 111).

audio.c carries them to the audio thread as `CMD_TRACK` and keeps a shadow by
track, replayed on a synth switch after the chip's. The backend table takes
them through `track_write`, and renders the music and the effects at gains of
their own.

* **The chip:** an effect's key-on that reaches the chip marks its channel
  as the effects', a music track's as the music's; the mark goes through
  fm.c's write queue so it lands with its key-on. While EFFECTS and MUSIC
  differ, each channel's output is weighted clock by clock: Nuked puts out
  one channel a group of four clocks, the one `OPN2_ChOutput` latches when
  the cycle is a multiple of four (the channel counter, plus one before
  cycle 12) -- checked by keying each channel alone. Equal gains take the old
  code path.
* **Q:** a virtual channel a track -- the drums, FM1-FM5, the four effect
  tracks -- so an effect plays on voices of its own and the music under it
  plays on. A track's write to a channel not its own (the pan flag's always
  lands on FM1) goes to whichever voice plays on that channel; an effect
  starts from the chip's registers for the channel it claims, so what it
  never writes -- the pitch, for the effects that set FM3's special mode on
  FM4 or FM5 -- is what the chip would have.

Measured with `fm_probe --steal` (below): an effect over the battle music
($92), what the music loses under it against the effect's own energy. The
chip: +6.4 dB (Slasher), -6.4 (Whip), +0.3 (Deban) -- whole chord notes gone,
as loud as the effect. Q: -112, -121, -60 dB -- nothing.

(build 129b) That number mixes two losses; see "The steal split" below
(Build 129b's section at the end of this file).

### The AUDIO page

EFFECTS (OFF, -30 ... 0 dB) after MUSIC, `"effects"` in settings.json (a file
without it reads 0 dB), in the start-up line. MUSIC is now the music alone.
Route `audio_effects`: MUSIC OFF silences the title's theme, Gaj played over
it still sounds, EFFECTS OFF silences Gaj too, MUSIC back plays the theme.

### Three sounds described (read first: DoModEnv, the four envelope tables, cfF4, FinishTrkUpdate, SendFMFreq, SendFM3Freqs, cfFE, DoPitchSlide, the write routines, BuildingMusicPtrs)

`tools/fm_probe.cpp` (`make fm_probe`) renders one sound through the chip and
through Q in each voicing and measures the pitch every 10 ms, the partials at
chosen moments, the envelope correlation, and the spectra over time (third
octaves, each by its own loudest band: 0 dB the same shape). A strike is met
with a flat window or 20 ms of pre-roll. Instruments are read from the
sequencer's image (the extracted `FMInstruments.bin` was one off from 71 on;
tools/extract_tables.py fixed, `make extract-tables`; (build 136d) the bank
is `music/fm_instruments.json` now, `make extract-music`).

**The vibrato envelope.** Each tick while a note sounds the sequencer sends
the note's base frequency word plus (table byte x multiplier): an offset from
the base, not a running sum. At a new note only the multiplier (and the
stopped state) go back to 0; the table position runs on from wherever the last
note left it. `$84 n` adds n to the multiplier, `$85 i` jumps to byte i. Table
1 (15 songs' melodies) plays 43 bytes once, then loops a 14-tick cycle (4.3
Hz) whose $84 grows the multiplier by 1 each pass; table 2 (Power's melody)
loops 11 ticks (5.5 Hz), +2 a pass; 3 and 4 are the effects'. So the depth
grows the longer the note rings. The broken sign extension (`ori.w #$FFFF`
where `ext.w` belongs) makes every negative byte -1: table 1's cycle is
-1, +1, +1, +2, +2, +1, +1, 0, then -1 six times -- up +2m, down only -m, a
lopsided wobble. Positive bytes come out right only by luck: the routine
reuses a register whose top byte is the table's offset, and all four tables
sit below $100 in the driver. In F-number units, so shallower in cents high in
an octave. Step Up's melody on a 2 s note: the pitch swing grows from about
+-1 Hz to -13/+21 cents at 375 Hz, and Q follows it frame for frame (190 of
190 within 25 cents) -- it gets the same frequency writes.

**Data Memory's song** (building 2, Step Up, $8B). The melody's instrument
(72): algorithm 4, two pairs. Operator 2, a carrier at the written pitch, TL 0,
no decay -- the prime, sustained; operator 1 at half the pitch, maximal
feedback, TL 42 dB, lightly modulating it -- a hum an octave below and a
fifth above, 26 dB down. Operator 4, a carrier at 4 x, instant attack, decaying
to silence (40 dB in about 0.6 s), modulated by operator 3 at 15 x (TL 21.75
dB, index 2.05) -- partials at 4, 11, 19, 26 and 34 x the pitch, 1.5-13 kHz at
375 Hz: a sparse metallic strike that dies under the prime. Prime, hum and
fifth are a bell's partials; the strike is its clang; the wavering tail is the
vibrato envelope deepening while the note rings (held without it, only a +-2
cent shimmer from operator 1's feedback). The subtractive voicing: a comb of
4, 8, 12 x with no hum or fifth, 12.1 dB from the chip's spectrum.

**Battle effects.** Gaj ($C1, instrument 33: algorithm 4, feedback 6, both
modulators within 3 dB of full level), Enemy killed ($D4, 53: carriers at 15 x
under full-level modulators), Zan ($BC, 28: one modulator at 3 x on carriers at
0.5, 2 and 12 x), Sword ($B3, 19: algorithm 0, four in series at full level):
indices near 20 radians and feedback make dense clusters that change as the
modulators decay; plus slides of up to 124 units a tick. Two ROM behaviours:
the slide's octave fold upward never fires (it tests the word masked with $3FF,
never >= $4D1), so a long rising slide runs the F-number to its top and
carries into the block; and FM3's special mode is never switched on (nothing
writes $27), so its frequencies only offset FM3's own, and an effect that sets
the flag on FM4 or FM5 (the dam opening, Sword, Eijia, Gaj, the Pulser's and
the Terakite's attacks) never writes
its pitch there: it sounds at whatever the music's chord last left on that
channel. Chip against the subtractive voicing: 7-29 dB apart.

### The PM voicing (Q's default; PS2_Q_VOICE=subtractive for 110's)

Four operators, `q::sin` on `q::phase`, wired as Nuked's fm_algorithm table
(operators worked 1, 3, 2, 4: operator 3 hears operator 2's last sample in
algorithms 0-2); feedback from operator 1's two outputs a native sample and two
back, (o1 + o2) x 2^(FB-7) cycles; a modulator moves its carrier 4 cycles at
full level. The phase increment is OPN2_PhaseCalcIncrement's (F-number, block,
detune from the key code, multiple, the 17- and 20-bit wraps), then a
frequency. The envelope is the chip's generator: a step every three native
samples on a 12-bit timer, the rate by state with key scaling, the increment
from the timer's shift or the high-rate table, the attack's exponential, the
decays, the sustain level as Nuked keeps it; attenuation to gain 2^(-att/64),
interpolated between steps. Register writes act at once, as on the chip. Each
channel's carriers sum through the chip's 9-bit clamp, in slot order. Left
out: the 14-bit output, the 10-bit phase and log-sine table, the 9-bit DAC and
its ladder, the envelope's stairs; the LFO and SSG-EG (unused).

**Aliasing.** The operators run at 4 x 48 kHz (PS2_Q_OVERSAMPLE=1|2|4) and come
down through a Kaiser low-pass (129 taps at 4 x, cutoff 22.5 kHz, beta 8).
`fm_probe --alias`: of a held note's energy, what lies off the grid of half
its pitch -- the folded images. The bell patch at 1245 Hz: the chip -19.4 dB,
PM at 1 x -18.1, 2 x -34.5, 4 x -50.0.

**Against the chip** (fm_probe): eight music instruments held (64, 65, 66, 68,
72, 74, 82, 87) -- spectra 0.7-1.9 dB apart (74 11.2: its tail, to look at),
envelopes 0.95-1.00, pitch frame for frame; their level a constant -1.15 dB at
the chip's own scale (768/8192), so the voice gain 0.107. The bell: every
partial within 1-2 dB, the shimmer too. Effects: 3-8 dB apart (the subtractive
voicing 7-29). The bright ones come out 4-5 dB under the chip at 4 x: at 1 x
their level matches the chip's to 0.2 dB -- that much of the chip's loudness
there is images folded back. PS2_Q_OVERSAMPLE=1 has the chip's crunch.

**Cost** (`q_demo`, the title's theme, one core of a 2.1 GHz Xeon): PM 4 x 936
ns a sample, 2 x 560, 1 x 299; the subtractive voicing 394; Nuked-OPN2 1456.

**Parallel voices** (PS2_Q_PARALLEL=0 plays the chip's way): a strike on a
channel whose voice still sounds (a carrier within 60 dB) releases that voice
as a tail and starts the note on a fresh one from silence. `fm_probe --click`:
the note, released 50 ms, struck again; rendered with and without the first
note, one taken from the other, what is left is the tail -- its largest step in
the first 0.5 ms after the strike against its own steps before. The title's
chord patch (66): the chip +20.7 dB, PM the chip's way +23.0, parallel +2.3.
Instrument 67: chip +19.3, +14.9, parallel +9.3. The title's bass (64): chip
+18.3, +16.2, parallel -6.8.

**Trims** (`q_demo --trim-pass`, 23 songs, 60 s): at 1, every song's RMS
within about 1 dB of the chip's; median peak +1.26 dB with the synthesised
drums (0.865), -0.26 with the samples (1.031). `make audio_check_q` now holds
Q's loudest 400 ms to the chip's (+-3 dB): the chip's loudest sample in the
attract run is a 3 ms transient at 36.45 s (an instrument upload's TL 0 live
for a millisecond through the write queue under a sounding tail), 7 dB above
the music around it. PM +0.0 dB, subtractive -1.1.

### The toms

The chip's toms start about 25% above where they ring and never stop falling
(the high tom 258, 229, 217, 210, 209, 207, 204 Hz at 20 ms steps), with
membrane overtones near 1.7, 2.4, 2.9 and 4.7 x their pitch, 8-14 dB under it at
the strike and some 20 dB by 50 ms; 110b's held after 80 ms and were pure
sines. Now: a higher start (about 6% over the chip's first frame), the glide,
then a slow fall (70-100 Hz a second), and four overtones decaying ahead of the
body. From 40 ms the pitch follows the chip's within a few Hz; level within
0.2 dB; spectra 21 to 15 dB apart. In Secret Ways the rolls are hits every
100-150 ms, each cutting the last (the DAC plays one drum at a time), panned
left, both, right with the tom.

Listening (beside the tarball, FLAC): Data Memory's Step Up 45 s, the battle
music with eleven effects over it 24 s, Secret Ways 43 s (the tom rolls from
13 s) -- each through the chip and through Q's PM voicing.

### Build 111a: OVERSAMPLE on the AUDIO page

The owner, on 111's listening files: "fantastic"; and yes to a row for the
oversampling. OVERSAMPLE, after EFFECTS: 1X, 2X, 4X (the default) -- the rate
Q's PM voices run at against the output's. At 4X nothing folds back into the
audible band; at 1X the chip's images are there, its crunch on the bright
effects (Gaj, Enemy killed 4-5 dB louder, level with the chip). Left and right
step and stop at the ends, C steps on and goes round. `"oversample"` in
settings.json (1, 2 or 4; anything else, or a file without it, 4);
`PS2_Q_OVERSAMPLE=1|2|4` for one run whatever the file says; the start-up
line says it. It is Q's: while the chip plays the line under it says Q ONLY
and the chip sounds the same (route `audio_oversample`).

A change reaches every backend that has it (the table's `set_oversample`,
NULL for the chip) on the audio thread, playing or not, so a switch to Q later
finds it set. Mid-song, each sounding voice's phase steps are worked out
again at the new rate from the pitch it holds, and the low-pass starts empty:
switching 1X, 2X, 4X every 2 s through the title's theme, the largest 25 ms
level step at a switch is 0.55 dB (0.32 at the same moments with no switch);
nothing non-finite, nothing from ASan or UBSan. `make audio_check_q` names 4
(PS2_Q_OVERSAMPLE), as it names the synth, so a settings.json cannot move it.

### Build 111b: the level-up and the heal

The owner, on 111a in the game: the level-up sound "really weird", and the heal
at the hospital too (a Mate plays the same sound). Read first: SFX_LevelUp
(two tracks, FM4 and FM5, the same six-note arpeggio in unison, 4 ticks a
note, one with the vibrato envelope; instrument 1, algorithm 4, feedback 7,
releases of 1-7) and SFX_Healed (one track on FM5 in slide mode: a note
rising 28 units a tick for 10 ticks, then 8.25 dB quieter, five times;
instrument 35, algorithm 4, a full-level modulator at 6 x on a carrier at 8 x,
releases of 2-5). Where the game plays them: the level-up over the victory
music (Movement), the heal over the hospital's (Step Up) and over the level's
for a Mate.

Three things, measured with fm_probe (the level-up alone, 100 ms loudness
from the strike, and bands over its first half second):

1. **Parallel tails rang their whole release.** 111 let a note's tail finish
   its release on its own voice; these instruments release at about 8 dB a
   second, so each of the arpeggio's notes -- and each of the heal's five
   rises -- kept sounding under the next: the level-up 9-12 dB over the chip
   for seconds. The chip has one note a channel: a strike ends the last. Now a
   tail fades out over 15 ms (a raised cosine) and is freed: the click stays
   gone (the title's chord patch +1.9 dB, where the chip's way is +23), and
   nothing piles up.
2. **Q struck both unison tracks in the same instant.** The chip takes a
   register write every two native samples, so a tick's writes land spread
   out -- FM4's and FM5's strikes some 150 us apart, their copies partly
   cancelling. Q took a tick's writes at once: the two copies added whole, 3-6
   dB over the chip. Now Q keeps the chip's pace: each write waits for its
   slot in a queue as fm.c's does (a hook's write lands with the chip write it
   precedes; a drum byte takes no slot, as the DAC's). The level-up's bands
   now within 1-2 dB of the chip's (they were 4-13 over), its loudness within
   1-3 dB over time. Held notes unchanged (within 0.3 dB).
3. **The heal is mostly the chip's images.** A modulator at full level moving
   a carrier at 8 x spreads it far past any sample rate; the chip folds it all
   back. At 4X Q is 3 dB under the chip and 13.9 dB apart in spectrum; at 1X
   level with it (-28.3 against -28.4 dB), 9.6 apart. OVERSAMPLE 1X on the
   AUDIO page has the chip's version.

Also: with effects on voices of their own the victory music plays on under
the level-up (by 20 ms energy the chip's loses up to 8.5 dB there, Q's
nothing) -- the arpeggio over the music's own FM4 and FM5 parts, which the
chip drops. `PS2_Q_EFFECTS=take` (not on the page yet) has Q take the channel
as the chip does: the music's note there stops (faded as a tail) and its
writes are not voiced while the effect holds it; effects stay on voices of
their own for the EFFECTS volume.

The trims again (the pace moves the peaks): 0.804 with the synthesised drums,
0.947 with the samples; `make audio_check_q` -0.3 dB. `fm_probe --steal` now
compares energy, 20 ms at a time: with the chip's pace an effect's writes
delay the song's by a slot or two, as on the chip, and a waveform difference
counted that. Sanitizers clean over the new paths (the pace, the fade, taking
channels, 1X).

Listening: `victory_level_up_{chip,q_pm,q_pm_take_channels}_111b.flac` (the
level-up at 1.5 s and 4.5 s over Movement), `hospital_heal_{chip,q_pm,
q_pm_1x}_111b.flac` (the heal at 2 s and 6 s over Step Up).

### Build 111c: an effect's note ends when the music takes its channel back

The owner, on 111b's files: the level-up's tone still sustains. Measured over
the victory music (the level-up at 1.5 s; the mix with it against the music
alone, energy per 200 ms from its last note): on the chip nothing of it is
left from 1.9 s; on Q it rang 5.5 dB over the music there and faded over some
2 s. Read again: when an effect ends, RestoreBGMChn uploads the music track's
instrument to the channel -- on the chip that lands under the effect's
releasing note, which takes the music's levels and rates from then (at the
victory, instrument 0's: silent at once); and the music's next strike there
ends it outright, one note a channel. Q's effect voice, a voice of its own,
heard neither. Now, while an effect's note is what sounds on a chip channel,
the music's writes that reach the chip there go to the effect's voice too,
and a music strike there fades that note as a tail (15 ms). After it: nothing
of the level-up from 1.9 s over the victory music, as on the chip; the heal
over Step Up gone by 2.3 s on both (it was already short). Alone, with no music
behind it, the level-up still rings some 2 s on both (-33 dB at 2 s on the
chip, -31 on Q): its instrument's release, the ROM's.

Only q_synth.cpp: songs have no effects, so the trims and `make audio_check_q`
(-0.3 dB) are unchanged; ASan and UBSan clean over the level-up, the heal and
Gaj over their music, each voicing, taking channels or not. Listening:
`victory_level_up_q_pm_111c.flac`, `hospital_heal_q_pm_111c.flac` (the chip's
are 111b's, unchanged).

## Build 112: VOICES, TAILS, the patch file, and the chip's grain

The owner's answers to 111c's three questions: keep effects on voices of their
own, with a row for limited voices; he loves the ring (111a's tails); yes to
the patch file.

### The AUDIO page's three rows

After OVERSAMPLE, each Q's (Q ONLY under it while the chip plays), each two
values that left, right and C flip; settings.json `"voices"`, `"tails"`,
`"patches"` (a file from before 112 reads the defaults); for one run
`PS2_Q_EFFECTS=take`, `PS2_Q_TAILS=fade|ring`, `PS2_Q_PATCHES=0|1`. The
start-up line says all three and how many patches the file has.

* **VOICES** -- EXTRA (the default): an effect on voices of its own, the music
  plays on under it. LIMITED: the effect takes the music's channel as on the
  chip -- the music's note there fades (15 ms) and its writes are not voiced
  while the effect holds the channel; effects stay on their own voices for
  the EFFECTS volume. LIMITED limits which track sounds on each of the six
  channels, not how many voices Q renders: a strike that restarts an envelope
  under a sounding note -- the claim, the effect's own notes, the music
  taking its channel back -- still gets a fresh voice with the old note as a
  tail on its own (The owner asked; only PS2_Q_PARALLEL=0 plays one voice a
  channel, with the chip's click). Measured with `fm_probe --music-only` (Q's
  effects at 0, the music alone under them): Gaj over the battle music,
  LIMITED against EXTRA, the music down up to 5.5 dB in 13 of 152 20 ms
  windows while Gaj holds its channels, as the chip drops it; the largest
  sample step the same (+4.9 dB over the 99.9th percentile of steps), so no
  click at the claim. The level-up over the victory music: nothing to take
  (Movement's FM4 part is two bytes; it has no FM5). The heal over Step Up:
  within 0.5 dB.
* **TAILS** -- FADE (the default): a note struck again ends the last in 15 ms,
  as the chip's one note a channel (111b's). RING: the last rings out its own
  release on its own voice (111a's). At most 12 ring at once; the next fades
  the quietest of them, so a fresh voice is never taken from under a sounding
  one (with the channels' own ten, the pool of 24 keeps two for fades).
* **PATCHES** -- ON (the default): the patch file's instruments; OFF: the
  ROM's alone. The chip always plays the ROM's.

### The patch file

`assets/music/q_patches.json`, read once at start-up (preloaded like every
asset: restart after an edit). An entry names an instrument by the bank's
number -- the byte after cfEF_SetFMIns in a song -- and says only what it
changes; a field left out keeps the ROM's value. Every field and its range is
in src/audio/patches.h and the file's own "about". The chip's parameters by
friendly names (algorithm, feedback; per operator 1-4 multiple, detune -3..+3,
level, key_scale, attack, decay, sustain_rate, sustain_level, release), and
what only Q can do: `fine_cents` per operator, `volume_db`, and `tail`
("row", "fade", "ring"). `tools/q_patch_template.py N` writes instrument N
whole, to start from; `make patches_check` tests the reader.

How it reaches Q: the sequencer's hooks now say which instrument a track
uploads (`SMPS_HOOK_INSTRUMENT`, before SetFMIns's 26 writes; the chip never
sees it, and every song's chip writes are unchanged). While a track's
instrument has a patch, Q takes its own-channel writes through it, by rules
that do not count where in the upload a write falls, so a synth switch's
replay comes out the same:
* an instrument register (each operator's $30, $50, $60, $70, $80, and $B0)
  takes the patch's value -- but SetMaxRelRate's $FF into $80 (an effect
  silencing the channel it takes) stands;
* a level: a write to one of the ROM instrument's carriers is RefreshVolume's
  instrument level plus the song's volume, so the volume is what it wrote
  less the ROM's level, and every level is then the patch's with that volume
  on the patch's own carriers -- a patch that changes the algorithm still
  follows the song's volume and fades;
* pan, pitch and keys as written.

The shipped file: **instrument 70 set to ring** -- Advanced's chord track moves
from 82 to 70 part-way (its `$EF $46`), and 70's operator 2 is a carrier at 9
x the note with no first decay and a release rate of 0: the drone the owner
loves. On the chip that operator never lets go, so each chord strike carries
the drone on; FADE cut it and started it from silence. Advanced's 30-40 s
(the only window of its first minute with 70) in the drone's band, 1.5-5 kHz:
FADE 1.3 dB under the chip, the file's ring 0.5 under. Everything else in the
song unchanged. And instrument 74 written out whole, `"enabled": false`, as
an example. RING costs Q roughly a tenth more time on Advanced (fm_probe's
timings, which include its analysis).

### Step Up's chord instrument (74) is the chip's grain

111's 11.2 dB (the rest 0.7-1.9): instrument 74 -- algorithm 0, four
operators in series, operator 3 attacking at rate 1 into a carrier at 2 x
whose sustain level is the bottom. `fm_probe --note 74`: at 0.3 s the chip
has the carrier's odd multiples (6, 10, 14 ... 38 x the pitch) 30-45 dB under
it, Q none; at 1.0 s the chip's note sits at -55 dB and Q's at -75. Read in
Nuked-OPN2: each carrier enters the channel's 9-bit sum truncated (`fm_out
>> 5`), and OPN2_ChOutput's YM2612 DAC puts the channel out on one cycle of
four with 1 added when it is not negative, and its sign (+1 or -1) on the
other three -- +4 steps above zero, -3 below, a jump of 7 at the crossing. A
quiet note becomes a square wave at that jump's size, which no longer decays:
the chip's tail.

`PS2_Q_GRAIN=1` puts both into Q's channels (the +4 of silence taken off, so
a silent channel is 0). 74: first 300 ms 4.6 dB apart becomes 0.5; the odd
partials at 0.3 s within about 1 dB of the chip's each; the whole note 11.7
becomes 6.7 (what is left is the deepest tail, the envelope below the last
bit). 72: 0.9 to 0.4; 66: 3.0 to 2.0. So the gap is the grain the PM voicing
leaves out on purpose, not a fault in it. A diagnostic for now; a GRAIN row
on the AUDIO page if wanted. (Build 116: the GRAIN row.)

Listening (beside the tarball, FLAC): the level-up twice over the victory
music, chip and Q; Advanced 25-55 s through the chip, Q with FADE, and Q with
the patch file's ring; Gaj three times over the battle music, chip, Q EXTRA, Q
LIMITED; Step Up 30 s through the chip and Q with the grain on.

## Build 116: the AUDIO page two rows apart, GRAIN, and instrument 70's ring off

The owner's answers 1 and 2 (handover_113_windows.txt): instrument 70's ring off
by default in the shipped patch file, and a GRAIN row on the AUDIO page.

### Instrument 70's ring off

`assets/music/q_patches.json` keeps 70's entry, `"enabled": false`: set it
true to hear Advanced's drone ring again. The shipped file now changes
nothing (the start-up line: `patches on (0 in the file)`), so PATCHES ON and
OFF sound the same until an entry is enabled. Advanced 25-55 s through Q
(`fm_probe --song advanced --from 25 --seconds 30 --patches
./assets/music/q_patches.json`): with the new file, byte for byte Q with no
patch file (RMS -23.8 dB; the chip -22.9); with 115's, the ring (-23.5).

### The page's layout

At three cells a row -- the value, why it cannot be had under it, a line of
fill -- the page's eight rows took 27 of the screen's 28, so a ninth needed
room first. Its rows are two apart now, as the Escape menu's own (the lamp
steps 16 pixels, as the game's window cursor does), with Q ONLY or NOT BUILT
in a column beside the value (AUDIO_WHY_COL) rather than under it: nine rows
in 22. The CRT and HDR pages keep three apart, their reasons being long
(NEEDS FULLSCREEN).

### GRAIN

After PATCHES: OFF (the default) or ON, Q ONLY beside it while the chip
plays; two values that left, right and C flip; settings.json `"grain"`
(true or false; a file from before 116 reads off); `PS2_Q_GRAIN=0|1` for one
run, as the other Q switches' variables, and the start-up line ends `grain
on` or `off`. ON is build 112's diagnostic (Step Up's chord instrument, above):
each carrier into the channel's sum cut to the chip's 9-bit step, and the
YM2612 DAC's jump at zero, on Q's PM voices -- what the chip's quiet tails are
made of. It reaches Q as the fourth SynthOption (synth.h's SYNTH_OPT_GRAIN);
Q still reads PS2_Q_GRAIN at creation, for the tools (fm_probe, q_demo).
Route `audio_grain` (fails on 115's probe, which has no such row).

Measured on the Q build: the attract sequence (2400 frames) with GRAIN off is
build 115's byte for byte; with `PS2_Q_GRAIN=1`, and with only
`"grain": true` in settings.json, the two WAVs are identical to each other
and not to off.

## Build 129b: the steal split

### The steal split

On the Mega Drive an effect plays on one of the music's FM channels: it
*steals* the channel, and the music's part there is silent until the
effect lets go. `--steal` measured the together against the song alone
plus the effect alone, which mixes two losses. It now also splits the
together as the AUDIO page does -- rendered again with EFFECTS at 0 (its
music) and with MUSIC at 0 (its effect) -- and measures each against
itself alone: the music's loss (20 ms windows, mean / worst), the effect's
cut (where it starts and ends in the mix and alone, the energy it keeps,
and while it still plays, 20 ms mean / worst), and whether the two parts
add back up to the together, sample by sample. The chip in fm_probe is now
the game's own chip backend (synth.c), so `--chip limited|extra|both`
measures the one chip (LIMITED, the default, as fm_probe always had it) and
the second chip for the effects (EXTRA, build 124); `--music-only` reaches
the chip too. Every other case renders as before (Q's proof set identical).

`./fm_probe --steal ID --over 92 --chip both`, over the battle music; the
old line first, unchanged, then the split:

| effect | chip (LIMITED) | chip EXTRA | Q PM |
|---|---|---|---|
| Slasher ($B5) | music -0.2 / -2.0 dB; ends 660 of 800 ms, keeps -2.2 dB | music 0.0; ends 420 of 800, keeps 0.0 | music 0.0; ends 360 of 400, keeps 0.0 |
| Whip ($C6) | music -0.4 / -6.1; ends 640 of 980, keeps -0.3 | music 0.0; ends 360 of 980, keeps -0.3 | music 0.0; ends 320 of 600, keeps -0.3 |
| Deban ($C2) | music -0.4 / -6.1; ends 660 of 1300, keeps -0.3 | music 0.0; ends 560 of 1300, keeps -0.3 | music 0.0; ends 520 of 860, keeps -0.3 |

Read: EXTRA gives the chip what Q has -- the music loses nothing under an
effect. What every path loses is the effect's tail: when the effect's
track ends the music takes its channel back (RestoreBGMChn's upload lands
under the releasing note, and the music's next strike ends it), so a tail
that rings to 800-1300 ms alone stops at 320-660 ms in the mix. The tail is
quiet: the energy kept is within 0.3 dB but for Slasher on the one chip.
While the effect still plays, the one chip alters it most (Whip -19.7 dB
mean over its windows), the music's upload landing on the operators it is
sounding. The parts add up on every path. Not traced: Whip and Deban keep
0.3 dB less energy on all four paths alike, so it comes from the
sequencer's side, not a synth's.

## Build 132: the pause keys off the parts lent to an effect

The pause flag (cfE8_MusPause) turns down the music parts in use and not
lent to an effect, as the ROM's; a lent part is silent on the Mega Drive.
VOICES EXTRA plays a lent part on voices of its own (the music chip, Q), so a
note struck just before the pause rang on through it (Shure's chord pad on
FM4, -35 dBFS 90 ticks in). The port's pause now first keys off every lent
music part, on the hooks alone: the chip's writes are unchanged
(docs/history.md, Build 132; route pause_tail).

### Build 132b: SetMaxRelRate's drift

The ROM's SetMaxRelRate writes the four release rates only on FM1 and FM4;
on FM5 and FM3 its register walks off (WriteFMIorII adds the channel's offset
on every pass) and ends in FM4 op1's and FM1 op3's SSG-EG registers, which
nothing else writes. Ported as the ROM has it: on the chip, FM4's and FM1's
parts keep that SSG-EG from the first effect on FM5 or FM3 for the session.
Q does not voice it (docs/history.md, Build 132b).

## The song text (build 137)

The owner, after build 136: "hex bytes for notes is not human accessible".
Every song and effect is text now, with the driver's tables beside them,
built back into the driver's image byte for byte (`tools/song_text.py`;
`make extract-music` writes them). Build 137 wrote and proved the text;
(137a) the game builds its image from it (`src/audio/songtext.[ch]`) and
`SoundData.bin`, the last container, is gone.

**The files**, all under `assets/music/`:

- `sounds.txt`: one line a sound id, $81 to $D8 -- the id, the song or
  effect it plays (`none`, an empty sound) and its priority
  (SoundPriorities). Then `song_file` lines, the files under songs/ to
  read (a file not named is not read: the game has no directory listing),
  how the instruments are stored (`same`, `share`: below), the driver's code (`driver_code 4672`, zeros, where the 68k code
  sat between the effects and the frequency table) and `streams`, the order
  the songs' blocks and the bytes no stream reaches sit in after the
  frequency table.
- `envelopes.txt`: the four vibrato envelopes (ModEnvelopePtrs) by name --
  `vibrato` (15 songs' melodies), `power_vibrato` (Power's), `scoop` and
  `short_scoop` (named by no header and no stream). Steps as signed numbers,
  `depth +N` ($84), `jump LABEL` ($85), `restart` ($80), `hold` ($81),
  `stop` ($83).
- `effects.txt`: every effect in the order the ROM stores them, each an
  `effect` line, its tracks' headers, then its streams.
- `songs/<name>.txt`: a song's `song` line (tempo, PAL tempo, PAL tick
  multiplier), its tracks' headers, then its streams in `block`s. Songs that
  share a passage are one file: `pleasure_and_exciting_town`,
  `advanced_and_mystery`, `rise_or_fall_and_the_place_of_death` (Pause and
  Unpause share theirs inside effects.txt).

**A track header** is a line: `track fm1  phantasy_fm1  tick 2  transpose
-12  mod vibrato  instrument selection  volume 20` -- the channel (`drums`
for the DAC, `fm1`-`fm6`), the label its stream starts at, then the nine
header bytes by name: the tick multiplier, the transpose, the envelope
(`off` for none), the instrument (a song's header always names 0,
`selection`, replaced before its first note) and the volume (an
attenuation added to the carriers' total level: higher is quieter). The
flags byte's mode bits follow as words: `pitch_slide` ($20) and `raw_freq`
($08) start the track in those modes, as 59 and 8 effect tracks do;
`flag_6` is the bit 6 two headers set and nothing reads. `unused_track` is
a header after the counted ones -- Slasher's second, which the ROM's count
of 1 leaves unread.

**A stream** is labels (`name:`, on a line of their own or before a
length), events and `bytes`:

| Text | Bytes | What the sequencer does |
|---|---|---|
| `C#5 12` | note, length | strike the note for 12 ticks (times the tick multiplier) |
| `C#5` | note | strike it for the last length |
| `rest 12` | $80, length | silence for 12 (the hold bit set, the note off) |
| `again 12` | length | the last note struck again for 12 (a drum track: the last drum) |
| `C5+E5+G5 12` | 3 notes, length | chord mode (`chord 3`): one note a part, each with its own part's transpose |
| `tie 24` | $E6, length | the note held on 24 more, not struck again (cfE6 then a length) |
| `hold` | $E6 | before a note: does nothing to that note, but (build 139) when it is the very next byte after a note, DoNoteStop sees it and does not let that note go 3 ticks early -- it sounds up to the next one (29 places in the songs -- build 139 counted 32, 139b counted again; since 139b the disassembler writes "holds the note before up to the next" beside it, "does nothing" beside the 17 others) |
| `release 48` | $E7, length | the note let go now, silence for 48 (cfE7) |
| `C5 12 slide +3` | note, a skipped byte, speed, length | pitch-slide mode: the frequency moves 3 F-number units a tick; `unused N` gives the skipped byte when it is not 0 (it often is not: 44 slides carry one) |
| `freq $2060 12` | word, length | raw-frequency mode: the chip's frequency word (plus `slide N`) |
| `kick 6` | drum, length | a drum track: drums.json's names |

The state words, one a coordination flag: `instrument NAME` ($EF, the
names in fm_instruments.json), `volume +4` ($EC, added) and `volume 20`
($F0, set), `transpose +12` ($FB, added), `mod NAME` / `mod off` ($F4),
`pan left|right|center|none` ($FF), `pan_anim sweep 1` ($E3: `off`,
`sweep`, `cycle`, `left_first`, `right_first`, then a timeout), `chord 3` /
`chord off` ($E5), `tick 2` ($FA), `tempo 16` ($E4), `pitch_slide on|off`
($FC), `raw_freq on|off` ($FD), `legato on|off` ($EE: the hold bit itself,
notes change without being struck), `fm3_special a b c d` ($FE), `music
stop` / `music restart` ($E0, as the Hidapipe's effect uses them), `sound
NAME` ($E2, queue a sound), `pause on|off` ($E8). The flow: `goto L`
($F6), `loop L 4 times` ($F7: from L, four times in all; `counter N` when
not counter 0), `call L` ($F8), `return` ($F9), `end` ($F2), `end2` ($E1,
the same). And the ones no song uses, for completeness: `comm`, `fms`,
`ams`, `noise`, `psg_instrument`, `nop`, `unused_ed`.

**Notes** are concert names from note_frequencies.json: byte $81 is entry
1, B0, a semitone a byte; past entry 63 (C#6) the names go on a semitone a
step to $DF. What plays is GetFMFreq's: the byte plus the track's
transpose, six bits kept. So a `# heard:` comment follows each line on
which a note is heard as other than it is written -- a transpose, or a
byte past $BF wrapping round (Zan's `E6 21 slide +48` is heard as C1).
Where a note is reached at more than one transpose (a passage called or
looped at two) the comment gives each, `G#2|A2|...`. `python3
tools/song_text.py annotate` rewrites them after an edit, walking every
track as the sequencer does; they are never read back.

**Read as the sequencer reads.** The disassembler walks every counted
track of every song and effect as ProcFMTrack and its three variants do:
flag by flag; raw-frequency mode chosen by the mode when the read began
(ProcFMTrack_Raw never looks at it again), pitch slide by the mode after
the flags; chord mode's extra note bytes; loops with their counters, calls
with their returns. Every byte reached has one meaning, whichever way it is
reached. Ten loops land on a note's length byte (Restoration's bass,
Mystery, Violation, Under, Power): `F#3 restoration_fm2_2: 6` -- the label
before the length, and a comment that each time round the loop reads it as
`again 6`. 61 bytes no stream reaches are `bytes`: 48 after the frequency
table (three subroutines' worth of lengths and returns), 8 in the middle of
the streams, Zan's 4, Slasher's unread header, and five padding bytes.

**Placing.** A jump finds its label wherever it sits, so the order only
keeps the image the ROM's: the tables, the song headers in id order (the
empty song after them), FMInsPtrs and the instruments, the effects in
effects.txt's order (the empty effect after them), the driver's code,
the 85 frequency words, then the blocks in sounds.txt's order. A block is
a run of one file's streams that ends in a `goto`, `end` or `return`.
The envelopes must end by $100, where the PAL tables are read by address.
The instruments are stored in number order, whole, except as `same`
(unused_56 to unused_63 and bass are one instrument stored once) and
`share` (phantasy_chords_2 starts in phantasy_high's last four bytes;
rise_or_fall_chords in power_chords') say, each kept only while the bytes
agree.

**Proof.** `make extract-music` writes the text only after building it
back into the image and finding it the same, byte for byte (23593 of
23593); `make roundtrip` packs it again at level 2 ("songs and effects").

### Build 137a: the game builds its image from the text

`smps_load(SmpsData *, music_dir)` reads `note_frequencies.json`, then
`songtext_build` (src/audio/songtext.[ch]) reads the instruments
(`instruments_load_named`, fm_instruments.json with their names), the
note names (note_frequencies.json's `"note"`), the drum names (drums.json),
sounds.txt, envelopes.txt, effects.txt and each `song_file`, and lays the
image out as `assemble` does. It refuses, naming the file and line, a word
it does not know, a name nothing defines (note, drum, instrument, envelope,
sound, label), a number out of range (a length past 127, a step that is a
code's byte, a slide speed past +-127, a `freq` whose high byte is a
flag's), a label, block, song or effect given twice, a block not placed,
ids not $81 on in order or stopping before $D8, the envelopes past $100,
the image past 64 KB. So an edit that breaks the text stops the game at
startup with the line to fix, as the other loaders do; one that reads but
makes no musical sense (a chord of the wrong size for its mode, a slide
note outside pitch-slide mode) plays as the driver would play those bytes.

The callers that read SoundData.bin take the music directory now: audio.c,
patches_check, smps_trace (`--assets` names the music directory), q_demo,
fm_probe; q_patch_template.py builds the image with song_text.py
(`--music`). fm_instruments.json is in the manifest (the game reads it) and
`.bin` is off make_manifest's and roundtrip.py's skip lists.

`instruments_load` checks the names (137b's part, done here): one word --
letters, digits, `_`, at most 31 -- no two the same, on every instrument
or none; `instruments_load_named` hands them out. The song text needs them
all.

## The tracker format (build 139)

The owner, after build 138e: the stream text reads one part at a time; laid out
like an old tracker (FastTracker II), the parts can be read against each
other and edited by position. `tools/tracker.py` writes every song that way
and reads it back. Build 139 is the format, the writer, a reader in Python
and the proof that the two play the same; the game does not read the files
yet (that is the C reader, next), so they live outside `assets/`. (Build
139b: the game reads them, `assets/music/songs/<song>.trk`, which `make
extract-music` writes; "Build 139b" below.)

    python3 tools/tracker.py export [--out DIR] [--full] [SONG ...]
                                     # songs to DIR/<song>.trk (build/tracker)
    python3 tools/tracker.py streams FILE.trk   # the stream text it reads as
    make smps_trace && python3 tools/tracker.py prove
    python3 tools/tracker.py image OUT   # (139b) the image the game builds

### What the driver does with time (read first: SoundDriverInput, DoTempo, PlayMusic, UpdateFMTrack, UpdateDrumTrack, the three ProcFMTrack variants, TickMultiplier, FinishTrkUpdate, DoNoteStop, DoNoteOn_A/DoNoteOff_A, GetFMFreq, cfHandler and every flag)

A tick is one update of a track, once a 60 Hz frame (SoundDriverInput from
the vertical blank). A note's length is its written length times the
track's tick multiplier (TickMultiplier), counted down one a tick; a note
with no length byte keeps the last multiplied length (FinishTrkUpdate copies
it). The tempo steals frames: every `tempo`-th frame DoTempo adds 1 to every
music track's count, so that frame's update leaves the notes where they were
(tempo 0 steals none). Phantasy at tempo 6 counts 50 ticks a second -- a
beat of 24 ticks is FastTracker's 125 BPM -- and its fm1's `tempo 16` (the one
tempo change in the songs) makes that 56.25. The vibrato, the pan
animations and the fades move every frame, stolen ones too. In PAL the
update runs twice every fifth frame and PlayMusic takes the PAL tempo and
gives every track the PAL tick multiplier; the port plays NTSC.

How a note ends. When a note has 3 ticks left, DoNoteStop keys it off --
unless the byte stored right after it is a hold ($E6). So a note before a
`tie` sounds through; any other note is let go 3 ticks before the next event.
That byte is the next byte in memory, not the next one played: a `hold`
right after a note (29 places; 32 was a miscount, 139b) keeps it sounding up to the next note, and a
volume change, an instrument, a `call`, `return`, `goto` or `loop` between a
note and its `tie` (54 places) lets the note go anyway -- the tie then holds
silence. A `tie` (cfE6 then a length) sets the hold bit: not struck, not keyed
off. A `rest` and a `release` (cfE7 then a length) make the same chip writes
(a key-off, then the frequency each tick, no key-on); so do `again` and
striking the note it repeats, when no slide is on (no song slides).

Chords. `chord 3` (cfE5) makes tracks 4 and 5 shadow FM3: each note is three
note bytes, each part read with its own track's transpose. cfE5 copies the
lead's transpose, vibrato and volume to the parts when it is read; cfEF (an
instrument) copies the instrument and volume to them and loads it on each;
a volume change on its own changes FM3 alone (20 places in the songs). So
the order of the changes before a note matters to the chip writes in a few
cells (a volume before or after an instrument or a chord, two instruments in
a row): the tracker keeps them in the order the driver read them.

What the songs use. Only Phantasy changes tempo or transposes mid-song; no
song changes its tick multiplier mid-song, slides, uses raw frequencies or
legato; drum tracks carry no changes at all. Every song's parts repeat after
the same length, but for Over's drums (a twelfth of it) and Movement Ver2's
(half); Movement ends. Where each part goes back to differs (Mystery's fm2 at
tick 780, its others at 12 and 72).

### The file

`<song>.trk`, one a song (the three pair files of the stream text become six
songs; the passage they share is written in each):

    song phantasy  tempo 6  pal_tempo 6  pal_tick 2
    channel drums  tick 2  mod off  instrument selection  volume 20
    channel fm1    tick 2  mod vibrato  instrument selection  volume 20
    ...
    section 00  8 rows x 12 ticks    # ticks 0-95
    row | drums      | fm1                         | fm2                 | fm3
     00 | closed_hat | C#5 instrument phantasy_mid | G#2 instrument bass | C-4 G#3 D#4 chord 3 instrument phantasy_chords pan_anim sweep 1
     01 | ...        | C-5                         | ...                 | ...

- **The `song` line**: the tempo, the PAL tempo and PAL tick multiplier, as
  the stream text's. The file's comment header gives the ticks a second and
  the BPM for a beat of 24 ticks.
- **`channel` lines**: a part and how it starts -- the stream text's `track`
  line without the label and the transpose (the notes are written as heard).
  `tick N` stays: a length is written in ticks, so it says what lengths the
  part can take (a multiple of N, up to 127 N) and keeps the bytes the ROM's.
- **Sections**: the song cut into spans of one length (96 to 384 ticks, the
  one that writes least), every channel side by side, `N rows x T ticks`. The
  row is the song's usual one (most often 6 or 12 ticks), finer in a section
  that needs it (the 4-tick arpeggio under Phantasy's melody); a cell that
  falls between rows says `@N`, N ticks after its row. Sections are numbered
  from 00 in order; the `# ticks` comment and the order table at the top are
  written by the exporter and never read.
- **`fm1 = 03`** heads a column that plays section 03's fm1 again: its cells
  are left blank, so a repeated part is written once and edited once (Over's
  drums, Phantasy's 20 bars of hi-hat). `export --full` writes them out
  anyway for reading; the reader then requires them to match their section,
  naming the line that does not.
- **A cell**: the notes struck, as heard (`C#5`, `C-5`; B0 to C#6, which is
  what six bits of note and transpose can play), one a part in a chord, lead
  first; a drum by drums.json's name; `===` silence (a rest or a release);
  `tie` the note held on; `...` nothing new. Then `@N`, then `hold` or `cut`,
  then the changes in the order the driver makes them, in the stream text's
  words: `instrument NAME`, `volume N` (the driver's value -- an attenuation,
  higher is quieter; `volume +N` adds), `mod`, `pan_anim`, `chord N|off`,
  `tempo`, `sound`, and the rest. A note lasts until the next cell in its
  column.
- **`hold` / `cut`**: by default a note before a `tie` is held to it and any
  other is let go 3 ticks early. `hold` on a note keeps it sounding up to the
  next one anyway; `cut` lets it go before a `tie`. A `tie` with changes in it
  cannot hold the note before (the driver reads the changes between them),
  so the reader asks for `cut` there.
- **`goto SS.RR`** (`@N` if between rows) in a cell of its own: where the
  channel ends and the section and row it goes back to. `end` where it stops
  (Movement), after any changes it makes as it stops (`sound NAME end`).

The channels are written out until the last of them comes round: a part
that repeats sooner (Over's drums) is written out again, and each channel
goes back to the latest place it can, so the ends line up within a note.

### The reader and the proof

`read_tracker` reads a file, refusing with the file and line what it cannot
take: a word it does not know, a name nothing defines, a note past C#6, a
cell's length that is not a multiple of the channel's tick or is past 127 of
them, a chord of the wrong size for the `chord` in force, `chord 1`, a
transpose, a slide, a `loop`, `call` or `return` in a cell, a `= NN` column
that differs from its section or is a different length, two cells in a row,
a `goto` to a place where the channel has no cell, a cell after the end, a
`tie` with changes after a note not marked `cut`. `stream_text` writes the
song as stream text, one block a channel with every repeat written out
(`again` where the note is the one struck last, lengths left out where the
driver keeps them, a bare `hold` after a note marked `hold`, a `nop` between a
note marked `cut` and its plain `tie`); `song_text.assemble` builds it.

`prove` exports every song, reads the exports back into a scratch music
directory (sounds.txt's song files and streams replaced) and runs
`smps_trace --regs` on both: every song's chip writes equal, frame for
frame, for its written span and two more turns of its longest loop -- 23 of
23, 3,150 to 28,920 frames, 674 to 82,393 writes a song. The image the
trackers build is 31,836 bytes against the stream text's 23,593: no calls
or loops, everything written out, still half the driver's 64 KB.

## Build 139b: the game reads the tracker files

(Made again in build 168 from docs/history.md's Build 139b: none of 139b's
code had reached the tree. The names below are 139b's.)

`make extract-music` writes every song's tracker file,
`assets/music/songs/<song>.trk`, beside its song file, exported
(`tracker.write_song`) from the stream text it has just written and
checked against the ROM's image; a `.trk` for a song that is gone is
deleted. The game reads a song from its `.trk` where there is one, and from
its song file's stream text where not. So a song is edited in its `.trk`;
an edit to its `.txt` plays only for a song with no `.trk`, or as a mod.

**Which file.** First found: a `.trk` in the mods folder, the song file in
the mods folder, the game's `.trk`, the game's song file (docs/mods.md, "A
song's two files"). The second is there so a stream text mod made before
139b is not hidden behind the game's new `.trk`. songtext.c asks
`songtext_set_mod_test`'s function whether a path is a mod's; audio.c sets
it to assetfs's `assetfs_is_mod`. The tools set none, so smps_trace and the
rest read a song's `.trk` wherever there is one.

**The layout.** songtext_build reads sounds.txt and envelopes.txt, then
finds each song file's `song` and `block` names by their lines' first two
words (`scan_songs`), and which songs have a tracker file to read. Then:

- the tracker files, in song order (the song files in sounds.txt's order,
  the songs in each file's), each refused if its `song` line names another
  song;
- effects.txt;
- each song file that still has a song read from it, less each replaced
  song's `song` line and the `track` lines after it. A song file all of
  whose songs come from tracker files is not read at all.

The image is laid out as before, except that a block of a song file not
read is left out of sounds.txt's streams (unless a file still read has a
block of that name), the `bytes` lines (the ROM's bytes no stream reaches,
and padding) are kept only while any song file is read, and the tracker
songs' blocks go after all the streams, in song order, then channel order.
A tracker song's block and its track's start are `trk_<song>_<channel>`
(tracker.py's `PREFIX`); its loop's place is `trk_<song>_<channel>_loop`.
The other song of a pair file, still read from its stream text, keeps its
own labels.

**The bytes.** songtext.c's "Tracker files" section is tracker.py's
read_tracker, channel_events and channel_stream in C. It writes straight
into the image the bytes song_text.assemble makes of the stream text
tracker.py's stream_text writes for the song:

- every repeat written out, with no calls or loops;
- `again` and a length where the note or drum is the one struck last (not
  right after a bare hold, not at the loop's cell);
- a length left out where it is the last one, which the driver keeps (but
  not before an `again`, whose length byte would be read as this one's, and
  not at the loop's cell, which two places reach);
- a bare `hold` ($E6) after a note marked `hold`, and after a channel's
  last note when its loop goes back to a `tie` (the byte after that note is
  the `goto`'s otherwise, and DoNoteStop would let it go);
- a `nop` ($F1) between a note marked `cut` and its plain `tie`;
- the end's changes, then `goto` the loop's cell ($F6) or `end` ($F2).

A cell's changes are read with the stream text's words and stored as
stream_text writes them. `tie N` among a cell's changes is a bare `hold`,
`release N` a bare `release`, and `sound N` the first id with N's name.

The refusals are tracker.py's, by file and line, in its words. Each
tracker file is read and its blocks made before the next one is read, so
of two broken files the same one is refused first. A refused tracker mod is
handled like any refused music mod: the refusal, every mod under `music/`
set aside, the game's own music. With the game's own `.trk` broken, the
music stops, with the refusal said.

**Sizes.** With every song from its tracker the image is 31,836 bytes; with
none, 23,593 (the ROM's); with one song from its tracker, 23,624
(movement) to 25,390 (pleasure). The largest mix, every song but
exciting_town, mystery and the_place_of_death, is 33,234: the
pleasure_and_exciting_town and the_place_of_death files are still read with
all their blocks, beside nearly every tracker block. A song file whose songs
are all trackers is dropped, so no mix comes near the driver's 64 KB.

**The Python twin.** tracker.py's `game_music(music_dir, out_dir)` writes
the same choice out as a music directory of stream text:

- the kept song files, less the replaced songs' header lines;
- `songs/trk_<song>.txt` for each tracker song;
- sounds.txt's song files and streams to match.

`music_from_trackers`, prove's, is game_music with every song required;
`tracker.py image OUT` assembles it, and `smps_trace --image FILE` writes
the image the C builds. `song_span` (a song's written span and two more
turns of its longest loop; song_frames uses it) and `timeline` /
`timeline_difference` describe what a song plays as the tracker sees it:
each channel's cells unrolled through its loop up to the span. `make
roundtrip` compares that, at level 2, between each `.trk` and the ROM's
song.

**The hold comments.** song_text.py writes "holds the note before up to
the next" beside each bare hold that is the byte right after a note, a rest
or a length (29: 15 after a note, 14 after a length), where DoNoteStop reads
it and does not let the note go. Beside the other 17 bare holds and
releases it writes "does nothing: the next byte is not a length". Build 139's
32 was a miscount.

## Build 139d: Q 1.5

Q 1.5 (October 2026) moved its generators into `q/synth/gen/` and its
band-limited oscillators into `q/synth/va/`. q_synth.cpp includes either
layout and the Makefile's `Q_HAS` finds either (docs/history.md, Build
139d).

**The drums' noise is the port's own.** Q's `white_noise_gen` read its 32
bits unsigned before 1.5: its "white noise" ran from 0 to 2 -- white noise
plus a constant 1 -- not the -1 to 1 its comment says. The drums' noise and
click go through the `svf`'s band-pass or high-pass, which take most of
that constant out but not all, and the drums were voiced by ear on it
(builds 110b-111). Q 1.5 reads the bits signed; built with it unchanged,
every drum moved by up to a decibel in a band or at its peak and the
songs' peaks by up to 0.6 dB. `DrumNoise` in q_synth.cpp is the old
generator, line for line, so the drums sound the same with any Q -- Q's
checks are the same files with the old Q and with 1.5. A zero-mean noise
(Q 1.5's) would change every synthesised drum a little; it is a choice to
make by ear, not taken.


## Build 139e: the toms' membrane, and a room behind them

The owner, on the Q toms: something wrong, perhaps a decay a little fast or a
tiny reverb wanting; then, of five versions, the one with a small room "the
best but there's something radically different about the timbre". Each tom
struck alone through the chip and through Q, measured side by side
(fm_probe's WAVs, 10 ms windows from the strike, partials by FFT):

* **Pitch**: right. From 40 ms Q is within a few Hz of the chip (chip/Q,
  the high tom 221/223, 214/216, 212/212 Hz at 50, 70, 100 ms; the low
  176/176, 169/171, 168/168). The first 20-30 ms ran 1-2 semitones sharp
  (the high tom 259 Hz against the chip's 229 at 10 ms): 111's start, 320 /
  290 / 250 Hz.
* **Decay**: from 80 ms Q fell about 1.5 times as fast, 3-5 dB under the
  chip by 100-160 ms; the chip's sample then stops dead (150, 167, 197 ms:
  2049 bytes at 13.46, 12.26 and 10.41 kHz).
* **Timbre**, the radical difference. The ROM's tom is a sampled drum: the
  strike sets off some fourteen membrane modes between 1.7 and 8.6 times the
  pitch, at inharmonic ratios, the one at 4.24 (about 1 kHz on the high tom)
  nearly as loud as the fundamental, each dying at its own rate -- one pitch
  with a knock and a wash. 111's tom had the fundamental and four overtones
  (1.72, 2.42, 2.95, 4.70) at near one level dying together, 4-6 dB louder
  than the chip's between 25 and 60 ms with nothing between them: heard as
  notes, a chord, synth- or bell-like. And the room of the version he liked
  held the sharp opening pitch under the falling drum, a faint second note
  1.2-1.4 times above in the tail.

Three more versions, each with the room (ps2-port_toms_q2.zip, outside the
tree): the start at the chip's pitch; that with the membrane; the ROM's
sample through Q's clean playback. The owner: "I like 7" -- the membrane. In
q_synth.cpp now:

* **The membrane** (`kTomModes`): the fourteen modes measured in
  assets/music/drums/tom.wav at the high tom's rate, each a ratio to the
  gliding pitch, a level under the fundamental at the strike and a decay
  beyond the body's: 1.70 -5 dB 230 dB/s, 2.20 -9 205, 2.45 -11 115, 2.63 -7
  220, 3.01 -14 65, 3.46 -11 250, 3.78 -12 180, 4.24 -1 385, 4.85 -11 220,
  5.20 -12 195, 5.92 -10 195, 6.56 -11 195, 7.30 -15 140, 8.63 -13 180.
  `DrumPatch.membrane` marks the three toms; 111's four overtones and their
  fields are gone. Measured on the high tom: each mode within 1-2 dB of the
  chip's at the strike, most within about 3 dB through 110 ms.
* **The start** at the chip's opening pitch: 262 / 238 / 202 Hz (high, mid,
  low); the glide, the fall after it and the body's decay as 111 left them.
* **The room** (`TomRoom`): a Schroeder reverberator -- 4 ms before it, four
  damped combs a side of 13.1-19.6 ms (the right's 0.53 ms longer) for a
  0.4 s decay, two allpasses (4.9 and 1.7 ms, the right's 0.31 ms longer,
  0.6) -- fed the toms alone, in mono, and added under them at 0.3 of its
  output (about 20 dB under the dry tom). It runs from a tom's first sample
  to a second after the last and then empties itself, so nothing runs while
  no tom plays and nothing decays into denormals; the bank's reset empties
  it. The ROM samples' mode (`Q_DRUMS_SAMPLES`) is untouched.

What changed in Q's checks: drum_hits' three toms (RMS -31.0 / -30.9 / -30.5
dB against the chip's -31.5 / -31.1 / -30.4; the peaks higher, -8.9 / -9.9
/ -12.0 dB against 111's -15.4 / -15.3 / -14.5: the fourteen modes start
together, a sharper knock), fm_probe's drum $80, and the trim pass, whose
song peaks rose with the toms' (median Q - chip +1.90 to +2.95 dB) while
every song's RMS stayed within 0.1 dB. The trim (`kQTrimPm`, 0.804) is left:
it is a loudness match and the loudness did not move; the loudest song's
peak at it is about -5.3 dBFS, under the mixer's knee at -3. Everything
else of Q's set the same -- every FM file, the attract WAV (the title's
first 40 s have no toms), audio_check_q, the steal report.

## Build 143: the click at an effect's end under EXTRA

The owner, at the game on Windows: "a slight click after pushing the
continue button on dialog boxes" (never noticed on Linux). It was the
game's own sound, the same on every machine.

**What it was.** Every stop in a dialog (and every menu press) plays the
selection sound ($A0) on FM5: B3 for 8 ticks, a 2-tick rest, the pan
swept. At its end cfF2_TrkEnd calls RestoreBGMChn, which gives FM5 back to
the music track by uploading that track's instrument: SetFMIns's 26
writes, the carriers at the instrument's own levels, then RefreshVolume's,
the track's volume added (the Governor's music: `$49` 08 then 2C, `$4D` 00
then 24 -- 36 TL steps, 27 dB). On the Mega Drive that lands on the
channel the effect was sounding, and the levels in between go unheard.
Under VOICES EXTRA the music's FM5 part plays on under the effect -- on
chip A (docs/second_chip.md) or the track's own voice on Q -- and the
upload landed on its sounding note: for the few write slots between the
two (the chip's write queue; Q's slots, the chip's), two carriers 27 dB
too loud, a burst well under a millisecond long, about a tick after the
effect's end. Heard only where the music's part held a note through it:
17 of 39 presses on Q, 16 on the chip (below); never under LIMITED.

**How it was found** (scratch tools, not kept). The new_game route's 40
presses of C (NEW GAME, the name board, the dream, the Governor, Rolf's
house; the last too near the end to measure), each a selection sound,
rendered by a probe that wrote its audio out, with
The owner's settings (Q, OVERSAMPLE 4, EXTRA, FADE, patches) and on the
chip:
* the 100 ms after a press: nothing (the selection sound's attack is
  smooth; peak -7.4 dBFS, nothing at full scale); rendering took at most
  0.8 ms of a 512-frame chunk's 10.7 ms, so no underrun either;
* the high band (above 6 kHz) 155-185 ms after each press, against the
  same render with the selection sound taken out of the game: 17 of 39
  presses 6-48 dB louder there on Q, 16 on the chip; the dream's last
  stop a narrow spike in the waveform 184 ms after the press;
* EFFECTS off: the same (the music's voice, not the effect's); LIMITED:
  none;
* the sequencer's writes by track at the end (PS2_SNDLOG with the hooks
  printed): the effect's key-off, then track 5's upload and its volume;
* the upload kept off the music's voice: 0 of 39 on both.

**The change.** restore_bgm_chn (smps.c) brackets the upload for the
hooks: SMPS_HOOK_RESTORE (smps.h; no chip register; `value` 1 before, 0
after). The chip's writes are as they were. Under EXTRA:
* the chip (synth.c, `chip_track_write`): the upload reaches B alone --
  under the effect's releasing note, second_chip.md's third rule -- and
  not A, which holds the instrument already. SetMaxRelRate's writes at the
  claim reach B on the claimed channel (the claim comes first), so A's
  channel there holds exactly the music track's state; its drifted writes
  onto other channels (132b) stay on A, where the upload never reached
  either.
* Q (q_synth.cpp): between the brackets the track's own voice keeps its
  instrument, patch and volume (SMPS_HOOK_INSTRUMENT and `apply_own`
  skipped); the effect's releasing voice still takes the upload (111c).
  The bracket takes no write slot, so every write keeps its time.

LIMITED (the one chip; Q's `take_channels`) takes the upload as before.
audio.c's record of each track keeps no bracket, so a switch's replay
starts nothing half-way; and a switch cannot land inside one, as a tick's
writes are queued together.

**The check.** `expect sound clicks below DB` (docs/harness.md): the
sharpest bend (second difference) over the last four ticks rendered. The
route effect_end_click holds the Governor's briefing to it at three
selection ends: -32.4, -36.6 and -37.0 dBFS now, -18.7, -18.8 and -16.0 on
142d (a FAIL at the first); outside the four ticks over each end, the
two are the same to the tick.

What moved in the proofs (docs/history.md, Build 143): six audio routes'
`expect sound playing` peaks four ticks after an Escape menu press, 0.9 to
5.8 dB lower (the burst had been the loudest sample there); Q's steal
report in its EXTRA voicings, by a dB or two in the partials after the
effect's track ends. The attract WAVs (chip and Q), the trim pass, every
other fm_probe file and every other route file the same.

## The fade-out's overrun (build 183)

**The ROM.** The fade-out command ($E0, FadeOutMusic, ps2.asm:92206)
copies the chord's voice -- the algorithm and four operator levels kept in
FM3's track -- to the chord's other parts, because the fade's step
(DoFading) calls RefreshVolume on each of tracks 1-5 from that track's own
copy. The loop makes as many copies as the chord has parts, starting at
the track after FM3's; the chord's parts start at FM3's own
(SendFMFreq_A), so the last copy lands on the track past the chord. Every
chord in the game is of three (21 of the 23 songs; My Home and Movement
have none and copy nothing), so that track is the first effect track, the
one an effect on FM3 runs in.

**What it does.** Nothing unless an effect is sounding on FM3 as the fade
starts (PlaySFX clears a track before starting an effect in it). An effect
caught there plays on, but its track's record of its voice is now the
music's: its next volume changes pick carriers by the music's algorithm
and add its volume to the music's levels, until its next instrument or
its end.

**Where.** The game sends the fade from three places: the SEGA screen's
first instruction, the ending before its white, and Gaira's control panel.
A scratch build logged every fade: 42 in the 170 routes, 147 in twelve
whole games by the C player (seeds 1-12 at the probe's pace: 12 at
power-on, 102 at game overs, 22 around the ending, 11 at Gaira). Only
Gaira's has an effect track in use -- the explosion ($A3), 22 frames old
-- once in every game that got there, and nowhere else.

**The explosion.** Its FM3 part (algorithm 5, three carriers at level 0)
steps its volume up 8 (6 dB down) at its frames 17, 25, 33, 41 and 49,
the last at its end. The fade's copy arrives at frame 22, and Mystery has
two chord voices:

| Mystery's chord voice | frames of its 2304 | after the fade a step writes | the mix, 0.4 s from the first changed write, 182 to 183 |
|---|---|---|---|
| first (algorithm 5, levels `$1F $12 $27 $04`) | 0-1535 | the three carriers at volume + `$27`, `$12`, `$04` (29, 13.5 and 3 dB under the explosion's own): 12 writes differ | -23.5 to -26.2 dBFS: 2.7 dB quieter (the difference -30.5 dBFS) |
| second (algorithm 0, one carrier at level 0) | 1536-2303 | operator 4 alone; operators 2 and 3 keep the step at frame 17's level: 8 writes never made | -23.7 to -19.1 dBFS: 4.6 dB louder (up to 10 dB in a 4-tick window; the difference -19.4 dBFS) |

So on the Mega Drive the explosion's last 0.4 s is quieter two times in
three, and one time in three two of its carriers ring on at one level
until the note ends. VOICES LIMITED, measured under the first voice: the
same twelve writes, 2.8 dB against EXTRA's 2.8 on the same route.

**The change.** smps.c's fade_out_music copies as the ROM does, through
track_byte. The sequencer's whole write stream through the scene is the
measuring build's with the ROM's loop, count and hash, under both voices.

**The check.** `expect sound-register PART $REG $VALUE` (docs/harness.md).
Routes fade_out_overrun and fade_out_overrun_second_chord hold FM3's three
level registers after each step; on 182 both pass before the fade and
fail at the first step after it.

## Chords past the chip (build 184)

**The ROM.** A chord (cfE5_ChordMode, `chord N` in the song text) plays
one stream on N tracks: the lead's, and the N-1 track blocks after it.
DoNoteOn_A and DoNoteOff_A key them, SendFMFreq_A and GetFMFreq give each
its own note byte from the stream, cfEF_SetFMIns uploads the instrument to
each, and the pan and the fade's copy (above) walk them the same way; the
loops count from the lead's block, whatever the blocks after it hold. The
game's chords are all of three, on FM3, in songs of four tracks: the two
blocks after FM3's are FM4's and FM5's, which the song's header leaves out
and PlayMusic gives their channels anyway. Two songs have no chord.

**A mod's chord, up to build 183.** The song text took `chord N` up to 255
and the tracker a cell of up to 16 notes, and smps.c's loops indexed the
blocks unchecked. Phantasy's chord made six notes (a scratch copy of 183's
smps_trace, the first 3,000 frames): its parts ran on into the effects'
three blocks and played on whatever channel each block held -- 942 more
writes to the chip than the song's own. Made sixteen: 4,082 more, 936 of
them key writes on FM1 (the melody's channel: a zeroed block's channel is
FM1's) and others on FM6, the drums' channel; and the parts past the tenth
block wrote outside the track array, into the driver's other memory.

**The rule now.** A chord is at most 16 notes (SMPS_CHORD_MAX, smps.h): a
tracker cell's notes, which the cell already held to 16. songtext.c and
tracker.py both refuse past it, naming the file and line (`chord: the
notes: 17 is past 16`; a cell of 17 notes, `more than 16 notes in a
cell`). Within it every note plays, past the Mega Drive's chip if need be.
A lead's part p (1 to N-1) is, in order:

1. a music track after the lead that the song's header leaves out and no
   other chord has taken, on its own channel (FM1-FM5 by its place:
   PlayMusic gave FM4's and FM5's theirs, and a part on track 1-3 gets
   FM1-FM3's). So the game's chords take tracks 4 and 5, as before, and
   the chip's writes for every song of the game are what they were;
2. else a **chord voice**: a track block of the port's own (Smps.voices,
   numbered from SMPS_TRACKS), on a channel of a chip of its own. The
   driver runs it as it runs a part -- keys, pitch, instrument, pan,
   volume, the fade, the pause -- but its writes never reach the Mega
   Drive's chip, nor the sequencer's write hash: they go to the hooks
   alone, as track SMPS_TRACKS + v, and the synth voices them (below).

A lead keeps the parts it took until its song starts again (a song's
start and cfE0 give the music's back and silence their voices), an
effect's track until PlaySFX clears it. An effect's chord takes voices
only (the music tracks are the music's). The fade's copy is made into the
lead's parts as they are, so the game's own fade still copies one block
past its chord of three, the ROM's overrun of build 183.

**How many voices.** smps_load walks every stream of the image as the
sequencer reads it -- every song's tracks, every effect's, the special
effects -- following the jumps, the loops, the gosubs and the chord flags
(the walk takes each flag's arguments by cfHandler's table), and counts
each track's largest chord. The music's voices are the most any one song
needs: its tracks' parts, summed, less the music tracks its header leaves
out. The effects': each effect track's largest, summed over the four.
SmpsData.chord_music and chord_effects; `smps_trace --chords` prints them.
The game's own music needs 0 and 0, so nothing below is made for it.

**The voices on the chip.** Six voices a **chord chip**, a Nuked-OPN2 of
its own (synth.c): the music's on chips of their own, the effects' on the
chips after them, so a chip takes one volume (MUSIC or EFFECTS). Voice v
is channel id smps_voice_channel there, chip smps_voice_chip (smps.h).
The chord chips are made at start-up for the music read
(SynthBackend.set_chord_voices, from audio_load_music); each starts with
its DC blocker set to the reset chip's standing DC (fm_settle), so it is
silent from its first sample, and is rendered only while it sounds:
fm_idle is a chip with no write waiting, no drum, and every operator at
full attenuation for half a second rendered, its blocker's tail spent --
rendering it then adds nothing, and left unrendered it takes up where it
stopped at its next write. The LFO register ($22) is the chord chips' too;
$27 (FM3's special mode) and $2B (the DAC on FM6) are not, as a voice on
a chord chip's third or sixth channel is no FM3 and no drums. VOICES
LIMITED and EXTRA voice them alike: they are no channel of the Mega
Drive's either way.

**On Q.** A virtual channel each, SMPS_TRACKS + v, and a subtractive
voice each; the PM voice pool grows by as many, so a chord's notes take
neither each other's voices nor the tracks'. A chord voice is no chip
channel: it takes none and ends no note there.

**audio.c.** Each voice's writes are kept for the replay on a switch of
synth and after FAST (AudioShadow.voice): the registers of its one channel
(operators, algorithm, pan, pitch), its key and instrument, 40 bytes a
voice.

**The check.** `tools/chords_check.py` (`make smps_trace`, and for the
last two `make headless ps2_headless_q`); docs/harness.md has its cases.
Phantasy's chord made six notes, heard through the headless game against
the game's own: the same to the bit until the first chord, and from it
the difference is the three added notes and their overtones (99.9% of its
energy on the chip, 100.0% on Q) at -30.7 and -32.7 dBFS. Made sixteen,
the mix's peak -8.8 dBFS on the chip (-10.2 for the game's own), -10.0
on Q (-12.1): no sample at full scale.
