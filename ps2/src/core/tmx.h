#ifndef PS2_TMX_H
#define PS2_TMX_H

#include "ps2_types.h"

/* (build 134) A Tiled map as the port's tilemaps keep them: embedded
 * tilesets, each with integer properties, and CSV tile layers. A gid
 * keeps Tiled's flip flags in its top bits; tmx_tileset_of looks one up
 * with them masked. Firstgids are read, never assumed: Tiled renumbers
 * tilesets when it saves, so a map says what a tile means through its
 * tileset's properties, not through where its gids start. (build 135)
 * The map's own integer properties are read too (tmx_map_int). Readers:
 * winmap.c (the window maps) and nametable.c (the screen nametables). */

#define TMX_FLIP_H      0x80000000u
#define TMX_FLIP_V      0x40000000u
#define TMX_FLIP_D      0x20000000u
#define TMX_FLIP_HEX    0x10000000u
#define TMX_FLAGS       (TMX_FLIP_H | TMX_FLIP_V | TMX_FLIP_D | TMX_FLIP_HEX)

#define TMX_TILESETS_MAX 8
#define TMX_PROPS_MAX    8
#define TMX_NAME_MAX     32

/* Integer properties, as Tiled writes them (`<property name=... value=...>`
 * inside a `<properties>` block). */
typedef struct {
    int  count;
    char name[TMX_PROPS_MAX][TMX_NAME_MAX];
    int  value[TMX_PROPS_MAX];
} TmxProps;

typedef struct {
    u32      firstgid;
    u32      tilecount;
    TmxProps props;
} TmxTileset;

typedef struct {
    int        width, height;
    u32       *gid;             /* width * height, flags included */
    TmxProps   props;           /* (build 135) the map's own */
    int        tileset_count;
    TmxTileset tileset[TMX_TILESETS_MAX];
} Tmx;

/* Reads `path` through the asset store: its tilesets and the layer named
 * `layer` (the first layer when NULL). Says why on stderr and returns
 * false on anything it cannot read -- an external tileset (.tsx), a layer
 * not in CSV, a cell count that is not width x height. */
bool tmx_load(Tmx *t, const char *path, const char *layer);
void tmx_free(Tmx *t);

/* The tileset gid falls in (flags masked), and the tile's index within it
 * in *local; NULL for gid 0 or a gid in no tileset. */
const TmxTileset *tmx_tileset_of(const Tmx *t, u32 gid, int *local);

/* A tileset's integer property, or fallback when it has none by that
 * name. */
int tmx_tileset_int(const TmxTileset *ts, const char *name, int fallback);

/* (build 135) The map's own integer property (the <properties> block
 * after <map>, before the first tileset or layer), or fallback. */
int tmx_map_int(const Tmx *t, const char *name, int fallback);

#endif /* PS2_TMX_H */
