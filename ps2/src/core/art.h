#ifndef PS2_ART_H
#define PS2_ART_H

#include "ps2_types.h"

/* (build 142) The pictures: the art as the game draws it, one full-colour
 * picture a thing (docs/asset_format.md). For now the portraits and (146)
 * the library graph, assets/art/portraits/<thing>/<thing>.{json,png},
 * written by tools/extract_pictures.py from the portrait's tiles, its
 * window's map and its palette line.
 *
 * A picture stands in for the tiles it was made from: the compositor
 * (video.c) takes a pixel's colour from the picture wherever a binding says
 * those tiles are drawn (VideoPicture), and the index from the tiles as
 * before, so tools/colour_check.py proves the picture is the tiles through
 * the palette. */

typedef struct {
    char thing[32];     /* "nei", "rolf": the folder and file stem */
    int  w, h;          /* pixels: the JSON's "size", the PNG's size */
    int  at_col;        /* where its top left sits in its window, in cells */
    int  at_row;        /* from the frame's top-left corner: the JSON's "at" */
    u8  *rgba;          /* w * h * 4; alpha 0 is nothing drawn */
    u8  *slots;         /* (build 146) w * h palette entries 1-15 where an
                           effect recolours the pixel, 0 elsewhere; NULL
                           for a picture no effect touches */
    int  frame;         /* (build 149) its key in the description's
                           "frames": 1 for a thing of one picture */
} Picture;

/* `json` is a picture's description (assets/art/<kind>/<thing>/<thing>.json);
 * its frame "1" is read from the PNG beside it. Refuses, naming the file
 * and saying why, a description without "size" or "at", a frame that is
 * missing or not a picture png_load_rgba reads, and a PNG whose size is not
 * the description's. (build 146) A frame's "slots" names its slot map, an
 * indexed PNG of the same size beside it (tools/extract_pictures.py,
 * SLOTS): each pixel's index the entry, in the line the picture is drawn
 * in, that an effect writes there -- the radar's DANGER under Gaira's
 * control panel, the library graph's hidden lines under its overlay.
 * Refused, naming it, when it does not load, is another size or holds an
 * index past 15. */
bool picture_load(Picture *p, const char *json);
void picture_free(Picture *p);

/* (build 149) The same for the description's frame `number` (its key in
 * "frames", the number the game gives the frame): a thing of several
 * pictures, Tyler's monitor's five. picture_load is frame 1. */
bool picture_load_frame(Picture *p, const char *json, int number);

/* assets/art/portraits/portraits.json: each portrait window's picture, by
 * window number (WinID_PortraitStart + n's n, 0 Rolf's), each thing loaded
 * once. False, said on stderr, when the index or a picture it names does
 * not load; the windows that did load keep theirs. */
#define ART_PORTRAIT_WINDOWS 31

bool art_portraits_load(void);
void art_portraits_free(void);

/* Window `n`'s picture; NULL when it has none (the windows the game never
 * shows, a picture that did not load). */
const Picture *art_portrait(int n);

/* (build 146) The library graph's picture (portraits.json's
 * "library_graph"): what WinID_LibraryGraphPortrait draws in the library.
 * NULL when it did not load. */
const Picture *art_library_graph(void);

/* (build 147) A cut-scene picture, assets/art/scenes/<thing>/<thing>.json
 * (tools/extract_pictures.py's SCENES: a nametable drawn whole), loaded
 * the first time it is asked for and kept; NULL, said on stderr, when it
 * does not load -- so ask once, at startup, and keep the pointer. */
const Picture *art_scene(const char *thing);
void art_scenes_free(void);

/* (build 149) Frame `frame` of a scene of several (tyler_monitor's 0-4),
 * the same way; art_scene is frame 1. */
const Picture *art_scene_frame(const char *thing, int frame);

#endif /* PS2_ART_H */
