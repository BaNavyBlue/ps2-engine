# How much of the ROM is left (build 181)

The README's "Not done yet" said that what is not ported "has not been
measured again since the early count of ~18k instruction lines". No method
was recorded for that count. This page is the new count: a tool anyone can
rerun, the reading of everything it could not settle, and what is left.

**The answer.** No routine of the game's 68000 code is left unported. Every
routine either is cited by the port, or was read and found ported under
another name, replaced by the port's own design, or never run. The reading
found one thing a player sees that the port did not do (dead members
still walked in the party's line; ported in build 182) and one ROM quirk a
listener hears that the port did not reproduce (the music fade-out's
overrun; measured and ported in build 183). Both are under "What is
left", which since 183 holds nothing still to port.

## The numbers

`python3 tools/rom_left.py` (route_out/rom_left/summary.txt, one line a
routine in routines.tsv):

| | routines | instruction lines |
|---|---|---|
| ps2.asm's 68000 code | 1,055 | 19,169 |
| cited by src/ (a label, or a `ps2.asm:N` line inside) | 841 | 16,682 |
| not cited, all read (tools/rom_left_review.tsv) | 214 | 2,487 |
| -- ported under another name | 160 | 1,816 |
| -- replaced by the port's design | 41 | 616 |
| -- never runs | 13 | 55 |
| -- missing | 0 | 0 |

Build 182's walking line cites loc_1183A (the ending's cast, 18 lines),
so the tool now gives 842 routines and 16,700 lines cited and 213 and
2,469 not, 159 and 1,798 of them ported under another name (and 10,406
lines for the lower bound below). Build 183's fade-out cites FadeOutMusic,
DoFading and SendFMFreq_A (45 lines): 845 routines and 16,745 lines
cited, 210 and 2,424 not, 156 and 1,753 of them ported under another
name, 10,433 lines for the lower bound; of the sound driver's 93
routines, 71 cited and 22 read (21 ported in smps.c, the Z80 bus request
replaced).

Control can reach all but 34 of the 19,169 lines (listed under "The
reading"); 11 of those 34 are the one uncited routine nothing reaches,
counted under "never runs", and the rest sit inside cited routines.

Of the 19,169 lines, 1,185 are the sound driver (from SoundDriverBase):
93 routines, 68 cited, the other 25 read (24 ported in smps.c, mostly
under the labels' snake_case names, and the Z80 bus request replaced).

The early "~18k" is close to the whole: 19,169 is all of the disassembly's
68000 code by this count. So that figure was most likely the total, not
what was left.

**Inside the cited routines.** "Cited" counts a routine whose entry, or any
label or cited line in it, src/ names. Most C cites only a routine's entry,
so 6,524 lines in cited routines sit in stretches (a code label to the next)
whose own label src/ never names. Those lines are the place a skipped branch
could hide. So 40 of those stretches were drawn at random, weighted by size
(506 lines; `random.Random(181)`), and each was read against its C. All 40
are in the port: 348 lines ported, 158 replaced by design (jump tables done
as switches, the decompressors, the bus and busy waits). Forty clean draws
put the share skipped at under 7% with 95% confidence (under about 470
lines). The stretches whose own label src/ names hold 10,388 lines, the
floor if nothing else were trusted.

## How it counts

tools/rom_left.py reads ps2.asm the way AS does:

- **Instructions.** Every 68000 mnemonic line, and the one-instruction
  macros (`_btst`, `_move`, ...). The data macros (`nametxt`, `border`, ...)
  and `dc` lines are not instructions.
- **Nameless labels.** `+`, `-` and `/` lines, with `++` meaning the second
  `+` on. A first version read `++` as a label of its own and lost the item
  actions behind `bne ++`.
- **Reachable.** Followed from every code label that data or code takes the
  address of: the vectors, `dc.l` tables, `lea`, `move.l #`, and
  `jmp Table(pc,d0.w)` (with an offset too, `Table-4(pc,d0.w)`). Branches,
  calls and fall-through are followed. A table of `bra.w` or `jmp (X).l` +
  `nop` lines is entered at every line.
- **Routines.** A routine runs from an entry (a `bsr`/`jsr` target, an
  address taken, or a branch table's target) to the next entry in the
  file. Every line belongs to exactly one routine, so the counts add up.
- **Cited.** src/'s C, headers, C++ and HLSL name one of the routine's
  code labels, or a `ps2.asm:N` line (or range) inside it, which is how
  smps.c cites. A line that says "not ported" or "unported" does not count.

tools/ is counted beside src/ (routines.tsv's last column) but not as the
port: it holds the extractors and the harness.

## The reading

Each routine src/ does not cite, and that control can reach, has a line in
tools/rom_left_review.tsv: PORTED (where), REPLACED (what does its job),
DEAD (why it never runs) or MISSING. Five readers took the five regions
(objects and townspeople, the start-up and the screens, the menus and the
save, the windows, the sound driver). Each read the routine in full, found
what reaches it, and read the C it maps to. The seven item actions behind
`bne ++` were read when the tool learned `++`.

Why src/ does not name them:

- **A dispatcher's cases.** The C ports a jump table as a `switch` and
  cites only the dispatcher. Examples: menu.c's ITEM, TECH and EQP steps,
  the armour shop's, the backup check's, every window by its WIN_* id in
  window.c, and npc.c's inits and dispatchers in front of a cited main.
- **The sound driver's names.** smps.c names its functions after the
  ROM's labels in snake_case (DoFading is `do_fading`), so the label
  itself never appears.
- **Replaced.** The VDP's set-up, the DMA jobs and the scroll's edge
  redraws: the port composes and redraws whole planes every frame. The
  joypad: SDL. The art decompressors and Nemesis: tools/ at extraction.
  The battery RAM's copies, checksum, stamp and format: a JSON file a
  slot. The Z80's bus and the YM2612's busy waits: fm.c's queue. The
  window art's RAM copy: each window built from its asset.
- **Never runs.** The CPU's crash trap and the line interrupt (never
  enabled). Object 9 (nothing writes its id). Objects $34-$36: no level
  places them, nothing spawns them, and the mappings of $35 and $36 point
  into compressed art. The Nemesis writer pair no caller selects
  (NemDec_WriteAndAdvance, unreachable).

The 34 lines with no way in: the screen table's seven `nop` pads; the
Nemesis decompressor's to-RAM entry and its writer pair (13; the
disassembly's own note: "doesn't seem to be used"); two coordination flag
handlers the disassembly marks unreferenced (cf_SetFMVol, cf_SetLFOSpd)
and a third no flag selects (cf_unused), 9; and an unlabelled branch after
an `rts` near loc_824C and a stray `rts` after data at loc_58AC (5). None
of it is game behaviour.

## What is left

**Missing until build 182: the dead in the party's walking line.**
(Ported in build 182: game.c's line_pack, route walk_line_dead.)
RenderCharSprites
(ps2.asm:26657) fills each walking slot through its per-slot step
(loc_11868), which skips a member at 0 HP and packs the next living one
into the slot, then hides the slots past the living. With Rolf dead, Nei
leads the line and only three walk. The port's map draw (game.c, the
party's `render_walker` calls) draws `party_member_id[i]` in slot i
whatever the HP. Seen in the port: route paseo_rolf_dead's
02_still_outside.png, where the party is Rolf at 0 HP, Nei, Rudo and Amy,
and Rolf stands at the door. Not seen in an emulator; the ROM side is
from reading. It shows every time someone is down on the map. Only the
pictures change: the positions, the followers' trail and everything the
game decides stay as they are. The ending's cast already followed the
rule for its six, but not for the leader's slot or for RenderCharSprites'
pass every field after it; build 182 does both, which only someone dead
left at home can show (the aftermath has just brought the party to full).

**Not reproduced until build 183: the music fade-out's overrun.**
(Ported in build 183: smps.c's fade_out_music, routes fade_out_overrun
and fade_out_overrun_second_chord. Measured first, as this paragraph
asked: docs/audio.md, "The fade-out's overrun". The reading below was
right about where -- Gaira's control panel, once a game, and nowhere
else in 170 routes and twelve whole games -- and half right about what:
the explosion's last four volume steps do write Mystery's chord voice,
but Mystery has two. Under the first the explosion's last 0.4 s is 2.7
dB quieter in the mix; under the second, one time in three, two of its
three carriers stop stepping and it is 4.6 dB louder.) FadeOutMusic
(ps2.asm:92206) copies FM3's chord voice (algorithm and four TLs) into
`count` tracks starting at the one after FM3. SendFMFreq_A walks a
chord's `count` parts starting at FM3 itself, so the fade's last copy
lands one track past the chord, on the first effect track. smps.c's
fade_out_music stops at the chord's last part. It can only be heard when
an effect on FM3 is still sounding as the fade starts and then changes
its volume before loading a new instrument. By the sound reader's
account, that is the explosion at Gaira's control panel
(Building_GairaControlPanel: the explosion, about 21 frames of palette
fade, then the fade-out under Mystery), once a game: its last volume
steps would write Mystery's chord voice. Not measured. A register trace
of the scene with and without the extra copy would say how much it
changes. Whether to reproduce it is the owner's call: the port keeps
ROM quirks elsewhere (the music pause's, for one).

**Different by design, not counted as left.**
- A battle effect's art is placed at once. The ROM streams it, 16 tiles
  a frame from the next VBlank.
- The boot check's "repaired from the backup copy" ($130E) cannot appear:
  there is no mirror, and a file that will not parse is set aside.
- The second joypad's bytes are never read by the ROM either.

**Read, and the same.**
- The Magic Cap's price test masks bit 7 of the head slot in the port, not
  in the ROM. The slot holds the plain item id (loc_ACC2 writes
  item_index; bit 7 goes on the bag's entry), so the two agree.
- The EXP share's `divu` keeps its register on an overflow where the port
  masks to 16 bits. No member's share reaches $FFFF in play.
- Window $54 takes the ordinary portrait line in the port and Rolf's in
  the ROM. Nothing opens it.
- The big message box's store to $DEB0 is read by nothing.

**Leads not settled.**
- PlaySoundID's command table has two entries, so sound commands $E2 and
  $E3 run into PlaySFX's code. The port takes them as "stop everything"
  (smps.c). Nothing in the code queues either by number; whether an
  enemy's sound byte can hold one was not checked.
- The Earthmen's first walker (loc_1183A) is the first living member, and
  the port drew `party_member_id[0]` (settled in build 182: the port packs
  the line as the ROM does). They differ only with the first member dead
  at the Earthmen's last step, and the aftermath raises everyone (build
  93).

## What the count cannot see

- **Routines ported in part.** The sample above is the estimate.
- **The split by file order.** A stretch after an `rts` that another
  routine branches into is counted in the routine above it. The sample met
  two (the object table after loc_526's code, loc_650E after loc_64E8),
  both with cited owners, so no verdict moved.
- **Timing within a frame or across frames.** The effect art's streaming
  is one case. The routes and the emulator checks answer for timing, not
  this count.
- **The Z80's drum program** (PSII_Disasm/sound/ps2.pcm_drums.asm). It is
  Z80 code, not counted. drums.c plays the extracted samples in its place.
- **Data.** The tables, art, maps and text are the round-trip's to answer
  for (`make roundtrip`, the README's "Round-trip verification"), not this
  count's.

## Running it again

    python3 tools/rom_left.py

It writes route_out/rom_left/ and exits 1 when the review is out of step
with src/: a routine not cited and not in the review (a citation removed,
or the disassembly changed), or a routine in the review that src/ now
cites. Add a line for the first, drop the line for the second. It reads
../PSII_Disasm/ps2.asm (`--asm`), src/ (`--src`) and the review
(`--review`), and takes about two seconds.
