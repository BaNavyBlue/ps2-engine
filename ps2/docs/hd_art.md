# HD art from the game's own pictures (build 158)

The owner's aim (after build 157e): an image model that has learnt the
Phantasy Star look -- this game's art, the in-game pictures and sprites
among it, and as much of the series' other art as can be had (Phantasy
Star I from its port, III and IV, the original artists' illustrations and
concept art, manuals, boxes) -- and draws each of the game's pictures
again in high resolution from the picture itself. Not a model made from
nothing: an existing one (SDXL) taught the style with a LoRA, a small
add-on trained on the collected art, trained again as more is added.

The hard rule: **an HD picture is exactly a whole number of times its
native size**, never another scale -- docs/asset_format.md's HD set
already says so (a file at a whole-number scale of its native size, the
scale read from the size). The scale for the owner's set is **9x**: the
game shown in its TV (or 4:3) shape on a UHD (3840 x 2160) television in
HDR is 224 lines x 9 = 2016 of the screen's 2160. The TV shape is the
display's last step, the same horizontal squeeze every native frame gets
today; the art stays on the native pixel grid, 9 x 9 HD pixels for each.

Nothing here is in the game yet, and nothing goes into `assets/art_hd/`
until the owner wants a picture there: the HD compose (asset_format.md,
step 3) waits for art he has chosen.

## The machine (C:\AI, outside every repository)

| Path | What |
|---|---|
| `C:\AI\ComfyUI_windows_portable` | ComfyUI v0.39.0, the NVIDIA portable build (its own Python 3.13, PyTorch 2.14 with CUDA 13); `ComfyUI\extra_model_paths.yaml` points it at `C:\AI\models` |
| `C:\AI\OneTrainer` | OneTrainer (the LoRA trainer), its own venv: Python 3.12 from uv (`C:\AI\uv`), PyTorch 2.12 with CUDA 13, bitsandbytes |
| `C:\AI\models\checkpoints` | Illustrious-XL v0.1 (SDXL, tuned on anime art), the base |
| `C:\AI\models\controlnet` | xinsir's tile ControlNet for SDXL: holds the native picture's shapes |
| `C:\AI\models\upscale_models` | Real-ESRGAN x4plus anime 6B (whole-number 4x; not in the 9x path) |
| `C:\AI\models\loras` | the trained LoRAs (`ps2art_v1.safetensors` ...) |
| `C:\AI\datasets` | `ps2_art_dump` (`make art`), `ps2_train` (hd_dataset.py's set), `ps2_official` (illustrations to add), the other games' sets |
| `C:\AI\train` | OneTrainer's workspaces and caches |
| `C:\AI\out` | hd_generate.py's results |
| `C:\AI\logs` | ComfyUI's and the trainings' logs |
| `C:\AI\tools\uv.exe` | uv 0.13.0 (made the trainer's Python) |
| `C:\AI\tools` | (168c) the gathering, curating, picking and set-building scripts of the runs below, none of them in this tree |
| `C:\AI\out\review` | (168c) the picker pages: a set's pictures or a round of renders, each markable, the owner's choices saved beside them |

The GPU is the RTX 3080 Ti (12 GB). ComfyUI runs as a local server,
127.0.0.1:8188 only, started hidden:

    C:\AI\ComfyUI_windows_portable\python_embeded\python.exe -s ComfyUI\main.py
        --windows-standalone-build --listen 127.0.0.1 --port 8188 --disable-auto-launch
    (from C:\AI\ComfyUI_windows_portable; log in C:\AI\logs)

Before a training run, ComfyUI lets go of the GPU's memory:
`curl -X POST http://127.0.0.1:8188/free -d '{"unload_models":true,"free_memory":true}'`.

## The tools (tools/, Python with Pillow; no part of the game)

* `hd_dataset.py OUT --dump GAME:DIR --images GAME:KIND:DIR ...` -- the
  training set, (158a) of the whole series: art dumps in this tree's
  layout, and folders of a game's own images (`ingame`), big maps cut into
  screen-sized pieces (`map`) or illustrations (`official`). Each image
  once (an animation's frames apart, at most three of them spread over it
  -- a monster's eight near-alike frames outweighed everything else --, a
  frame seen before left out), the in-game ones cropped to what they draw
  and enlarged by the largest whole number within 1024 (nearest
  neighbour), on grey, with a mask of what they draw (OneTrainer's masked
  training: the grey is not learnt); a caption of tags: `psclassic`, the
  game (`phantasy star ii` ...), `pixel art, sprite` for the games' own
  images or `official art, illustration` for the rest, then what and who.
  Writes OneTrainer's concepts.json (a concept a game and kind, the
  illustrations repeated to about a third of each epoch) and train.json
  (SDXL LoRA, rank 32, 1024, batch 2, AdamW 8-bit, bf16, masked).
* `hd_generate.py NATIVE OUT_DIR [--scale 9] [--lora NAME] ...` -- an HD
  picture through ComfyUI: the native picture enlarged k times as both the
  starting image and the tile ControlNet's guide -- (158a) smoothly
  (Lanczos), not as blocks, so no pixel edge is there to be kept --, the
  canvas padded to SDXL's multiples of 8 and cropped back, an optional
  whole-number `work` factor taken back out by averaging blocks; the
  transparency inside the native one enlarged, (158a) with a smooth
  outline that follows the sprite and cuts its stair-steps' corners
  (`--edge exact` for the blocks). Defaults (158a): denoise 0.7, the
  ControlNet 0.45 to 75% of the steps. Writes the picture, its check and
  a preview (the native picture enlarged, the HD one, and the HD one
  averaged back and enlarged again -- that third is the check, blocky by
  design, not a result).
* `hd_check.py NATIVE HD` -- how faithful an HD picture is: each k x k
  block averaged and held to its native pixel (CIE76 Delta E, mean, max,
  how many past 10), and the transparency: (158a) inside the native one
  (nothing drawn where it is clear), exact or with a smooth edge, and how
  much of its coverage is drawn; refuses a size that is not a
  whole-number scale.

Training, from `C:\AI\OneTrainer`:

    venv\Scripts\python.exe scripts\train.py --config-path C:\AI\datasets\ps2_train\train.json

## The other games' art (158a)

* Phantasy Star I: `C:\AI\tools\ps1_art_export.py` draws its screens
  (towns, rooms, shops, landscapes, story pictures, title, opening: 44 of
  45, flight_arrival has no palette file) from a snapshot of the PS1
  tree's last commit (`git archive HEAD ps1`: that tree is another
  session's to change), each `.cells` file over the tile sheet its header
  names, in its palette's --BBGGRR bytes; its `render_maps.py --plain`
  draws the overworld maps. Its monsters (sprite frames, name-table cells
  and a palette made from the scene's and the monster's) are not drawn
  yet.
* Phantasy Star III and IV, the original artists' illustrations, concept
  art, manuals and boxes: being gathered (The owner: "as much imagery
  and art as possible"); disassemblies of III and IV, if he finds them,
  give their in-game art exactly, as PSII_Disasm gives this game's.

## First results

* Rolf's portrait (80 x 80) with the base model alone, no LoRA (seed 1,
  denoise 0.55, ControlNet 0.6): 720 x 720 exactly, transparency exact,
  Delta E mean 7.98, 24% of pixels past 10. The face and armour redrawn,
  much of the hair still pixel blocks: the untrained starting point.
  The owner: "definitely don't want it to look like giant pixels" --
  "true to the spirit of the original art not just a pixelated upscale".
* (158a) The guide enlarged smoothly, redraw 0.55 / 0.65 / 0.75 / 0.85
  (seed 1, base model): no block edges at any; Delta E mean 11.3 / 13.1 /
  18.2 / 24.9. 0.55-0.65 keep traces of the stepped outlines in the
  hair; 0.75 clean and close; 0.85 a modern anime look, the wall behind
  him gone -- and the portraits' backgrounds are part of them (the
  building screen around the window is black), so they must stay. The
  defaults sit at 0.7.
* (158a) The fire ant's first frame (48 x 37, work 2): 432 x 333; the
  exact edge leaves the outline in stair-steps, the smooth one follows the
  ant (98.2% of its pixels drawn, none outside).
* The look of the 1989 illustrations is the LoRA's to give: with the base
  model alone, every setting draws today's anime.

## The series' set and v0 (158b)

The owner: "as much imagery and art as possible", and of the ant drawn
by the base model, "too smooth and cartoony looking. PS2 has this mix of
gritty and cartoony".

* **Gathered** (C:\AI\tools\gather.py, one request at a time, each site's
  pace, every file's origin in C:\AI\datasets\web\sources_*.csv): the
  Internet Archive's scans (the PS III strategy books, the Saturn
  Collection's manual, the PS IV Players Guide, the hint books, the
  manuals: 921 pages), its long-play videos of PS I-IV and Gaiden, the
  Phantasy Star wiki's image categories (1196), The Spriters Resource's
  sheets (164), PS IV's cut-scene panels (RPG-O-Mania, 296), Phantasy Star
  Cave's galleries and sprites (743), VGMuseum's endings (219). Left out
  on purpose: Hardcore Gaming 101 (its robots.txt shuts Claude out) and
  Sega Retro (an anti-AI-scraper wall).
* **Curated** (curate.py, sheet_slice.py, frames.py, frame_filter.py,
  tag_gpu.py, page_filter.py in C:\AI\tools): sheets cut into their
  sprites (3329), the long-plays' scene changes shrunk by their exact
  factor to native size and the near-alike and dark ones dropped (6537),
  everything tagged by SmilingWolf's WD ViT tagger v3 on the GPU, and the
  book pages whose tags name nobody and nothing drawn set aside (639 of
  1631: guide text, tables, maps). C:\AI\datasets\curated\<game>\<kind>.
* **hd_dataset.py (158b):** a game's in-game images capped at
  `--ingame-cap` (900) an epoch, drawn afresh each epoch (OneTrainer's
  SAMPLES balancing); the illustrations twice an epoch; a tagged image's
  caption its tags (and a character's name from its path), not the path's
  words.
* **hd_generate.py (158b):** `--style` (the style's words, replacing the
  default's), `--avoid` (added to the negative prompt), `--guide soft`
  (the blocks softened just past their edges: the shading's pattern kept).
* **v0** (psclassic_v0_ingame, the games' own images only, PS2's and
  PS1's, 4 epochs, 1h50): with the style "1980s (style), retro artstyle,
  official art" and against "pixel art, sprite, glossy, 3d, logo, text",
  Rolf comes out an 80s cel picture -- heavy ink outlines, the wall behind
  him kept -- with or without v0; v0 itself changes little (the colours,
  the eyes a little nearer the sprite's). Learning only sprites, it learns
  who and what, not the illustrators' hand: that is v1's, with the
  illustrations. Left to watch: the ink outlines still follow the
  sprite's pixel steps in places; a pale halo round a portrait's figure.

## III and IV from their disassemblies; names (158c)

The owner owns the four games, so their own art is exported from
Phantasy Star Cave's disassemblies (which carry the ROMs), by
exporters that live beside the data (C:\AI\tools\ps3_art,
C:\AI\tools\ps4_art), into C:\AI\datasets\ps3_art_dump and
ps4_art_dump, each with an index.json (the art, palette and layout each
picture came from):

* **IV** (511 pictures): 43 dialogue and shop portraits (6x6 cells on the
  field's palette line 2), 4 intro portraits, 32 battle backgrounds, 146
  enemies (each enemy's record, its animated parts at their first frame),
  42 battle poses, 11 walking sheets, 228 cut-scene panels (named from the
  disassembly's comments), 5 title pieces. Left out: the maps' tile sets
  (each tile's palette line is in block data not decoded), townspeople,
  the Sega logo, two boss backdrops whose palettes were not found.
* **III** (485): 37 portraits (64x64; shopkeepers 56x56), 22 cut-scenes
  (the generations' story pictures, the weddings, the monitor), the title,
  14 battle backgrounds (at scroll 0), 181 enemies' frames (402 files;
  the battle's colours 1-3 written over as the game does), 9 space pieces.
  PS III packs its art with its own Huffman-like scheme (loc_D878);
  Nemesis only for the Sega logo.
* **Checked against the games' own frames** (the long-play videos, at
  native size): IV's intro portraits, shop portraits, enemies and battle
  backgrounds match in layout and colour (98-100% of pixels within the
  video's error, after mapping the video's colour ladder, about 36 a
  level, to the DAC ladder used here); III's portraits, cut-scenes,
  enemies and backgrounds 95-100% (the frames mostly caught mid-fade).
  What colours cycle in play (IV's Plate System, III's title) is the
  starting or the longest-held state.
* **Captions:** C:\AI\tools\dump_captions.py writes each picture's
  .txt -- the kind (portrait, battle background, monster ...), what it
  is, then the tagger's tags -- for `--images ps4:ingame:DIR`.
* **hd_dataset.py (158c):** a path's names are found as whole words
  ("Alys and hahn 100m" -> alys, hahn; until 158c only a folder or file
  named for one party member and nothing else), and III's and IV's casts
  are known by name. The training's sample pictures are three (Rolf, a
  monster, Chaz), against text and logos: they are the model drawing, to
  watch it learn, never training data. v0's single sample asked for Rolf
  and drew a monster under a made-up logo -- the in-game set alone had
  not taught it Rolf, and "official art" brought the box scans' text.

## v1 stopped; only clean pictures (158d)

v1's first epoch spoilt the base model's drawing: its sample (the same
prompt, "official art", against "pixel art") came out blurred and in
blocks where the untrained model's was a clean illustration. Measured
(C:\AI\tools\set_stats.py): the long-play videos' frames -- half the
in-game set -- had thousands of colours each where a Mega Drive screen
has at most 61 (the video's compression), the wiki's screenshots were
mostly resized, and some illustrations were small, stretched to train.
Enlarged by whole numbers, a blurred picture is blur drawn in blocks: what
v1 learnt. (v0, the games' own exact pictures only, had not.) v1 was
stopped after its first epoch (its checkpoint kept in C:\AI\train\v1).

* **The frames snapped** (C:\AI\tools\snap_frames.py): each channel to
  the nearest of the levels the video shows -- the Mega Drive's eight
  (0, 34, 70, 107, 143, 179, 215, 250 in these videos), the Master
  System's four (0, 62, 126, 190: PS I's video records them dark) --
  written as the console's own (the DAC ladder; 0/85/170/255). A frame of
  thousands of colours comes back to about a hundred (a fringe at sharp
  edges is left), its pixel art as drawn. Gaiden's Game Gear frames (16
  levels a channel, too fine to snap) set aside. The raw frames kept in
  curated/_rejected.
* **hd_dataset.py (158d):** an in-game picture already enlarged (each
  pixel a k x k block) is brought back to its own pixels, and left out
  if it has more than 256 colours (`--ingame-colours`) or is still bigger
  than --max-side; an "illustration" of 64 colours or fewer is the game's
  own (a screenshot filed as art) and taken as in-game; an illustration
  whose short side is under 512 (`--official-min`) is left out. An epoch
  now shows each illustration three times (`--official-repeats`, 2
  before) and at most 600 of a game's in-game pictures (`--ingame-cap`,
  900 before): about as many illustrations as in-game pictures, where v1
  had a third.
* **v2** (psclassic_v2): v1's sources, III's and IV's own art, the frames
  snapped -- 13257 images (illustrations 774), 643 blurred or compressed
  in-game pictures, 121 small illustrations and 44 too-big ones left out.
* A slip caught before training: Pillow's getbbox() reads an RGBA image's
  alpha only, so the first test of "is it enlarged" passed every opaque
  picture and shrank the frames to an eighth. Every band is compared now
  (getextrema), and the set was rebuilt and looked at full size before
  the training was started.

## v3 to v7 (2026-10-13 to 10-17; written up in build 168c)

Five more runs after build 158d, made in sessions outside this tree: their
sets, tools, logs and LoRAs are all in C:\AI, and nothing here changed for
them (hd_dataset.py, hd_generate.py and hd_check.py are still 158d's; the
sets were made from train_v2's pictures by the scripts in C:\AI\tools).
Written up in build 168c from the runs' own files (C:\AI\datasets\train_v3
... train_v7, the set builders' and trainings' logs in C:\AI\logs, the
files in C:\AI\models\loras) and those sessions' notes. Dates are UTC.

Every run's train.json is v2's: Illustrious-XL v0.1, a LoRA of rank 32
(alpha 16), AdamW 8-bit (weight decay 0.01), 1e-4 falling on a cosine
after 100 warm-up steps, 3 epochs of batch 2 at 1024 (aspect buckets),
bf16, masked (the unmasked part at 0.1), a save and the sample pictures
each epoch; about 1.6 s a step. Only the concepts and the captions
changed:

| Run | Trained | Steps an epoch | New in the set | LoRA files |
|---|---|---|---|---|
| v3 | 10-13 | 2,598 | the owner's picks of v2's set | v3, _e1, _e2 |
| v4 | 10-14 | about 3,820 | 1,450 frames and drawings from 1980s-90s anime films and manga | v4, _e1, _e2 |
| v5 | 10-15 | 4,485 | 358 pictures of what the renders failed on; described captions; 233 of the renders themselves | v5 |
| v6 | 10-16 | 4,746 | the renders out; 6,078 subject pictures, half of them photographs and film stills | v6, _e1, _e2 |
| v7 | 10-17 | 4,748 | III's, IV's and I's drawings of PS2's monsters captioned as PS2's | v7 |

* **v3** (psclassic_v3). The owner on v2's Rolf: "I'm not a fan at
  all. ... The upscale looks all smooth with even lighting and pastel skin
  tone. ... The cartoon style is more cutesy cartoon than edgy scifi
  fantasy." Measured: in the game's picture Rolf's lit skin is 22.5 points
  lighter than his shaded skin, in v2's 15; and about 45% of v2's epoch
  was manual and hint-book pages, in a soft pastel. A picker page (v2's
  13,257 pictures in 81 groups, each to drop, keep or favour) took
  The owner's choices, and apply_choices.py made the set from them,
  train_v2's captions unchanged: 5,407 favoured in-game pictures (3,000
  samples an epoch), 156 favoured illustrations (four times), 582 kept
  illustrations (once), 6,968 kept in-game pictures (1,000 samples), 144
  dropped. The pages fell to 11% of an epoch. 4h14 with the caching, to
  the end.
  The samples came out darker and less cute, with no manual text, but in
  the beige of scanned paper (the favoured illustrations are paintings
  scanned off book pages) and with no true black. Through hd_generate at
  its middle settings Rolf's gap was 18.1 with v3, 17.8 with v2 and 18.2
  with no LoRA: the settings made the look, not the LoRA. The in-game
  pictures' hard light and shade is learnt under "pixel art, sprite", the
  words hd_generate asks against. v3's best Rolf (denoise 0.8, the
  ControlNet 0.4 to 70% of the steps, v4's style words below): 21.3.
  The owner: "We're starting to go in the right dirrection, but I think
  we need to look for some more training assets" -- Akira (the film and
  the manga), Appleseed, Macross (the first series and Do You Remember
  Love?), Ghost in the Shell and Innocence, and Nausicaa.
* **v4** (psclassic_v4). gather_anime.py fetched from the sites whose
  rules allow it, as gather.py does (one request at a time, every file's
  source in a CSV), into C:\AI\datasets\web_anime; a picker of 1,602
  pictures in 19 groups; anime_set.py captioned the owner's choices:
  `celclassic`, the title, `retro artstyle`, `1980s (style)` (`1990s` for
  Ghost in the Shell), the kind (anime screencap, official art, reference
  sheet, production art), then the tagger's tags. 1,450 kept (152 left
  out, 86 by their tags: Robotech's, cosplay, edits, the later Macross
  series): anime_fav 331 (four times), anime_fav_nausicaa 257 (260
  samples), anime_keep 862 (once), about a third of an epoch; v3's four
  concepts as they were. Two more samples: Rolf in the anime words, and an
  Akira biker.
  Its restarts (logs train_v4_stopped, _recache, _update, _part2, _part3,
  train_v4): stopped at step 276 to free the GPU for a test of v3; started
  again, it cached everything again -- OneTrainer gives each concept a new
  random seed a run and names its cache after it -- so the seeds were
  written into concepts.json; at the end of epoch 2 Windows Update
  restarted the PC before the checkpoint was saved; resumed from the last
  backup (train_resume.json: train.json with `continue_last_backup`),
  paused twice more for tests, and finished.
  The caption words alone, at step 0, gave the untrained model hard 1980s
  cel shading. The LoRA pulled the other way: Rolf's gap 13.3 at epoch 2
  and 12.4 at the end (14.0 in neutral words, 10.3 with `celclassic`, 16.3
  at denoise 0.6) against v3's 21.3 -- the anime frames' soft shading
  over the game's hard light and shade. The owner: "I don't like the
  way it keeps turning rolfs neutral straight lip expresion into a a down
  turned lip like like a frown or scowl"; "we probably need to train more.
  Don't forget that we also need to train for map sprites" (v4 failed on
  Rolf's walking sheet); and of a round of renders, "it failed pretty bad
  on things like the Doctor, the Clone Labs Clown all of the Dezorians.
  ... Perhaps we need to train oon insects , aliens, elderly, clown ma,
  figure poses etc?"
* **The renders between the runs** (C:\AI\tools): v5_pool.py draws a pool
  of renders of the game's pictures and v5_pick.py's page takes
  The owner's picks, favourites and notes on them, round after round;
  hd_fine.py's guide draws the game's ink as thin lines, not each native
  pixel's 9 x 9; recolour.py draws a palette-swap family (fire ant,
  armorant, pinchant) once and recolours it for the others, where separate
  renders gave three different creatures; hd_twopass.py draws a portrait's
  figure and background apart; cel_flatten.py and cel_flatten2.py (the
  render snapped to the sprite's own colours) were tried and left. None of
  it is in hd_generate.py: offered, not asked for.
* **v5** (psclassic_v5). What v4's renders failed on: gather_subjects.py
  (Danbooru, a subject with a period tag, 300 posts a search at most,
  general and sensitive ratings only), a picker, subjects_set.py: 165
  favoured (four times) and 193 kept (once), the insects, monsters and
  aliens the owner marked. The captions (v5_set.py): `chibi` taken out
  of 347, and 1,069 of PS2's in-game pictures described (ps2_subjects.py)
  where they were only named; the in-game samples down to 2,000 and 600 an
  epoch. And the owner's picks of the renders, under the word `pshd`:
  232 picked (eight times) and one favoured (16 times) -- about 21% of an
  epoch's 8,977 samples (v5_set.log; the session counted 40%, the renders'
  mask files among them), each seen 24 times. 6h50, to the end; no _e1 or
  _e2 kept.
  The next round of renders was drawn with v5, and an A/B of 40 (ten
  subjects, two seeds, two settings; C:\AI\logs\ab_v4.log, ab_v5ps.log)
  set it beside v4: neon colours, rainbow halos and swirls, whichever
  trigger word it was given. The owner: "Some of the images have
  degenerated into a psychadelic mess diverging from the ones which I
  thought were ok", and "Probably leave out our renders for now until they
  are excellent." A LoRA taught on its own model's drawings drifts; the
  renders went back to v4.
* **v6** (psclassic_v6). The owner, after the subject picker: "not
  everything should come from Anime sources. monsters should be based off
  of things from nature, like crabs and insects. No cute anime zombies.
  Zombies from horror and movies. real cats and birds. Movies like Cat
  people for cat women." -- and octopus, squid, nautilus and Cthulhu for
  the tentacles, and Nausicaa's Ohmu. gather_real.py (Wikimedia Commons'
  photographs and art, film-still sites), clip_filter.py (CLIP's zero-shot
  match sets the off-subject pictures aside, in
  C:\AI\datasets\web_subjects_off: most of a horror film's frames are
  people and rooms), subjects_set.py with the
  pictures left unmarked kept: subj_fav 1,120 (1,500 samples an epoch),
  subj_keep 1,921 (900), subj_real 3,037 (850: photographs of beasts,
  insects, sea life, plants and fossils, Haeckel's plates, kraken art, and
  creature, zombie and Cat People film stills); the photographs captioned
  `no humans`, each naming its subject. v5's other concepts as they were,
  the renders out. About 9 hours with the caching, to the end. (The
  caching crawled for an hour and a half and picked up the moment a remote
  desktop session closed; connected again later, it kept its speed: so
  something running in that session, not the connection. Not found.)
  No psychedelic drift in the samples, and the faces more 1980s. A round of
  renders set v6 beside v4. Monsters drawn at 620-790 pixels came out
  in rainbow and fishnet patterns; drawn at twice the size, not.
  The owner: "There's a lot of promising stuff. The back grounds sort of
  need a pixel by pixel interpretation I'm guessing. ... The "catman" has
  degenerated into rainbows for wome reason. ... Also there are other
  examples of dezorians in the phantasy star 4 images."
* **v7** (psclassic_v7). The owner: "Look in the PS3 and Ps4 images as
  there is some overlap between monsters", and in PS1's. v6 had learnt
  those drawings under the tagger's words (`frog`, `food`, `pokemon
  (creature)`): 86 of III's and IV's captions now carry the PS2 monster's
  description (IV's Dezolians among them; PS2's Dezolians are `dezolian`
  now), 41 of I's moved to the favoured in-game pictures, 1,155
  descriptions in all, the owner's notes on the last round written into
  them. v6's settings and concepts otherwise. 10h37 with the caching
  (stalled for about two and a half hours; not found), to the end; only the
  final file kept.
  A round of renders set v7 beside v6: almost the same at the close
  setting; at the freer one v7 took some of the notes, and the Poisoner
  and the Catman broke. The owner: "I don't know why you keep adding a
  little green gem to the ants butt. Not a Fan of the direction things are
  going. Things look sketch like, not technical. ... Some serious shape
  degeneration going on." The session put that down to its own prompt
  words (`gem`, `blush`, `grotesque`) at the freer setting. Of the next
  round, idle and attack frames in pairs: "Great the general consistency
  is good even though it looks like a rough sketch."
* **Not measured:** after v4, no run's lightness gap or Delta E.

## After v7 (to 2026-10-28)

* psclassic_v7 is the LoRA in use. v6 was left out of the rounds after (it
  draws as v7 does), and the main LoRA has not been trained again.
* **Monsters:** the owner, 10-24: "The monsters need to be photo
  realistic not flat cartoon artt. The color bands should be a clue to the
  volume of the image." Since then they are drawn on Juggernaut-XL v9
  (RunDiffusionPhoto v2), a photographic SDXL model, with psclassic_v7 at
  half strength on top (v5_pool.py's MON_CHECKPOINT and MON_LORA_STRENGTH;
  its comment: "Illustrious makes airbrushed toys whatever the words"); the
  portraits and map sprites keep the cel look.
* **Animation:** the monsters move, so their frames are drawn as a set
  (anim.py; from 10-26 its "steady" way: on most of the 22 monsters the
  flicker between frames fell from 6-38% to 1-8%). mon_lora.py (one
  monster's own LoRA on top of v7) and its set were made and never
  trained. Tracing templates in C:\AI\tracing take the owner's own
  sketches; 63 frames of 19 monsters are listed for sketching. IP-Adapter,
  tried on 10-27, was turned down on 10-28 ("No Stop it sucks."): the
  Catman's blush drawn as eyes, the Pug's teeth wrong.
* Nothing of it is in the game, and nothing in `assets/art_hd/`.

