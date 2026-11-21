#!/usr/bin/env python3
"""Extract the compressed art that is not a binclude'd art/*.bin.

Not all art is a binclude. FontsIconsArt (ps2.asm:48014) holds the game's
glyphs, item icons and window frame tiles as inline bytes in the same
compressed format as the art/*.bin blobs, and so do the labels below.

(build 185) From the cartridge image (tools/rommap.py), not ps2.asm's dc.b
lines: a label's block is its bytes up to the next label, and the
compressed stream in it ends itself (ps2art.decompress), so what follows
the stream in the block is left alone, as it was.

    python3 tools/extract_inline_art.py ROM -o assets/tiles
"""

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import asset_names  # noqa: E402
import ps2png  # noqa: E402
from ps2art import decompress  # noqa: E402
from rommap import RomMap  # noqa: E402

DEFAULT_LABELS = ["FontsIconsArt", "SegaLogoArt", "loc_6D7BA", "loc_6F040",
                  "loc_6FF22", "loc_71362", "loc_71702",
                  "loc_2B1F0", "loc_7567A", "loc_AED00"]

# The 27 enemies whose art is inline rather than a binclude; the other 15
# come out of art/*.bin through extract_art.py. Named the way the bin
# stems are (battle_ant_art) so battle.json can name a PNG per enemy.
INLINE_ENEMY_ART = [
    "Battle_ArchDrgnArt", "Battle_ArmyEyeArt", "Battle_CatmanArt",
    "Battle_Cooley61Art", "Battle_DarkFrceArt", "Battle_DarksideArt",
    "Battle_DezoOwlArt", "Battle_EyesoreArt", "Battle_FireEyeArt",
    "Battle_FirefallArt", "Battle_LungArt", "Battle_MastodonArt",
    "Battle_MazgammaArt", "Battle_MechomanArt", "Battle_MetalmanArt",
    "Battle_MombrainArt", "Battle_MystcapeArt", "Battle_NeifirstArt",
    "Battle_OrangahArt", "Battle_PugHitArt", "Battle_RabbitArt",
    "Battle_TerakiteArt", "Battle_VanArt", "Battle_WirefaceArt",
    "Battle_WizardArt", "Battle_WolfangArt", "Battle_WresterArt",
]
DEFAULT_LABELS += INLINE_ENEMY_ART

# The weapon and technique effects WeaponTechArtPtrs (ps2.asm:46030) names,
# all inline but KnifeArt (art/knife_art.bin). Kept under their own labels
# because battle.json's effect_art names them that way.
EFFECT_ART = [
    "AcidShotArt", "BowgunArt", "DebanAttackArt", "EijiaAttackArt",
    "FireStaffArt", "FoiArt", "GajAttackArt", "GajCastArt", "GenArt",
    "GraAttackArt", "GraCastArt", "LacoSwordArt", "LaserBarArt",
    "LaserShotArt", "LaserSwordArt", "MegidCastArt", "NeiShotAttackArt",
    "NeiShotFiredArt", "PulseCannonArt", "SakCastArt", "ShiftAttackArt",
    "ShiftCastArt", "SlasherAttackArt", "SlasherThrownArt", "SteelBarArt",
    "SwordArt", "TsuCastArt", "ZanAttackArt", "ZanCastArt",
]
DEFAULT_LABELS += EFFECT_ART

# BuildingScreen's Esper mansion case (loc_8364, ps2.asm:11915): Lutz,
# decompressed to VRAM $0020 (tile 1) under LutzPortraitTileInd (30x14 at
# $410A0003: plane A, row 2, column 5) with palette $3B (build 87).
DEFAULT_LABELS += ["LutzPortraitArt"]

# EndingScreen (ps2.asm:10795): the epilogue's pictures -- the six page
# portraits and the last picture the strip wipe opens -- at VRAM $0020
# (tile 1); the starfield (its first 50 tiles) and the ending UFO
# (object 8, the other 45) at $9000 (tile $480); the credits'
# 26-letter alphabet at $A000 (tile $500), over the font's first 26 tiles.
DEFAULT_LABELS += ["loc_AF82A", "loc_B6E30", "loc_B6C2C"]

# Unnamed in the disassembly; named here for what IntroScr_EventStartGame
# (ps2.asm:19528) does with them.
OUTPUT_NAMES = {
    "loc_6D7BA": "DreamBattleArt",     # the fight with Dark Force
    "loc_6FF22": "DreamWakingArt",     # Rolf waking
    # Building_TylerSpaceship's second picture (loc_CCE6, ps2.asm:18749):
    # decompressed to RAM and dissolved into VRAM $0020 (tile 1) by
    # loc_6BBE, 150 tiles, under nametable loc_70D42 -- "Help!!!" ($1903).
    "loc_6F040": "DreamHelpArt",
    # Building_Roof's flight (loc_C58A, ps2.asm:18105): the planet the
    # ship flies to, at VRAM $0020 (tile 1) under nametable loc_716BA, and
    # Obj_Spaceship's art at VRAM $8000 (tile $400, tile word $4400).
    "loc_71362": "SpaceshipPlanetArt",
    "loc_71702": "SpaceshipArt",
    # LevelScreen decompresses this to $FF3000; RenderCharSprites copies
    # eight tiles a frame out of it for each party member.
    "loc_2B1F0": "CharWalkArt",
    # BattleScreen loads this at VRAM $F020 (tile $781): the ground the
    # battle floor nametable (loc_75BD8) is built from.
    "loc_7567A": "battle_floor_art",
    # loc_7E02 cases 1 and 2 decompress this to VRAM $0020 (tile 1): the
    # picture of Nei's death on Climatrol F7 (nametables loc_AF4EA and
    # loc_AF7FA).
    "loc_AED00": "NeiDeathArt",
    # EndingScreen's three blobs (build 93a).
    "loc_AF82A": "EndingPicturesArt",
    "loc_B6E30": "EndingStarfieldUfoArt",   # the starfield's 50 tiles, then the UFO's 45
    "loc_B6C2C": "EndingCreditsLettersArt",
}


def output_name(label):
    if label in OUTPUT_NAMES:
        return OUTPUT_NAMES[label]
    if label.startswith("Battle_") and label.endswith("Art"):
        return "battle_" + label[len("Battle_"):-len("Art")].lower() + "_art"
    return label


def read_block(rom, label):
    """(build 185) The label's block in the cartridge image (a RomMap):
    its bytes up to the next label."""
    return rom.blob(label)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rom", type=Path, help="the cartridge image")
    ap.add_argument("-o", "--outdir", type=Path, default=Path("assets/tiles"))
    ap.add_argument("-l", "--labels", nargs="*", default=DEFAULT_LABELS)
    args = ap.parse_args()

    rom = RomMap(args.rom)
    args.outdir.mkdir(parents=True, exist_ok=True)

    for label in args.labels:
        out = args.outdir / f"{output_name(label)}.png"
        # (build 156) The art the pictures replaced is read from the
        # cartridge image where it is needed (disasm_art.py), not kept as
        # a sheet: an old one goes.
        if output_name(label) in asset_names.PICTURE_ART:
            out.unlink(missing_ok=True)
            print(f"{label}: picture art, no sheet ({out.name})")
            continue
        raw = read_block(rom, label)
        tiles, used = decompress(raw)

        ps2png.write_tile_sheet(out, tiles, ps2png.grey_ramp())

        trailing = len(raw) - used
        print(f"{label}: {len(tiles)} tiles, {used}/{len(raw)} bytes "
              f"({trailing} trailing) -> {out}")


if __name__ == "__main__":
    main()
