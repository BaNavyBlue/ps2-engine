# Languages (builds 163-165)

The owner, before build 163: "I'm interested in adding language options to
phantasy-star-2 one of the options would use his new font and
retranslation. i will probably look for the Japanese Rom as well so we can
add Japanese language as well. These should be selectable in a languages
menu." "His" is Excalibur_Z's *Phantasy Star II: English Retranslation*
(the ps2-retranslation repository): a new English script made from the
Japanese one, with a proportional font, built as a patch over the same
disassembly the port reads. Asked how much of it to bring in -- his build
also has Phantasy Star IV's menus, shops, battle commands and macros and
FlamePurge's rebalance -- the owner chose "Text and font only, keep the
port's gameplay for now".

The plan, a build a step: (163) the languages, LANGUAGE on the Escape menu,
his script and face in every message window; (164) his face in every other
window -- menus, lists, labels, profiles -- and his names there in full;
then Japanese, once the Japanese cartridge is to hand (its font, character
map and window art come from it; its text is in his files already).
The owner put the Japanese cartridge's image in ~/GIT/PSII (ps2jp.zip,
outside the tree): (165) JAPANESE.

## Credits

RETRANSLATED is Excalibur_Z's work: *Phantasy Star II: English
Retranslation* (github.com/ExcalZ/ps2-retranslation, read at commit
061e1f5) -- the script made anew from the Japanese, the names and the
glossary they follow, and the proportional font, the mixed-case face of
his Phantasy Star III and IV retranslations. The port's layout for his
longer words -- the wide battle box, the wider technique windows,
six-letter names -- is his engine's (`battle_box`, `wide_techs`,
`long_names`), redrawn here. His own credit: "Translation, hacking and
testing by Excalibur_Z, with Claude (Anthropic) for translation and
technical work." His text and font are MIT-licensed; the notice is
`assets/lang/retranslation/LICENSE.txt`, which `make dist` puts in the
package's `licenses/` (`retranslation-LICENSE.txt`) and the player's
README.txt names. He built it over lory90's Phantasy Star II disassembly,
the one the port reads too.

What here is not his: the port's code that draws it, the import, and the
two shop lines written for the original game's flow (0604, 0704, below).

JAPANESE is SEGA's: the Japanese cartridge's text and font, the text as
his tools decoded it from that cartridge and matched it to the US script
ids (his `jp` fields), the font and character map read from the cartridge
by the port's import ("Japanese", below).

## What a player sees

The Escape menu has LANGUAGE, above QUIT (where GAMEPAD went in build 160,
so the rows routes count to keep their places): ENGLISH 1989, the game as
the cartridge has it, RETRANSLATED, or (165) JAPANESE (below, "Japanese").
C, left and right step through them. The game takes a new one up at its
next tick, and (166) what is on the screen with it: every window built
again in the new language, a message typed again from its start in it
(below, "Changing language"). settings.json keeps it as `"language": "us"`,
`"retranslation"` or `"ja"`; a file from before 163 has
none, and reads as ENGLISH 1989. The Escape menu itself is drawn in the
stock font in every language.

In the retranslation (builds 163-164):

* Every message is his: the dialogue windows, the big window, the battle
  box, the game over message, the ending's pages and closing line, and the
  attract crawl -- in his proportional face, a glyph a frame as the stock
  text types a cell a frame, the line breaks where he put them.
* The names a message writes are his, whole: `{item}` "Laconia Slicer",
  `{tech}`, `{enemy}`, and the party: Eusis, Nei, Rudger, Anne, Huey,
  Amia, Kainz, Silka (up to six letters).
* The battle box is his `battle_box`'s: the dialogue window's width, 24
  cells at columns 7-32 (the stock box is 20 at 9-30), and two lines -- the
  script window's art two rows higher -- when a script queued as it opens
  breaks its line, one line otherwise. His battle text is written for it
  (160 px a line would not hold it). A `{C6}` message in the two-line box
  stays up two seconds, a second a line, as his.
* (164) Every other window's words are his too, in his face: the windows'
  labels (Items, Status, Techniques, Strength, Equip -- the original's
  order; Use, Give, Drop; Buy, Sell; the house's, the tower's, the
  library's, the hospital's options; the stat and equipment labels; the
  title's Start a New Game), the profiles' five lines, the item,
  technique, enemy and party names in every list and box, the towns, the
  songs, the jobs, the party list's prompts ("Who?", "To whom?") and the
  enemy box's Heal and Miss. Numbers stay the stock digits where the game
  writes them.
* (164) His layout where his words need it: the technique lists (the
  field's, the battle's, the casting plate) a cell wider and STRNG's two
  lists two wider and moved left to columns 8 and 24 (his `wide_techs`: 48
  px of name; Saschnella and Nasaresta are past the stock 40), and the
  main menu a cell wider ("Techniques" is 46 px).
* (164) Names of up to six letters on the name board (his `long_names`),
  for the hero, a renamed recruit and a Data Memory label.
* Stock still, in every language: the name board's letters and the name as
  it is typed (its cursor steps a cell a letter), the small-capital NEXT
  on the lists' borders and the hp/tp/lv of the stat boxes, the battle
  party boxes' HP and TP (their status icons are written over them), and
  the enemy names' places in their boxes (his `battle_name_panes` centres
  them, which needs his damage pop-ups).

## The files

    assets/lang/languages.json           the languages, in the menu's order
    assets/lang/retranslation/
        lang.json                        proportional, the face, its advances
        font.png                         the face: a tile a script byte
        names.json                       the names and labels drawn
        script/Script_*.txt, script.json his script, in the port's notation
        LICENSE.txt                      his MIT notice, which travels with it

`languages.json`'s first language is the cartridge's: the game's own files
under `assets/`, which nothing here changes. Each other is the folder
`assets/lang/<key>/`, whose files stand in for the game's own of the same
path -- `assets/lang/retranslation/script/Script_People.txt` for
`assets/script/Script_People.txt` (`lang_path`, src/game/lang.c) -- and a
file it does not carry is the game's own. A language's files are assets
like any other: in the manifest, the pack, and the mods folder (a mod of
`lang/retranslation/script/Script_People.txt` is read in its place, and a
broken one set aside as any is, docs/mods.md). A new language is a folder
and a line in `languages.json`.

`lang.json`: `"proportional"` (the face is drawn a glyph at its own
advance, not a cell a byte), `"font"` (the face's tile sheet, 16 x 16 tiles,
tile N the glyph of script byte N, index 1 ink and 0 paper), `"advance"`
(256 pixel advances, ink + 1; space 3) and `"charset"` (the letters past
ASCII its names write, by script byte: `…` $47, `Ä` $48, `ä` $49, `▶` $4A).
The script files write those bytes as `{47}`, as the stock files do.

`names.json`: `characters` (the default party names, by place), `items` and
`techniques` (by their keys in items.json and techniques.json), `enemies`
(by battle.json's label), `songs`, `towns`, `jobs`, `prompts`, `labels`, and
`windows` (each window map's label rows, by its art name).

## His text, converted

`tools/import_retranslation.py RETRANSLATION` writes the folder from a
checkout of his repository (163: commit 061e1f5). It reads data only --
`work/dialogue.json`, `work/script.json`, `LICENSE`, and the glyph table of
`tools/vwfmixed.py`, parsed with `ast`, never imported -- so nothing of his
runs. His notation maps one to one onto the port's: `{BR}` a line,
`{PAGE}` `{pause}` (the loader adds the line break after it, as the ROM's
RunScript does), `{CLR}` `{cont}`, `{END}` `{C5}` `{C6}` `{C7}` the end codes,
`{NAME}` `{NAME2}` `{ENEMY}` `{TECH}` `{ITEM}` `{MESETA}` the inserts.

**The proof of the mapping:** `--check` converts his `us` fields -- the
1989 text, as his tools read it from the ROM -- the same way, and every one
of the 26 banks comes out byte for byte the port's own
`assets/script/Script_*.txt` (the dead bytes after four end codes, his
`tail`, included).

His script and the port's line up exactly: the same 26 banks and 611
script ids, an id `bank << 8 | n` on both sides. A block his file gives
several ids (a stock bank's zero-length steps) has the same text at each,
as the port's files repeat it. His seven added messages (0029, 002A, 0811,
0812, 0C09, 122B, 122C) are for his engine's macros, shop and teleport
flow and status messages; the port's events never ask for them, and they
are left out, so every bank keeps its count.

His face is his `vwfmixed.py` table plus `diafont.py`'s two glyphs (the
ellipsis dot and the arrow), at `diafont.py`'s advances; the import checks
the build against the copy his proofreader embeds (`DIAFONT_B64`,
`DIAWIDTH_B64`): the same.

### For the original game

A few of his lines are written for his engine. ORIGINAL_GAME in the
import gives those spots the original game's:

* **0604, 0704** (the weapon and armour sellers, after a NO to "Are you
  sure?"): his shop flow goes back to the list, and he reworded them for it
  ("Then, how about something else?"). The original flow asks who will
  carry it again -- the JP じゃ、だれが もつんだ? ("Then, who'll carry it?")
  -- so the port's lines are **written for the port, not his**: "Then
  who'll carry it?" (the weapon seller, after his 0602 "Who'll carry it?")
  and "Then who's gonna carry it?" (the armour seller, his 0701's voice).
* **Items $44, $5D, $6E**: his rebalance's Cyber Vest, Silver Claw and Wave
  Shot are the original game's Heilsam Boots, Whip and Bowgun -- his
  glossary's names for them.
* **The main menu** (164): his labels are Phantasy Star IV's order and
  entries (Items, Techniques, Status, Equip, Macro); the original's ITEM
  STATE TECH STRNG EQP, the JP アイテム じょうたい テクニック つよさ そうび, in
  his words: Items, Status, Techniques, Strength, Equip.
* **Amia's profile** (164): "She fights with slicers and knives" is the
  rebalance's (whips made claws); the JP スライサーやムチ is slicers and whips.

Two of his structural changes are his text's own and stay: 010B (the
wound touched) runs on into 010C (the poison not healed) as the Japanese
does -- the US 1989 text ends it and loses the second line -- and 0D05 ends
where both cartridges run on into the Room bank's table bytes (the port
already ended it there).

### Where a message ends (build 168f)

The event code asks for the US ids, and a message is the id asked for and
those after it up to the first end code (`expand_script` runs on through
an entry with none, an empty one too). So a language's message must end
where the US one ends wherever the event code then asks for the next id
itself -- the Governor's briefing and Lutz ask for two in a row
(building.c's `win_script2`, `win_script_set2`) -- or that one is shown
twice. `tools/script_ends.py` is the rule: for each US end code, the
language's text that covers it (its own entry, or the last one with text
before it in the same US message) takes that end code, in place of a page
break it ends with. Both imports apply it to what they write (the
Japanese: $1101 and $190B; his text needs nothing); 168f applied it by
hand to the files already written, editing only those two entries, so the
rest are byte for byte the imports' as before. One run-on is a language's
own and is left (`RUN_ON`): 010B, which the healing technique asks for
alone on a poisoned member (TechEffect_Res, ps2.asm:15266), above.
`python3 tools/script_ends.py --check` holds every language's files to
it: PASS; on 168e's files FAIL, 2.

## How the face is drawn

**Runs** (window.h's WinRun). A proportional language's text is not cells
a byte: each line is a run -- its glyph codes, the interior row and first
column, the cells it may cover -- kept with the window entry. RunScript's
step (`run_script_step`) appends a glyph to the line's run a frame, where
the stock step writes the glyph's two tiles; the upper (dakuten) row stays
paper, as in his engine. A line break, a scroll (`text_scroll`: a line's
run moves up with its cells) and a clear (`text_clear`) treat runs as the
cells. `text_col` still counts glyphs, which is all the rest of the window
system reads of it.

**The text pool.** win_render draws each run every frame into tiles of a
pool: `g_vram` is 3072 tiles now, and $800-$BFF -- past the VDP's 2048,
which no screen's art reaches -- are the pool (`win_text_pool`, reset at
the start of each frame's drawing in game_render). A run takes the cells
its glyphs reach (his engine gives only the cells with ink a tile), each
cell's tile copied as the paper -- the window's fill ($B), or the cleared
fill on the game over and ending screens (transparent), or the fill where a
frameless text's cells were never written -- and the glyphs' ink drawn over
in colour 1, then the cells pointed at those tiles with the window's
palette line and priority. A glyph that would cross the line's end is
dropped, and the rest of the line with it (his rule). The compositor needs
nothing new: the pool's tiles are tiles, indexed, so the palette path, the
colour path and colour_check see them as any.

**Elsewhere:** the attract crawl draws its own box (draw_message): each
line a run on the box's fill (`win_text_draw`). The battle box above.

**Names in messages.** `{item}` and `{tech}`: `gamedata_item_text` and
`gamedata_tech_text`, the language's name or NULL; `{enemy}`: the WinSys
hook `enemy_text` (battle.json's label, `battle_enemy_label`); each is
written whole in the language's letters (`lang_codes`), not cut to the
stock ten or five. `{char}`: the party's names, which are up to six letters
now (`GD_NAME_MAX`; the save's raw `$C600` block keeps the first four, as
the cartridge's, and its `"name"` the whole). A name that is any
language's default for that character -- the cartridge's (characters.json)
or a language's `names.json` -- becomes the chosen language's default at a
new game, a load and a change of LANGUAGE (`gamedata_language_names`); a
name the player typed stays, and the letters typed are the whole of it.
Every route's save is in the cartridge's language, so none changes.

**Windows (164).** The stock way writes a name or a label into a window's
cells as glyph halves; in a proportional language each writer puts a run
instead, on the text's lower row over the stock cells, the cells blank:
`put_item_name` (ten cells), `put_char_name` and the lists' inline names
(`put_name_run`: four cells, his 32 px plate, up to six letters), the
technique lists (`TECH_CELLS`, six), the enemy boxes and groups
(`put_enemy_run`, the WinSys's `enemy_group_id` for the language's name),
the towns, songs and jobs (names.json's lists), the party list's prompt
(on its bottom border: a run on a border row is drawn as the border's
cells are, from the font) and Heal and Miss. A label of the tile charset's
two halves ("    Who?", "  LV") is its lower half when the upper is blank,
else one run ("To whom?", which his engine draws whole on the bottom row).

The window maps' labels (`lang_labels`, at build_entry): each map row with
letter tiles ($27-$62) is a label, in order, and names.json's row of the
same place takes its letters' cells -- drawn from the first letter's cell
over the cells to the next thing the row holds (a number's placeholder,
the border) -- when the counts agree (a window whose rows and his do not
agree keeps the stock labels, said on stderr). The profiles take his five
lines on rows 2-10; the name board only its last row's words, each at the
stock word's place. `widen_for_language` first makes the windows his
layout widens a cell or two wider, the new columns the map's last.

**The name board (164)** takes six letters in a proportional language
(`WIN_NAME_MAX`), and `gamedata_set_name` makes a name of what was typed
for the new game's hero, a renamed recruit and a Data Memory label (the
slot label, `save_name`, is six bytes now as a character's; the raw block
keeps four). The board's own letters and the name as typed stay the stock
tiles: its cursor steps a cell a letter.

## Checked

* `tools/import_retranslation.py --check`: 26 of 26 banks the port's own.
* Routes: `lang_menu` (the row, round both ways), `lang_shop` (a message
  and a whole item name), `lang_battle` (the wide box, the default name),
  `lang_names` (the names back to the cartridge's), `lang_new_name` (a
  typed name the whole name), `lang_gameover`,
  `lang_crawl`, `lang_ending` (frameless text, the pages and the closing
  line); (164) `lang_menu_pages` (the C menu: the widened main menu,
  "To whom?", the wide technique list, the Strength page), `lang_battle_lists`
  (the enemy's name, Nasaresta whole in the sixth cell), `lang_house` (the
  profiles, jobs and options; Amia's whips), `lang_places` (the teleport
  towns), `lang_six_letters` (ROLFIE on the board and in the dream). The
  pixel checks are where his glyphs have ink and the stock font's have
  paper.
* `tools/lang_check.py`: the headless game -- LANGUAGE chosen on the menu
  written to settings.json, the same frame read back from the file, a key
  no language has (said; the stock), a broken mod of his `names.json` (set
  aside; his own read).
* The cartridge's language unchanged: every proof the same but the Escape
  menu's main page, which has the new row (docs/history.md, Build 163);
  (164) every proof the same as 163's.
* (build 168f) The whole game in each language, seeds 1-12
  (`PS2_TEXT_CHECK=1 tools/playthrough.py --seeds 1-12 --language KEY`):
  all 36 to the ending, and the text check (docs/harness.md) silent -- no
  line, name or label too wide for its window, no empty message asked
  for. `tools/script_ends.py --check` PASS.

## Changing language (build 166)

The owner, after 165: "It does have some glitches when switching between
languages." What was on the screen stayed in the old language, and some of
it was drawn in the new one's face: a message being typed went on typing
the old language's codes (US English's in the Japanese face: nonsense
kana), the open windows kept their old words (his names, runs of his face,
blank in another language's), and the attract crawl's box was gone for the
rest of the crawl (its script dropped with the banks and never read
again).

Now `take_language` ends with `win_language_changed` (window.c):

* Every open window, painted ones too, is built again in place from its
  map and the game's state (`rebuild_entry`: build_entry into a copy, the
  cursor, the place over the stack and the tile base kept) -- labels,
  names, lists, widths in the new language.
* The text window's script is copied again in the new language
  (`expand_script`, split from load_script: through `script_text`, which
  logs nothing, the event code's flags and queues left alone): typed again
  from its start while it is still being shown, or, finished, shown whole
  at once to its end code (its last page). The battle box takes the lines
  of its language's box.
* (build 168d) Only while that window is open: closing it takes its
  script with it. The text's place in the stack stays where the closed
  window was, as the ROM's does, and the next window opened there is not
  a text window -- the select screen's NEW GAME / CONTINUE / ERASE GAME,
  opened where the backup check's messages were. Until 168d a change of
  language there typed "Data check complete." into the menu, over its
  first two rows and their lamps, the cursor lit on a blank row
  (The owner: "The flashing red cursor floats over text with no
  perimeter box").
* An open name board becomes the new language's, its cursor at the first
  stop; a name begun on the old board goes (its codes were the old
  language's).
* The crawl reads its script again (once a language) when it finds it
  gone.

(build 168e) COMPUTER PLAYS in every language: its new game answers the
opening's pages until the street instead of counting the cartridge's
English (the retranslation's Governor has more pages; at AUTO the
computer stood before him at 60 Hz), and on the Japanese name board ends
a blank name at オワル, at the new game and the Data Memory's saves
(docs/computer_plays.md). (build 168f) Its new game names the hero ROLF
in ENGLISH 1989 alone; in another language it ends a blank name, his
default standing. ROLF typed in the retranslation is the cartridge's
default, which a save loaded later makes his (read_char, as at a change
of language): until 168f COMPUTER PLAYS's retranslated hero was ROLF in
the dream after Nei and Eusis at Dark Force. A player who types ROLF
there meets the same; that is build 165's rule for names across
languages, left as it is.

Not run by a route: his two-line battle box changed to the stock one (the
two-line messages come from status effects and the Nei Sword, which no
short route reaches); the code takes the stock box's one line there.

## Japanese (build 165)

JAPANESE is the Japanese cartridge's text in its own font: *Phantasy Star
II: Kaerazaru Toki no Owari ni* (1989), the image CRC32 BEC8EB5A in its
first 768 KB (The owner's ps2jp.zip, from Phantasy Star Cave, a 1 MB
overdump; kept outside the tree, as the US ROM is). The port's play is the
same in every language; in this one, every word the game draws is the
cartridge's, in its font, with its dakuten and handakuten.

(build 186) A copy of the game without `assets/lang/ja/` offers to make
it when LANGUAGE closes on JAPANESE, or settings.json names it: a window
takes the Japanese cartridge and the retranslation (a checkout, or
GitHub's "Download ZIP", whose comment gives `--commit` below) and runs
`tools/make_assets.py` on them (docs/first_start.md).

**Where it comes from.** `tools/import_japanese.py JP_ROM RETRANSLATION`
writes `assets/lang/ja/` ((build 191) and its twin in C, `./ps2_extract
japanese JP_ROM RETRANSLATION`, the same files: docs/first_start.md, "In
C"):

* The font: FontsIconsArt, at $26800 in the JP image ($29EB8 in the US;
  the five `lea (FontsIconsArt).l, a0` of the US at $73CC, $75BE, $853C,
  $87E6, $893E are at $723A, $742C, $83AA, $8654, $87AC there). Its sheet
  has the kana where the US one has the Latin letters -- and where the US
  one has its status icons and its second set of digits, so it is not put
  in the US sheet's place: the US sheet stays at $500 for every icon,
  digit and frame the port's code draws, and the kana are a face.
* The character map: VDPCharacterMaps at $129C8 (the US $12BC8), a code's
  upper tile -- the dakuten $5B, the handakuten $5C or none -- and lower
  tile. With his tile-to-kana table (tools/ps2text.py's `_JP_TILES`, "read
  off the JP font sheet") 187 codes are characters: the hiragana and
  katakana and their voiced forms, ヴ ($C2), the digits, the capitals
  A D E H L M N O P S T V W X Y, and 、。・「」ー!?/.
* The text: his `jp` (work/dialogue.json, work/script.json), the
  cartridge's text decoded by his tools and matched to the US script
  ids. Where the US split a message in two (22 of them), the Japanese is
  on the first id and the second is empty, so the first runs on through it
  as his notes say. (build 168f) Each message ends where the US one does
  ("Where a message ends", above): twice the US ended a message at the
  empty id and the event code asks for the next one itself, so the
  Japanese showed it twice -- the Governor's mission ($1101 on into $1105)
  and Lutz's second half ($190B on into $190E). 0D05 ends before the bytes
  both cartridges run on into (the Room bank's table and its greeting), as
  the port's US ends. The library's list drops its title row (リスト) the
  US window has not got.
* The name board: its art (window $24, the WindowArtLayoutPtrs table at
  $13568 in the JP image) gives the six rows of katakana and their places,
  written into names.json's `board`.

**The face** (lang.json `tall`, `font_upper`): a code's glyph is its lower
tile, eight pixels, and over it, in the row above, its upper tile -- the
cartridge's own layout, which the window system's two rows a text line
already keep (the US leaves the upper row blank). Runs draw it (window.c's
`draw_upper`): the marks only in the cells that have ink, so a row above
that holds something else shows through. The crawl's lines are two rows
apart in this face (the stock crawl's one).

**Its charset** (lang.json `charset_full`): every character, ASCII too, is
the cartridge's code -- its "0" is not the script charset's. The script
files are UTF-8 kana, and `load_script` reads them through it.

**Its layout:** the stock one. His layout flags (lang.json `layout`:
`battle_box`, `wide_techs`, `wide_menu`, `long_names`) are the
retranslation's alone; the Japanese words are the windows' own (the US
windows are the Japanese ones with English in them), names four letters,
the battle box the stock 20 cells.

**The name board:** the cartridge's -- six rows of katakana at the art's
places (rows 5-15, two apart), every cell a stop but the blanks, the last
row's ススム, モドル, オワル advance, back and end. The cursor moves as the
port makes it (`board_move`: left and right to the row's next stop round,
up and down a row round, to the column or the nearest stop): the
cartridge's own movement is not read. The name as typed is drawn in the
face.

**Names across languages.** A name is codes of the language it was made
in, and the Japanese cartridge's are not the script charset's. So a name
goes across LANGUAGE as text (`gamedata_names_text`, `_set_text`): a
default of any language is the new one's (ユーシス, Eusis, ROLF), a typed
one is its characters in the new language's codes -- a character it has
none for a space (a name typed in kana is blank in English). Saves keep
names as the language's text (UTF-8); a save's default name is the
chosen language's before it is made codes.

**Credits.** The Japanese text and the font are SEGA's; the text as
Excalibur_Z's tools decoded and aligned it (his `jp`, MIT), and his
tile-to-kana table. `assets/lang/ja/NOTICE.txt` says so.

**Checked:** routes `lang_ja_shop` (a message and the store's list: the
dakuten over だ in the line's upper row), `lang_ja_crawl` (two rows a
line, the mark over ゴ), `lang_ja_pages` (the C menu's labels, the mark
over じ of じょうたい; だれが?, だれに?), `lang_ja_name` (アイウ on the
board, オワル, the dream's "ぼくの なまえは アイウ。"), `lang_ja_names`
(ユーシス across to Eusis), `lang_menu` (round through three);
`tools/lang_check.py` (settings.json's "ja": its own crawl). The probe's
`expect text` reads the language's charset.
