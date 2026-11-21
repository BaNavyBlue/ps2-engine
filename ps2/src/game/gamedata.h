#ifndef PS2_GAMEDATA_H
#define PS2_GAMEDATA_H

#include "../core/ps2_types.h"

/* Everything SaveData (ps2.asm:19604) writes to a slot, as a struct, and
 * a JSON file format for it.
 *
 *   $FFFFC000  8 characters, $40 bytes each
 *   $FFFFC600  party, money, position, names       ($C600-$C69D)
 *   $FFFFC700  event and treasure-chest flags      ($C700-$C7FF)
 *
 * The cartridge's checksum, mirror copy and copyright stamp exist to
 * survive a failing battery and are not carried over.
 *
 * Fields the port has a name for are struct members. The rest of the
 * $C600 block rides along in c600_raw so a load/save round trip never
 * loses state the port does not understand yet; naming a field later is
 * one row in the table in gamedata.c. */

#define GD_CHARS        8

/* The $C700 flag block's bytes by name (GameData.c700[FLAG_*]) are in
 * rom_ids.h. The save's key for each is its address plus the name in
 * assets/data/flag_names.json. */
#define GD_NAME_LEN     4       /* the cartridge's: four bytes a name ($C660) */
/* (build 163) What the port keeps of a name: six letters, as a language
 * whose names are longer has them (his long_names; lang.h), $C4 after a
 * shorter one. The save's raw $C600 block keeps the first four, as the
 * cartridge's; its "name" the whole. */
#define GD_NAME_MAX     6
/* (build 165) A name as text (UTF-8): six of the Japanese cartridge's
 * kana take eighteen bytes. */
#define GD_NAME_TEXT    32
#define GD_ITEM_SLOTS   16
#define GD_PARTY_MAX    4
#define GD_SAVE_SLOTS   4
#define GD_C600_SIZE    0x9E
#define GD_C700_SIZE    0x100

enum { EQUIP_HEAD, EQUIP_RIGHT, EQUIP_LEFT, EQUIP_BODY, EQUIP_LEGS,
       EQUIP_COUNT };

/* Technique ids: the rows of TechniqueData, TechID_* in the constants
 * file (ps2.constants.asm:441). The fight's action table
 * (TechniqueActionIndex, ps2.asm:2981) and the map's technique menu
 * dispatch on these. */
enum {
    TECH_NONE,
    TECH_FOI,   TECH_GIFOI, TECH_NAFOI,     /* 1-3   one enemy          */
    TECH_ZAN,   TECH_GIZAN, TECH_NAZAN,     /* 4-6   one group          */
    TECH_GRA,   TECH_GIGRA, TECH_NAGRA,     /* 7-9   every enemy        */
    TECH_TSU,   TECH_GITHU, TECH_NATHU,     /* $A-$C one enemy          */
    TECH_SHIFT,                             /* $D    the caster's stats */
    TECH_FANBI,                             /* $E    HP drained         */
    TECH_EIJIA,                             /* $F    one group          */
    TECH_BROSE,                             /* $10   one enemy          */
    TECH_CONTE,                             /* $11   the enemy quieted  */
    TECH_GAJ,   TECH_GIGAJ, TECH_NAGAJ,     /* $12-$14 one enemy        */
    TECH_SAG,   TECH_GISAG, TECH_NASAG,     /* $15-$17 every enemy      */
    TECH_GEN,                               /* $18   one enemy          */
    TECH_SAGEN,                             /* $19   every enemy        */
    TECH_VOL,                               /* $1A   one enemy          */
    TECH_SAVOL,                             /* $1B   one group          */
    TECH_SHIZA,                             /* $1C   the enemy quieted  */
    TECH_DORAN,                             /* $1D   the enemy insane   */
    TECH_RIMIT,                             /* $1E   paralysis          */
    TECH_SHINB,                             /* $1F   run away           */
    TECH_FORSA,                             /* $20   the enemy insane   */
    TECH_RIMET,                             /* $21   paralysis          */
    TECH_SHU,   TECH_SASHU,                 /* $22-$23 defense up       */
    TECH_DEBAN,                             /* $24   damage halved      */
    TECH_NER,   TECH_SANER,                 /* $25-$26 agility up       */
    TECH_RES,   TECH_GIRES, TECH_NARES,     /* $27-$29 one healed       */
    TECH_SAR,   TECH_GISAR, TECH_NASAR,     /* $2A-$2C the party healed */
    TECH_SAK,   TECH_NASAK,                 /* $2D-$2E a life given up  */
    TECH_ANTI,                              /* $2F   poison cured       */
    TECH_REVER,                             /* $30   revival            */
    TECH_RYUKA, TECH_HINAS, TECH_MUSIK,     /* $31-$33 map techniques   */
    TECH_MEGID                              /* $34                      */
};
_Static_assert(TECH_MEGID == 0x34, "the technique ids are TechniqueData's rows");

typedef struct CharData {
    u16 status;                 /* +$00, meaning not read yet */
    u16 hp, max_hp;             /* +$02 +$04 */
    u16 tp, max_tp;             /* +$06 +$08 */
    u16 level;                  /* +$0A */
    u32 experience;             /* +$0C */
    u16 strength, mental, agility, luck, dexterity;   /* +$10.. */
    u16 attack_base;            /* +$1A: before equipment */
    u16 attack;                 /* +$1C */
    u16 defense;                /* +$1E */
    u8  equip[EQUIP_COUNT];     /* +$20 item ids */
    u8  techs_a, techs_b;       /* +$25 +$26 */
    u8  item_count;             /* +$27 */
    u8  items[GD_ITEM_SLOTS];   /* +$28, bit 7 = equipped */
    u8  name[GD_NAME_MAX];      /* character_names entry, CHARSET codes */
} CharData;

typedef struct GameData {
    CharData chr[GD_CHARS];

    u16 party_members_num;      /* $C600: members - 1 */
    u16 party_members_joined;   /* $C604 */
    u16 party_member_join_next; /* $C606 */
    u16 party_member_id[GD_PARTY_MAX];   /* $C608 */
    u32 money;                  /* $C620 */
    u16 level_index;            /* $C640 */
    u16 level_y, level_x;       /* $C642 $C644 */
    u16 jet_scooter;            /* $C652 */
    u16 planet;                 /* $C658 */

    u8  save_name[GD_NAME_MAX]; /* $C680, what the slot list shows;
                                   (build 164) six, as a character's */
    u16 save_level;             /* $C684 */

    u8  c600_raw[GD_C600_SIZE];
    u8  c700[GD_C700_SIZE];
} GameData;

typedef struct {
    bool exists;                /* a file is there                   */
    bool valid;                 /* ... and it parsed                 */
    u8   name[GD_NAME_MAX];     /* (build 164) six */
    int  level;
} SlotSummary;

bool gamedata_load_tables(void);

/* loc_89EE (ps2.asm:12425): the state a new game starts from. */
void gamedata_new_game(GameData *gd);

/* SetCharEquipment (15897): attack, defense and agility from what is
 * worn. Defense and agility are added to, not recomputed, so this is for
 * a fresh character only -- exactly as the original uses it. */
void gamedata_apply_equipment(GameData *gd);

/* loc_AD4C and loc_ADBE (15907, 15958): one character's equipment
 * bonuses put on or taken off. Anything that changes what is worn calls
 * the second, changes the slots, then calls the first. */
void gamedata_equip_bonuses_add(GameData *gd, int c);
void gamedata_equip_bonuses_remove(GameData *gd, int c);

/* The Gaira prison, ObjType_Events $2D (loc_DF32): every character's $40
 * bytes copied to $FFFFDA00 (loc_6D5A x4), the bonuses off (loc_ADB0),
 * then for all eight TP 0, PrsnClths worn and nothing else, the bag a
 * PlsmRing and the PrsnClths, and SetCharEquipment. $DA00 is outside the
 * save, as in the ROM: it lives until the power goes. */
void gamedata_prison(GameData *gd);

/* Tyler's spaceship, loc_CE5E: loc_ADB0; for all eight HP = max HP,
 * TP = max TP, the five worn slots, the bag count and the sixteen bag
 * slots from $DA00; SetCharEquipment. Level, EXP and stats stay as they
 * are. */
void gamedata_prison_release(GameData *gd);

/* InventoryData (27677), by item id. The name is ASCII, up to ten
 * characters; win_charset turns it back into the codes the ROM stores. */
int         gamedata_item_id(const char *key);     /* by ItemID_ name */
const char *gamedata_item_key(int id);             /* the ItemID_ name */

/* character_index $18 is the Central Tower's item keeper: a ninth bag
 * whose count and slots sit where a ninth character's would, at $C627 and
 * $C628 -- inside the $C600 block, so it is already in the save. */
#define GD_KEEPER 0x18

/* The rest of the $C600 block by what reads it: offsets into
 * GameData.c600_raw, each a big-endian word unless said (docs/game.md,
 * "The $C600 block", has every byte). The block's other bytes -- party,
 * money, map, scooter flag, planet, names, the slot label -- are struct
 * members above and are packed into it on save.
 *
 * State a routine reads back later: */
#define GD_KEEPER_COUNT         0x27    /* $C627 (byte): the tower room's items, 0-16 */
#define GD_KEEPER_ITEMS         0x28    /* $C628-$C637 (bytes): the items         */
#define GD_LAST_SAVE_MAP        0x46    /* $C646: where the last Data Memory save */
#define GD_LAST_SAVE_Y          0x48    /* $C648  was made; the Telepipe and Ryuka */
#define GD_LAST_SAVE_X          0x4A    /* $C64A  land there (ItemAction_Telepipe) */
#define GD_ENTRANCE_MAP         0x4C    /* $C64C: where the party last went into */
#define GD_ENTRANCE_Y           0x4E    /* $C64E  a dungeon; the Escapipe, Hinas  */
#define GD_ENTRANCE_X           0x50    /* $C650  and the tower keyboard go back  */
#define GD_SCOOTER_PARKED_Y     0x54    /* $C654: the jet scooter's cell on Motavia */
#define GD_SCOOTER_PARKED_X     0x56    /* $C656                                   */
#define GD_LAST_SAVE_PLANET     0x5A    /* $C65A: 1 = the last save was on Dezolis */
#define GD_LAST_SAVE_VISIPHONE  0x5C    /* $C65C: 1 = made with the Visiphone      */
/* Scratch that the save happens to carry: */
#define GD_STEP                 0x02    /* $C602: a step counter many routines share */
#define GD_ORDER_NEW            0x10    /* $C610-$C617: the C menu's ORDER, the new order */
#define GD_PARTY_HELD           0x18    /* $C618-$C61F: the party held while Nei fights  */
#define GD_PARTY_HELD_NUM       0x24    /* $C624        Neifirst alone; a party list's copy */
#define GD_NAME_TYPED           0x3C    /* $C63C-$C63F (bytes): the name entry's letters */
#define GD_SLOT_IN_USE          0x9C    /* $C69C: $FFFF, the slot holds a save      */

u16  gamedata_word(const GameData *gd, int off);
/* A level's name as levels.json gives it ("Paseo", "MotaviaOutside"),
 * or NULL; and back, any case, or -1. The save names maps with these. */
const char *gamedata_level_name(int id);
int         gamedata_level_id(const char *name);
void gamedata_set_word(GameData *gd, int off, u16 v);

/* The count and the sixteen slots of character c's bag, or the keeper's. */
u8 *gamedata_bag(GameData *gd, int c, u8 **count);

/* TechniqueData, by technique id: the name in ASCII, up to five. */
const char *gamedata_tech_name(int id);

/* (build 163) The language's name of technique or item `id` (lang.h's
 * names.json, whole and in its own letters), or NULL where the game's own
 * (above) stands -- the cartridge's language, or a name it does not give. */
const char *gamedata_tech_text(int id);
const char *gamedata_item_text(int id);

/* (build 163) The party's names a language gives: a name that is any
 * language's default for that character -- the cartridge's, or one in a
 * language's names.json -- becomes the chosen language's default; a name
 * the player typed stays. gamedata_new_game and gamedata_load do it; the
 * game does it when LANGUAGE changes. */
void gamedata_language_names(GameData *gd);

/* (build 165) The party's names and the slot label as text in the chosen
 * language (lang.h), and back: what LANGUAGE carries across a change, a
 * name being codes of the language it was made in. Back, a name that is
 * any language's default is the chosen one's; a character the language
 * has no code for is a space. */
void gamedata_names_text(const GameData *gd, char text[GD_CHARS][GD_NAME_TEXT],
                         char label[GD_NAME_TEXT]);
void gamedata_names_set_text(GameData *gd,
                             char text[GD_CHARS][GD_NAME_TEXT],
                             const char *label);

/* (build 164) A name from the name board's `n` letters (four, or a
 * proportional language's six; $C4 after a shorter one, or zeros, as the
 * board leaves them): the whole name, $C4 after the n when n is short of
 * GD_NAME_MAX. */
void gamedata_set_name(u8 name[GD_NAME_MAX], const u8 *entry, int n);
u8          gamedata_tech_tp(int id);       /* TechniqueData+5, the TP cost */

/* loc_112C4: what character c knows, as the 32 bytes $C800 + 32c holds --
 * 16 map techniques, then 16 battle ones, zero-filled, with Musik in the
 * last place taken when bit 7 of the count byte is set. */
void gamedata_known_techs(const GameData *gd, int c, u8 out[32]);

/* CharExperiencePtrs (ps2.asm:30875): the experience row `row` of the
 * character's table starts at -- row 0 is level 1. */
u32 gamedata_level_exp(int c, int row);

/* The same row's other bytes: the level it names and the ten gains
 * Character_ProcessLevelUp (22421) rolls GenerateRandStatIncrease over.
 * techs_a is the high nibble of the last byte (+$25, battle techniques),
 * techs_b the low (+$26, map techniques). */
typedef struct {
    u8 level, hp, tp, strength, mental, agility, luck, dexterity;
    u8 attack, defense, techs_a, techs_b;
} LevelRow;
bool gamedata_level_row(int c, int row, LevelRow *out);

u8 gamedata_tech_flags(int id);     /* TechniqueData+6 */
u8 gamedata_tech_rate(int id);      /* TechniqueData+7 */
const char *gamedata_item_name(int id);
u16         gamedata_item_price(int id);
u8          gamedata_item_action_flags(int id);    /* byte 12, the kind */
u8          gamedata_item_attack(int id);          /* byte 14 */
u8          gamedata_item_equip_mask(int id);      /* byte 14 */

/* The directory the slot files are read from and written to; "saves" by
 * default. tools/route_probe.c gives each route its own, so a route can
 * erase, save or quarantine without touching the player's slots. */
void gamedata_set_save_dir(const char *dir);

/* (build 123) What "by default" is -- the directory a NULL above goes back
 * to: the game's saves folder (home_saves in main.c), "saves" until set. */
void gamedata_set_save_home(const char *dir);

bool gamedata_save(const GameData *gd, int slot);
bool gamedata_load(GameData *gd, int slot);
void gamedata_slot_summary(int slot, SlotSummary *out);
void gamedata_erase(int slot);

/* A file that will not parse is moved aside, never overwritten: it may
 * be somebody's hand edit with a comma missing. */
void gamedata_quarantine(int slot);

#endif /* PS2_GAMEDATA_H */
