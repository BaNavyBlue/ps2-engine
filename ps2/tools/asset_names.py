"""Friendly names for the assets whose ROM label or file name says nothing
(build 97b), in one place: every extractor that writes or names one of these
files takes its name from here, and tools/gen_rom_ids.py writes the palette
names into src/game/rom_ids.h as PALETTE_*.

Where a name was worked out, the evidence is beside it. The ROM's label stays
in the JSON the extractor writes ("source"), for checking against ps2.asm.
"""

# PalettePtrs (ps2.asm:26819), by index: assets/palettes/<name>.png. The
# constants file names the portraits (pal_id_*); the rest are named by who
# loads them -- the level records (levels.json "palette"), the ending's page
# table, and the PaletteLoad1/2 calls ported in game.c, building.c and
# ending.c.
PALETTE_NAMES = {
    0x00: "sega_logo",                 # Pal_Sega
    0x01: "title_screen",              # Pal_TitleScr; the ending's title too
    0x02: "field_common",              # loc_11D04: lines 0-1 on every level, the windows' colours
    0x03: "level_motavia_outside",     # level records
    0x04: "level_dezolis_skure",       # SkureB2, SkureB1, DezolisSkure
    0x05: "level_motavia_towns",       # Paseo, Arima, Oputa, Zema, Kueri, Piata
    0x06: "level_uzo",
    0x07: "level_underwater_passage",
    0x08: "unused_08",                 # loc_11F04: no level record, table or PaletteLoad names it
    0x09: "level_tubes",               # the four tubes
    0x0A: "level_blue_dam",
    0x0B: "level_yellow_dam",
    0x0C: "level_green_dam",
    0x0D: "level_red_dam",
    0x0E: "level_shure_nido",
    0x0F: "level_climatrol",
    0x10: "level_roron",
    0x11: "portrait_rolf",
    0x12: "portrait_nei",
    0x13: "portrait_rudo",
    0x14: "portrait_amy",
    0x15: "portrait_hugh",
    0x16: "portrait_anna",
    0x17: "portrait_kain",
    0x18: "portrait_shir",
    0x19: "portrait_librarian",
    0x1A: "portrait_data_memory_clerk",
    0x1B: "portrait_doctor",
    0x1C: "portrait_clone_lab_grandma",
    0x1D: "portrait_item_seller",
    0x1E: "portrait_weapon_seller",
    0x1F: "portrait_armor_seller",
    0x20: "portrait_ustvestia",
    0x21: "portrait_dezolian_1",       # pal_id_misc1_port .. misc4 (portraits.json: Dezolian1-4)
    0x22: "portrait_dezolian_2",
    0x23: "portrait_dezolian_3",
    0x24: "portrait_dezolian_4",
    0x25: "portrait_item_keeper",
    0x26: "portrait_central_tower_outside",
    0x27: "portrait_central_tower_outside_copy",   # the same Pal_CentTow1Port
    0x28: "portrait_governor",
    0x29: "portrait_roof_spaceship",
    0x2A: "portrait_central_tower_outside_2",      # Pal_CentTow2Port, also Tyler's spaceship
    0x2B: "portrait_library_graph",
    0x2C: "portrait_radar",
    0x2D: "portrait_radar_copy_1",     # the same Pal_RadarPortrait
    0x2E: "portrait_radar_copy_2",
    0x2F: "battle",                    # loc_11D44: the battle screen's four lines
    0x30: "dream",                     # loc_11CE4: the dream's line 3 (loc_D59C)
    0x31: "portrait_teleport_clerk",
    0x32: "library_graph_overlay",     # loc_12744: PaletteLoad2 after "overlap the graphs" ($1012, loc_C4B2)
    0x33: "level_biosystems_lab",
    0x34: "level_control_tower",
    0x35: "nei_death_picture",         # line 1 with NeiDeathTileInd (loc_7E02 case 1)
    0x36: "nei_body_picture",          # line 1 with NeiBodyTileInd (case 2)
    0x37: "level_crevice",
    0x38: "level_gaira",
    0x39: "flight_from_dezolis",       # loc_C58A: leaving DezolisSkure, line 3
    0x3A: "flight_from_motavia",       # leaving Motavia, line 3
    0x3B: "portrait_lutz",             # Building_EsperMansion, LutzPortraitTileInd
    0x3C: "flight_spaceship",          # the flight's line 2
    0x3D: "level_esper_mansion",
    0x3E: "level_noah",
    0x3F: "level_naval",
    0x40: "level_menobe",
    0x41: "level_ikuto",
    0x42: "level_guaron",
    0x43: "unused_43",                 # loc_12884, the same data as ending page 1's; nothing names $43
    0x44: "ending_page_1",             # ending.c's page table
    0x45: "ending_page_2",
    0x46: "ending_page_3",
    0x47: "ending_page_4",
    0x48: "ending_page_5",
    0x49: "ending_page_6",
    0x4A: "ending_wipe",
    0x4B: "ending_last_picture",
    0x4C: "level_dezolis_towns",       # Aukba, Zosa, Ryuon
}

# The level tilesets (levels.json "tilesets"), by index: their tiles
# assets/tiles/tileset_<name>.png and blocks assets/levels/blocks_<name>.txt
# (and .png, the 32x32 view the Tiled maps use). Named by the levels on them.
TILESET_NAMES = {
    0: "motavia_outside",
    1: "dezolis_skure",                # SkureB2, SkureB1, DezolisSkure
    2: "towns",                        # both planets' towns
    3: "caves",                        # Uzo, the Underwater Passage, the Crevice
    5: "motavia_dungeons",             # the tubes, Shure, Nido, Roron, the dams, the lab, Climatrol, the tower, Gaira
    6: "dezolis_dungeons",             # the Esper mansion, Naval, Menobe, Ikuto, Guaron, Noah
}

# The level sprites' art (the art lists, level_sprites.json), by ROM label:
# assets/tiles/sprite_<name>.png. Named by the object types whose tiles the
# art is placed at, in the levels that use each list.
SPRITE_ART_NAMES = {
    "loc_5814A": "motavian_young_man",
    "loc_58646": "motavian_young_woman",
    "loc_58A5C": "motavian_old_man",
    "loc_58E04": "motavian_child",
    "loc_59272": "dezolian",
    "loc_59A10": "roron_mota",
    "loc_59FB6": "musk_cat",
    "loc_5A410": "darum",
    "loc_5A886": "teim",
    "loc_5AE9C": "explosions",         # tile $400 for RandomExplosion ($2D) and the burst ($2B)
    "loc_5B452": "dead_body",          # Shure F3's bodies: one 32x16 piece, 8 tiles
    "loc_5B5B4": "esper",
    "loc_5BCA2": "esper_mansion_b1_pieces",   # objects $23-$26, B1's scene
    "loc_5C366": "esper_mansion_b1_mover",    # object $27, the one that ends in Lutz's room
    "loc_5C67E": "esper_mansion_door",
    "loc_5C900": "mother_brain_image",        # her image and the energy arcs ($2C, $45)
    "loc_5D05E": "neifirst",
    "loc_5D12C": "gaira_ring",
    "loc_5D1A8": "dungeon_door",       # OpenableDoor $3B, DestroyableDoor $3C
    "loc_5D560": "dezolis_chest",      # DezoTreasureChest $39
    "loc_5D710": "motavia_chest",      # MotaTreasureChest $3A
    "loc_5D98E": "jet_scooter",
    "loc_5DFEA": "earthman_leader",    # object $28 (loc_4DD8), NoahF1 33,7: the Earthman the walk to the Earthmen stops before (build 97d)
    "loc_5E322": "pump",
}

# The level tile animations (level_anims.json), by ROM label:
# assets/tiles/anim_<name>_<frame>.png. Named by their level.
LEVEL_ANIM_NAMES = {
    "loc_760D8": "motavia_outside",
    "loc_762D8": "underwater_passage",
}

# art/*.bin file names that are abbreviations, and what the PNG is called.
ART_RENAMES = {
    "cen_tow_out_port":           "central_tower_outside_portrait",
    "cen_tow_roof_port":          "roof_spaceship_portrait",
    "cl_grandma_port":            "clone_lab_grandma_portrait",
    "item_keeper_port":           "item_keeper_portrait",
    "mot_arm_sel_port":           "armor_seller_portrait",
    "mot_doctor_port":            "doctor_portrait",
    "mot_item_sel_port":          "item_seller_portrait",
    "mot_save_empl_port":         "data_memory_clerk_portrait",
    "mot_tele_empl":              "teleport_clerk_portrait",
    "mot_wpn_sel_port":           "weapon_seller_portrait",
    "tit_scr_copyright_art":      "title_copyright_art",
    "tit_scr_phan_star_logo_art": "title_logo_art",
    "title_scr_bg_art":           "title_background_art",
    "title_scr_women_art":        "title_figures_art",
    "battle_carrier_act":         "battle_carrier_art",
}

# art/*.bin files that are tile-index maps, not art: extract_nametables.py
# reads them. Decoded as art they are noise (build 97b dropped the one that
# decoded without an error, tylr_spc_monitor_tile_ind.png).
ART_NOT_ART = {
    "lutz_port_tile_ind", "tit_scr_background_tile_ind", "tit_scr_bg_tile_ind",
    "tyler_port_tile_ind", "tylr_spc_monitor_tile_ind",
}

# (build 156) The art the pictures (assets/art, builds 142-154) replaced:
# sheets the game no longer reads at run time. extract_art.py and
# extract_inline_art.py do not write them (and delete them where they
# were); the extractors that need their tiles -- the pictures, the window
# maps' and nametables' tilesets, the art dump -- read them from the
# cartridge image (disasm_art.py; the disassembly until build 185). The art
# still loaded under a picture, for a
# sprite drawn from the same tiles, is not here: tyler_spaceship_art (the
# monitor's explosion), EndingStarfieldUfoArt (the UFO),
# battle_mombrain_art (her figure).
PICTURE_ART = {
    # The portraits' (146a) and the library graph's (146a).
    "amy_portrait", "anna_portrait", "armor_seller_portrait",
    "central_tower_outside_portrait", "clone_lab_grandma_portrait",
    "data_memory_clerk_portrait", "dezolian_portrait", "doctor_portrait",
    "governor_portrait", "graph_portrait", "hugh_portrait",
    "item_keeper_portrait", "item_seller_portrait", "kain_portrait",
    "librarian_portrait", "nei_portrait", "radar_portrait", "rolf_portrait",
    "roof_spaceship_portrait", "rudo_portrait", "shir_portrait",
    "teleport_clerk_portrait", "ustvestia_portrait", "weapon_seller_portrait",
    # The cut-scene pictures' (147-154).
    "LutzPortraitArt", "SpaceshipPlanetArt", "NeiDeathArt", "battle_floor_art",
    "EndingPicturesArt", "SegaLogoArt", "DreamWakingArt", "DreamBattleArt",
    "DreamHelpArt", "title_background_art", "title_figures_art",
    "title_logo_art",
}


# (build 136) The colour ramps, assets/palettes/<name>.png (ps2png.write_ramp,
# read by table_load_ramp): tables of CRAM words a routine copies into the
# live palette a window or a row at a time. Keyed by the ROM label; the
# value is the file's name and how many colours a row of the picture holds.
RAMP_NAMES = {
    # PaletteCycle (ps2.asm:9235) slides an 11-word window over it, a word
    # every four frames: the Sega logo's sweep. One row of 31.
    "CyclingPal_SegaLogo": ("sega_logo_sweep", 31),
    # The credits' palette fade (loc_6AE0): one row of four a step into
    # line 3, entries 9-12, from the last row (black) up to the first.
    "loc_6B10":            ("ending_credits_fade", 4),
}

# (build 136b) The drum samples, assets/music/drums/<name>.wav, by the
# label each sits at in the Z80 program's file (ps2_pcm_drums.asm). The
# drums that play them (the toms are one sample at three rates) are named
# in extract_drums.DRUMS and src/audio/drums.h.
DRUM_SAMPLE_FILES = {
    "zloc_118":  "kick",
    "zloc_299":  "snare",
    "zloc_A99":  "tom",
    "zloc_129A": "open_hat",
    "zloc_189B": "closed_hat",
    "zloc_1A9C": "unused",     # 18 bytes; drum $90 plays 513 from it
}

# (build 136c) The music tables that were music/*.bin containers and are
# text now, assets/music/<name>: ROM label -> file name.
MUSIC_FILES = {
    # GetFMFreq's table: a frequency word for each note (block and F-number).
    "FMFreqs": "note_frequencies.json",
    # (build 136d) The FM instruments and the order SetFMIns uploads their
    # registers in (loc_BA818); tables/FMInstruments.bin and
    # FMRegisterOrder.bin until then.
    "FMInsPtrs": "fm_instruments.json",
}

# (build 136a) The tables that were tables/*.bin containers and are text
# now, assets/tables/<name>: ROM label -> file name.
TABLE_FILES = {
    # The order the dissolves reveal or erase a tile's pixels in.
    "FadeEffectArray": "dissolve_order.json",
}

# (build 136e) The tables that were tables/*.bin containers and are text
# beside the window text now, assets/script/<n>: ROM label -> file name.
SCRIPT_FILES = {
    # The ending's credits, which the credits writer (loc_787E) reads a row
    # at a time; tables/EndingCredits_Script.bin until then.
    "EndingCredits_Script": "ending_credits.txt",
}

def art_name(stem):
    return ART_RENAMES.get(stem, stem)


def palette_file(index):
    return PALETTE_NAMES[index] + ".png"

# (build 137) The FM instruments' names (docs/instrument_names.md, agreed by
# The owner after build 136d), by number: what the song text calls each and
# what fm_instruments.json gives as "name". One word each (letters, digits,
# _), no two the same.
INSTRUMENT_NAMES = [
    "selection", "level_up", "item_received", "explosion",
    "level_changed", "fell_in_hole", "revived", "poison_cured",
    "alarm", "hidapipe", "teleport", "door_open",
    "dam_opened", "jet_scooter", "spaceship_departed", "spaceship_landed",
    "dangerous_floor", "musik", "boomerang", "sword",
    "claw", "slasher", "shotgun", "strong_gun",
    "effect_b8", "effect_b9", "damage_red_screen", "foi",
    "zan", "vol", "tsu", "shinb",
    "eijia", "gaj", "deban", "healed",
    "effect_c4", "neifirst_attack", "whip", "whistle_attack",
    "polezi_attack", "pulser_attack", "terakite_attack", "fire_ant_attack",
    "bee_attack", "amoeba_attack", "blaster_attack", "van_attack",
    "van_attack_2", "spinner_attack", "mosquito_attack", "locust_attack",
    "unused_52", "enemy_killed", "pause", "unused_55",
    "unused_56", "unused_57", "unused_58", "unused_59",
    "unused_60", "unused_61", "unused_62", "unused_63",
    "bass", "phantasy_mid", "phantasy_chords", "movement_chords",
    "phantasy_mid_2", "my_home_mid", "phantasy_high", "phantasy_chords_2",
    "restoration_high", "silent_zone_low", "step_up_chords", "silent_zone_chords",
    "pleasure_high", "pleasure_high_2", "pleasure_chords", "pleasure_chords_2",
    "pleasure_chords_3", "pleasure_high_3", "exciting_town_mid", "exciting_town_high",
    "exciting_town_chords", "secret_ways_mid", "secret_ways_chords", "advanced_high",
    "advanced_high_2", "mystery_high", "mystery_high_2", "mystery_bass",
    "mystery_chords", "violation_mid", "violation_high", "violation_low",
    "under_mid", "under_mid_2", "under_chords", "bracky_news_chords",
    "bracky_news_mid", "bracky_news_mid_2", "unused_102", "power_chords",
    "rise_or_fall_chords",
]

# (build 137) The four vibrato envelopes (ModEnvelopePtrs), by number 1-4: a
# track header's or a `mod` line's name for each. 1 is the melodies' (15
# songs), 2 Power's melody's, faster; 3 and 4, a scoop up into the note then
# a wobble, long and short, are named by no header and no stream.
ENVELOPE_NAMES = ["vibrato", "power_vibrato", "scoop", "short_scoop"]
