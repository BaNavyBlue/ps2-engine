# FM instrument names (build 136d)

Agreed by the owner after build 136d. (Build 137) Each is its instrument's
`"name"` in `assets/music/fm_instruments.json`, written by `make
extract-music` from `tools/asset_names.py`'s INSTRUMENT_NAMES, and what the
song text calls it (`instrument bass`); the game reads them (137a). To
rename one: change it in fm_instruments.json and everywhere the song text
names it (sounds.txt's `same`/`share` lines too), or in INSTRUMENT_NAMES and
re-extract. Anything is fine as long as each is one word (letters, digits,
`_`, at most 31) and no two are the same: `instruments_load` refuses
otherwise. The table below keeps the reasons for each name.

How the list was made (a one-off, not shipped): every song played for 180
seconds and every effect for 20 through the port's sequencer (smps.c, the
routes' sequencer), each key-on recorded with the instrument its track had
uploaded last (the sequencer's instrument hook, SMPS_HOOK_INSTRUMENT) and
its pitch as a concert note (nearest semitone; the game sits about a
quarter of a semitone sharp, docs/audio.md "Pitch"). "Notes" is the range,
the middle note and the count over those plays: the count only compares
instruments, since a song's loops were played as far as 180 s takes them.

The scheme:

- An instrument only effects play: the first effect's name
  (`level_up`, `zan`); effects without a name are `effect_<id>`.
- A song's instrument: the first song that plays it, then its part --
  `chords` when that song plays it on FM3-FM5 at once (the chord mode,
  cfE5), else where its notes sit: `bass` below C3, `low` C3-B3, `mid`
  C4-B4, `high` from C5. A second of the same kind gets `_2`, and so on.
- 64, the bass under 21 songs on FM2, is just `bass`.
- What no song or effect keys a note with: `unused_<n>`. (52 is named in
  an effect's header, Army Eye's attack on FM5, but replaced before its
  first note; 0 is named in every song's FM track headers and replaced
  the same way, and sounds only in the Selection effect.)

| # | Proposed name | Alg. | Used by | Notes |
|---|---|---|---|---|
| 0 | `selection` | 3 | effects: Selection FM5 | B3-B3, mid B3 (1) |
| 1 | `level_up` | 4 | effects: LevelUp FM4, FM5 | D#4-B5, mid D#5 (12) |
| 2 | `item_received` | 4 | effects: ItemReceived FM4, FM5 | C#5-E5, mid C#5 (4) |
| 3 | `explosion` | 5 | effects: Explosion FM3, FM5 | B1-A5, mid A5 (14) |
| 4 | `level_changed` | 7 | effects: LevelChanged FM3 | A#3-A#3, mid A#3 (4) |
| 5 | `fell_in_hole` | 4 | effects: FellInHole FM4, FM5 | F1-C4, mid F1 (2) |
| 6 | `revived` | 5 | effects: Revived FM4, FM5 | A#2-D#3, mid A#2 (6) |
| 7 | `poison_cured` | 1 | effects: PoisonCured FM4, FM5 | B2-D#3, mid B2 (4) |
| 8 | `alarm` | 4 | effects: Alarm FM3 | B4-B4, mid B4 (1) |
| 9 | `hidapipe` | 7 | effects: Hidapipe FM4, FM5 | C5-G5, mid D5 (26) |
| 10 | `teleport` | 4 | effects: Teleport FM4, FM5 | G2-G3, mid C#3 (18) |
| 11 | `door_open` | 5 | effects: DoorOpen FM5 | A3-A3, mid A3 (2) |
| 12 | `dam_opened` | 2 | effects: DamOpened FM3, FM4, FM5 | E3-D#4, mid F#3 (6) |
| 13 | `jet_scooter` | 4 | effects: JetScooter FM3 | F#1-F#1, mid F#1 (200) |
| 14 | `spaceship_departed` | 5 | effects: SpaceshipDeparted FM3, FM5 | G#2-B2, mid G#2 (4) |
| 15 | `spaceship_landed` | 5 | effects: SpaceshipLanded FM3, FM5 | G1-D5, mid A#1 (14) |
| 16 | `dangerous_floor` | 2 | effects: DangerousFloor FM5 | G#3-A#4, mid G#3 (2) |
| 17 | `musik` | 5 | effects: Musik FM4, FM5 | C#3-F#5, mid D#4 (17) |
| 18 | `boomerang` | 3 | effects: Boomerang FM4, FM5 | G#4-G#4, mid G#4 (6) |
| 19 | `sword` | 0 | effects: Sword FM3, FM5 | D4-G5, mid C#5 (4) |
| 20 | `claw` | 2 | effects: Claw FM4, FM5 | G#4-G#4, mid G#4 (4) |
| 21 | `slasher` | 3 | effects: Slasher FM4 | A#4-E5, mid A#4 (2) |
| 22 | `shotgun` | 5 | effects: Shotgun FM3, FM5 | G2-A3, mid A#2 (4) |
| 23 | `strong_gun` | 5 | effects: StrongGun FM4, FM5 | A#2-C4, mid E3 (8) |
| 24 | `effect_b8` | 5 | effects: B8 FM4, FM5 | F#3-F#4, mid C4 (6) |
| 25 | `effect_b9` | 5 | effects: B9 FM3, FM5 | F#3-G#4, mid F#4 (7) |
| 26 | `damage_red_screen` | 3 | effects: DamageRedScreen FM5 | G2-A#4, mid A3 (26) |
| 27 | `foi` | 1 | effects: Foi FM3, FM5 | F1-D2, mid G#1 (6) |
| 28 | `zan` | 5 | effects: Zan FM4, FM5; Vol FM4, FM5; Eijia FM3, FM4, FM5 | C1-C6, mid B2 (25) |
| 29 | `vol` | 5 | effects: Vol FM4, FM5 | G1-G5, mid F4 (6) |
| 30 | `tsu` | 5 | effects: Tsu FM5 | F#1-F4, mid F#1 (2) |
| 31 | `shinb` | 5 | effects: Shinb FM5 | F3-B3, mid G#3 (3) |
| 32 | `eijia` | 4 | effects: Eijia FM3, FM4, FM5 | B0-F4, mid C#4 (21) |
| 33 | `gaj` | 4 | effects: Gaj FM3, FM4, FM5 | F#1-E8, mid A4 (7) |
| 34 | `deban` | 4 | effects: Deban FM4, FM5 | C3-E4, mid G#3 (20) |
| 35 | `healed` | 4 | effects: Healed FM5 | E4-E4, mid E4 (5) |
| 36 | `effect_c4` | 4 | effects: C4 FM3, FM5 | A3-D5, mid G#4 (26) |
| 37 | `neifirst_attack` | 3 | effects: NeifirstAttack FM5 | G#2-G#2, mid G#2 (1) |
| 38 | `whip` | 4 | effects: Whip FM4, FM5 | D2-D4, mid D2 (4) |
| 39 | `whistle_attack` | 3 | effects: WhistleAttack FM4, FM5 | D#2-A2, mid F#2 (8) |
| 40 | `polezi_attack` | 3 | effects: PoleziAttack FM4, FM5 | C#2-C#3, mid C#2 (24) |
| 41 | `pulser_attack` | 2 | effects: PulserAttack FM3, FM5 | B2-A3, mid B2 (36) |
| 42 | `terakite_attack` | 3 | effects: TerakiteAttack FM3, FM5 | F2-F#3, mid F3 (6) |
| 43 | `fire_ant_attack` | 4 | effects: FireAntAttack FM4, FM5 | F#1-G#2, mid G2 (6) |
| 44 | `bee_attack` | 2 | effects: BeeAttack FM4, FM5 | A#1-F7, mid B1 (6) |
| 45 | `amoeba_attack` | 1 | effects: AmoebaAttack FM4, FM5 | D#1-D#2, mid A#1 (14) |
| 46 | `blaster_attack` | 4 | effects: BlasterAttack FM4, FM5 | A#1-G#3, mid A2 (12) |
| 47 | `van_attack` | 0 | effects: VanAttack FM4 | D#1-D2, mid G1 (3) |
| 48 | `van_attack_2` | 0 | effects: VanAttack FM5 | F1-D#2, mid F1 (2) |
| 49 | `spinner_attack` | 4 | effects: SpinnerAttack FM5 | - |
| 50 | `mosquito_attack` | 4 | effects: MosquitoAttack FM4, FM5 | D2-F#5, mid G#3 (22) |
| 51 | `locust_attack` | 5 | effects: LocustAttack FM3; ArmyEyeAttack FM4, FM5 | E2-D4, mid F#3 (26) |
| 52 | `unused_52` | 1 | nothing keys a note with it | - |
| 53 | `enemy_killed` | 4 | effects: EnemyKilled FM5 | D2-D5, mid D3 (5) |
| 54 | `pause` | 2 | effects: Pause FM4, FM5; Unpause FM4, FM5 | F#5-B5, mid A#5 (16) |
| 55 | `unused_55` | 4 | nothing keys a note with it | - |
| 56 | `unused_56` | 7 | nothing keys a note with it | - |
| 57 | `unused_57` | 7 | nothing keys a note with it | - |
| 58 | `unused_58` | 7 | nothing keys a note with it | - |
| 59 | `unused_59` | 7 | nothing keys a note with it | - |
| 60 | `unused_60` | 7 | nothing keys a note with it | - |
| 61 | `unused_61` | 7 | nothing keys a note with it | - |
| 62 | `unused_62` | 7 | nothing keys a note with it | - |
| 63 | `unused_63` | 7 | nothing keys a note with it | - |
| 64 | `bass` | 7 | Phantasy FM1, FM2; Restoration FM2, FM3, FM4, FM5; Pleasure FM2; ExcitingTown FM2; SecretWays FM2; Advanced FM2; Under FM2; StepUp FM2; BrackyNews FM2; MyHome FM2; Pressure FM2; Dream FM2; Power FM2; Exclaim FM2; RiseOrFall FM2; ThePlaceOfDeath FM2; Movement FM2; Over FM2; MovementVer2 FM2; NeverDream FM2 | F#1-F#5, mid G#2 (12476) |
| 65 | `phantasy_mid` | 4 | Phantasy FM1; SecretWays FM1; Dream FM1 | D#4-B5, mid A#4 (324) |
| 66 | `phantasy_chords` | 4 | Phantasy FM3, FM4, FM5; SecretWays FM3, FM4, FM5; Violation FM3, FM4, FM5; Under FM3, FM4, FM5; Dream FM3, FM4, FM5; ThePlaceOfDeath FM3, FM4, FM5 | C#3-F#5, mid F4 (2007) |
| 67 | `movement_chords` | 3 | Movement FM1, FM2, FM3, FM4, FM5 | B3-B5, mid B4 (20) |
| 68 | `phantasy_mid_2` | 1 | Phantasy FM1; Movement FM3, FM4, FM5; Over FM3, FM4, FM5; MovementVer2 FM3, FM4, FM5; NeverDream FM3, FM4, FM5 | D#3-G#5, mid G#4 (2891) |
| 69 | `my_home_mid` | 7 | MyHome FM3; Exclaim FM3, FM4, FM5 | F3-G5, mid A#4 (1004) |
| 70 | `phantasy_high` | 7 | Phantasy FM1; SilentZone FM1; Advanced FM3, FM4, FM5 | B3-G5, mid C5 (879) |
| 71 | `phantasy_chords_2` | 7 | Phantasy FM3, FM4, FM5 | C#4-E5, mid G#4 (69) |
| 72 | `restoration_high` | 4 | Restoration FM1; StepUp FM1; Over FM1; NeverDream FM1 | A3-C#6, mid C#5 (1466) |
| 73 | `silent_zone_low` | 1 | SilentZone FM2 | A#2-D#4, mid F3 (704) |
| 74 | `step_up_chords` | 0 | StepUp FM3, FM4, FM5 | F#4-E5, mid B4 (1347) |
| 75 | `silent_zone_chords` | 4 | SilentZone FM3, FM4, FM5; MyHome FM4; Exclaim FM1, FM3, FM4, FM5; ThePlaceOfDeath FM3, FM4, FM5 | F3-D#5, mid F#4 (1504) |
| 76 | `pleasure_high` | 0 | Pleasure FM1 | B4-G#5, mid F5 (165) |
| 77 | `pleasure_high_2` | 6 | Pleasure FM1; Movement FM1; MovementVer2 FM1 | G#4-G#5, mid D#5 (886) |
| 78 | `pleasure_chords` | 4 | Pleasure FM3, FM4, FM5 | F#4-F#5, mid C#5 (360) |
| 79 | `pleasure_chords_2` | 2 | Pleasure FM3, FM4, FM5 | F#3-F4, mid C4 (1158) |
| 80 | `pleasure_chords_3` | 5 | Pleasure FM3, FM4, FM5; Mystery FM3, FM4, FM5 | F#3-D#5, mid A#4 (669) |
| 81 | `pleasure_high_3` | 5 | Pleasure FM1 | G#4-F#5, mid D#5 (24) |
| 82 | `exciting_town_mid` | 4 | ExcitingTown FM1; Advanced FM3, FM4, FM5; ThePlaceOfDeath FM1 | B3-A#5, mid A#4 (1359) |
| 83 | `exciting_town_high` | 2 | ExcitingTown FM1 | F#4-A#5, mid F5 (153) |
| 84 | `exciting_town_chords` | 3 | ExcitingTown FM3, FM4, FM5 | B3-D#5, mid A#4 (1788) |
| 85 | `secret_ways_mid` | 6 | SecretWays FM1 | G#3-D#5, mid G#4 (252) |
| 86 | `secret_ways_chords` | 4 | SecretWays FM3, FM4, FM5; Movement FM1; MovementVer2 FM1 | A#3-G#5, mid F#4 (993) |
| 87 | `advanced_high` | 4 | Advanced FM1 | G#4-A5, mid C#5 (440) |
| 88 | `advanced_high_2` | 4 | Advanced FM1 | G#4-C#6, mid G5 (150) |
| 89 | `mystery_high` | 4 | Mystery FM1 | F4-A#5, mid D5 (155) |
| 90 | `mystery_high_2` | 4 | Mystery FM1 | A#4-C6, mid D#5 (95) |
| 91 | `mystery_bass` | 0 | Mystery FM2 | F2-D3, mid A#2 (775) |
| 92 | `mystery_chords` | 0 | Mystery FM3, FM4, FM5; Violation FM3, FM4, FM5; Pressure FM1; RiseOrFall FM3, FM4, FM5 | C4-C6, mid C5 (1200) |
| 93 | `violation_mid` | 4 | Violation FM1 | F4-G5, mid B4 (173) |
| 94 | `violation_high` | 4 | Violation FM1; MyHome FM1, FM5; Pressure FM1 | F4-C6, mid D5 (940) |
| 95 | `violation_low` | 0 | Violation FM2 | E2-D3, mid C3 (288) |
| 96 | `under_mid` | 5 | Under FM1; Pressure FM3, FM4, FM5 | C#4-D#5, mid F4 (668) |
| 97 | `under_mid_2` | 5 | Under FM1 | G#3-A#5, mid A#4 (155) |
| 98 | `under_chords` | 2 | Under FM3, FM4, FM5 | D#4-B4, mid G#4 (1224) |
| 99 | `bracky_news_chords` | 4 | BrackyNews FM3, FM4, FM5 | A#4-B5, mid D#5 (573) |
| 100 | `bracky_news_mid` | 6 | BrackyNews FM1; MyHome FM1, FM5; Pressure FM3, FM4, FM5; ThePlaceOfDeath FM1 | B2-G5, mid E4 (1503) |
| 101 | `bracky_news_mid_2` | 4 | BrackyNews FM1 | A#2-D#4, mid C4 (346) |
| 102 | `unused_102` | 4 | nothing keys a note with it | - |
| 103 | `power_chords` | 7 | Power FM1, FM3, FM4, FM5; Exclaim FM1 | G3-G5, mid A4 (473) |
| 104 | `rise_or_fall_chords` | 2 | RiseOrFall FM1, FM3, FM4, FM5 | A#3-C6, mid C5 (605) |
