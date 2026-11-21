# The CRT simulation

Three phases. Phase 1 (build 105) is what can be built and checked without
a GPU: the Escape menu's CRT and HDR pages and their settings, what a
display can have, a display probe to run on real screens, and a CPU
reference of the whole chain. Phase 2 draws it in SDR through SDL_GPU;
phase 3 adds the HDR swapchain. Phases 2 and 3 must match the reference.

## The settings

The CRT page (Escape, CRT): **SCANLINES** (OFF, ON; builds 159 to 166 had INTERLACED too:
"Interlaced scanlines" below), **MASK** (OFF,
APERTURE GRILLE, SLOT MASK, SHADOW MASK), **SUBPIXELS** (RGB, BGR, OTHER),
**HDR**, whose C opens the HDR page (Escape or B goes back a page): **HDR** (ON, OFF), **PEAK** (AUTO, or
400 to 10000 nits: the panel's brightest), **WHITE** (the game's white in
nits, 50 to 1000). `settings.json` keeps them under `"crt"`:

    "crt": { "scanlines": true, "mask": "grille", "subpixels": "other",
             "hdr": true, "peak": "auto", "white": 200 }

(build 167) `"scanlines": "interlaced"`, from builds 159 to 166, reads
as `true`, and their `"flicker"` is passed over.

Everything is off by default, so nothing changes on screen before phase 2.

The white is D65 (The owner, build 105: the American sets'), the display's own:
the guns need no gains. The Japanese sets' 9300 K (9300 K + 8 MPCD, x 0.283,
y 0.297, NHK's) would be a gain per gun in `crt.c`'s plan, `white_gain`, and
cost SDR brightness and HDR headroom: at the same luminance its blue is
about 1.34 times D65's.

The panel's subpixels cannot be asked for -- no OS reports them -- so
SUBPIXELS is a setting. RGB and BGR are stripes (most LCDs) and get masks
exact to the subpixel; OTHER is everything else (WOLED, QD-OLED, PenTile,
Samsung's Diamond Pixel on the owner's ProArt P16) and gets masks of whole
pixels.

## What a display can have

`src/core/crt.c`, `crt_scanlines_why` / `crt_mask_why` / `crt_hdr_why`, from a
`CrtDisplay`:

* **The panel's own pixels.** A mask (and the scanlines) is laid out in
  display pixels, so the output must be the panel's: FULLSCREEN, or a window
  the desktop does not scale. The SDL3 platform creates its window with
  `SDL_WINDOW_HIGH_PIXEL_DENSITY`, so on a scaled desktop (Wayland's
  fractional scale, macOS's Retina, Windows at 150%) the window's buffer is
  the panel's pixels; it takes a window as scaled when its pixel density
  falls short of the desktop mode's.
* **k**, the largest whole multiple of the 224 lines that fits (with the 320
  pixels at the pixel aspect across -- `frame_layout`'s k): scanlines need
  k >= 2 (a beam and a gap); the aperture grille k >= 4 (1080p up); slot and
  shadow masks k >= 6 (1440p up: their rows need several display lines a
  scanline or they beat against the scanlines).
* **HDR**: SDL says the window can show it (`SDL_PROP_WINDOW_HDR_ENABLED_BOOLEAN`).
  On Linux that needs SDL 3.2.6 or later (Wayland colour management), a
  compositor that speaks it, the driver, and the monitor in HDR mode.

| Display, fullscreen | k | Scanlines | Grille | Slot, shadow | Triad |
|---|---|---|---|---|---|
| 1920x1080 | 4 | yes | yes | NEEDS 1440P | 3 px, ~390 across |
| 2560x1440 | 6 | yes | yes | yes | 3 px (shadow 4) |
| 3840x2160 | 9 | yes | yes | yes | 4 px, ~660 across |
| 3840x2400 | 10 | yes | yes | yes | 4 px, ~730 across |

What a value cannot have shows on the line under it:

| Why | When |
|---|---|
| NEEDS FULLSCREEN | not the panel's pixels, or k too small, but fullscreen on this display would do |
| NEEDS 480P / 1080P / 1440P | not even fullscreen here: the display is too small for k 2 / 4 / 6 |
| DESKTOP SCALED | the desktop's mode is not the panel's resolution: even fullscreen is resampled, and nothing in the game helps |
| NO HDR | SDL does not say the window can show HDR |

DESKTOP SCALED is not decided by the SDL3 platform yet: SDL has no call for
the panel's native size, and the largest fullscreen mode is not it on macOS
(it lists scaled modes above the native one), so a guess could lock the
masks out of a screen that can have them. `tools/display_probe.c` prints
what it would be decided from. A route (`display ... desktop-scaled`) or
`PS2_DISPLAY` can show it.

PEAK's AUTO is SDL's HDR headroom (`SDL_PROP_WINDOW_HDR_HEADROOM_FLOAT`)
times SDR white in nits, when SDL says HDR; else 1000. SDR white in nits
is the platform's reading of `SDL_PROP_WINDOW_SDR_WHITE_LEVEL_FLOAT`
(`CrtDisplay.sdr_nits`): on Windows the linear space is scRGB, whose 1.0
is 80 nits, so the level x 80; on Wayland and macOS SDL's SDR white is 1.0
and its nits are not said, so it is taken as BT.2408's reference white,
203 nits. The ratio is what sets the mask's depth, and it is SDL's own;
the nits the menu shows are only as right as that 203.

**The ProArt P16, measured** (`display_probe`, SDL 3.4.16, Wayland):
desktop 2021x1263 points at a pixel density of 1.9, 3840x2400 pixels; HDR
on, headroom 1.624 over an SDR white of 1.000; all four swapchain
compositions and all three present modes; SDL_GPU on Vulkan, the RTX 5070
Laptop GPU preferring performance, the Radeon 890M preferring low power.
Windowed, the game's 960x672-point window is 1824x1277 pixels, the
desktop's own density: not scaled, k 5 (scanlines, the grille); fullscreen
3840x2400, k 10 (every mask). The largest fullscreen mode is the desktop's
pixels, so on Wayland the DESKTOP SCALED test would work (Windows and
macOS not yet measured). Before build 105's fix AUTO read 1.624 x 1.0 x
80 = 130 nits; now 1.624 x 203 = 330 (route `crt_p16`, the probe's words).

**The PA32UCX, measured** (`display_probe` on the owner's Windows PC, SDL
3.4.16, Direct3D 12 on an RTX 3080 Ti, Vulkan also offered): the PA32UCX and
a Dell UP2718Q, both 3840x2160 at 100% (pixel density 1). Windows's HDR was
off on both: HDR enabled no, headroom 1, so the HDR row says NO HDR and
AUTO is 1000 (route `crt_pa32ucx`). Windowed at 960x672, k 3 (scanlines;
the grille wants fullscreen); fullscreen k 9, every mask, four-pixel
triads. Direct3D 12 offered the HDR10_ST2084 swapchain with HDR off (and
not HDR_EXTENDED_LINEAR): the swapchain list is not the HDR test, SDL's
window property is. With Windows's "Use HDR" on (the PA32UCX only): HDR
enabled, a headroom of 14.995 over an SDR white of 1.000 -- scRGB's 80 nits,
so AUTO is 1200 nits, the panel's rated peak: the x 80 reading is right on
Windows. Still no HDR_EXTENDED_LINEAR from Direct3D 12, so phase 3 on
Windows is HDR10 (PQ), the encode the reference checks. At PEAK 1200 on a
4K panel the mask and scanlines are whole up to WHITE ~238 (4-pixel triads:
the brightest subpixel 1.26 x 4 times the white field's mean).

DESKTOP SCALED stays undecided by the game. On both machines the largest
fullscreen mode is the desktop's pixels, but on Windows NVIDIA's DSR (and
AMD's VSR) list modes above the panel's native one, which would make every
native desktop look scaled; macOS lists scaled modes above native too.

A headroom of 1.6 is little: at PEAK 330 the white field's WHITE sets how
deep the mask can go -- WHITE 200: 11%; 150: 25%; 120: 40%; 100: 55%;
80: 77% (the scanlines whole throughout). The headroom is the compositor's
peak over its SDR white; turning the desktop's SDR brightness down (in the
compositor's HDR settings) raises it, and AUTO follows.

Testing without the screens: the headless platform reads `PS2_DISPLAY`,
and the route probe has `display`, both `crt_display_parse`'s words --

    display 3840x2400 fullscreen hdr=6.25/2.5
    display 1280x800 full=3840x2400 window-scaled
    display 1920x1080 fullscreen desktop-scaled
    display 1824x1277 full=3840x2400 hdr=1.624/1.000 sdr=203

(`sdr=N`: SDR white in nits; without it `hdr=H/S` takes S x 80, Windows's)
-- and `expect crt ROW VALUE [/ WHY]` (rows: summary, scanlines, mask,
subpixels, hdr, peak, white; builds 159b to 166 had flicker). Routes `crt_menu` and `crt_hdr` show every
value; `crt_p16` the P16 as measured.

## The model

`src/core/crt.c`, `crt_plan_resolve` and `crt_render`. Everything is in
display pixels of one output size (fullscreen: the display's).

1. **Index to light.** A frame's palette index gives a CRAM word; its three
   3-bit levels go through the measured DAC ladder (`palette.c`: 0, 52, 87,
   116, 144, 172, 206, 255 of 255 -- the VA4's output volts) and the set's
   gamma, 2.4 (BT.1886 with a black of zero): the gun's drive, linear, 0..1.
   Not `cram_to_linear`, which takes the ladder for sRGB-encoded.
2. **The picture's place.** k is the whole multiple of 224 lines that fits;
   the picture is 224 k rows and 320 x the pixel aspect x k columns (TV:
   32:35, 1170 at 1080p), centred. The beam spreads the 320 pixels over
   those columns, so the DISPLAY aspect comes free.
3. **Across.** Each source pixel is a box one pixel wide through a normal of
   0.30 source pixels, averaged over the display column's footprint (closed
   form: the normal's CDF integrated). The weights of all source pixels sum
   to 1 at every column, so a flat field stays flat.
4. **The scanlines.** Each of the 224 lines is a beam with a normal profile
   of height sigma = k x (0.20 + 0.12 x L^0.5) -- a bright line fuller than a
   dark one -- scaled so a line's rows sum to k: integrated over each display
   row, a flat field of level L averages L, whatever the width. Blended with
   the flat line by the scan depth.
5. **The mask**, in absolute display pixels (a shader's fragment
   coordinates), rows counted from the picture's top so the slot and shadow
   rows sit on the scanlines:
   * a triad of 4 pixels on every display (build 106, the owner: "whatever is
     most compatible"). Build 105 took 3 below 4K (k < 9); four spaces the
     lit subpixels evenly on every panel whatever SUBPIXELS says, and is Sony
     Megatron's coarsest 1080p grille. The model keeps the 3-pixel layout
     below (`triad_channel`), and `crt_plan_resolve`'s one line would bring
     it back;
   * RGB and BGR stripes, 4-pixel triad: R, G, B, dark on RGB (B, G, R, dark
     on BGR) -- the lit subpixels land every 4, R G B in turn, exactly;
   * stripes, 3-pixel triad (not used since build 106): the panel's order reversed (B, G, R on RGB), the
     lit subpixels two apart with a gap of five for the grille's dark stripe
     -- three pixels cannot space them evenly (R, G, B would leave gaps of 4,
     4, 1); Sony Megatron's 4K "800 TVL" grille does the same;
   * OTHER: whole pixels R, G, B (and dark in a 4-pixel triad);
   * aperture grille: the triad on every row; slot mask: two triad columns,
     each cut by a one-row bridge once a line pitch, the second column's
     half a pitch below the first's, both clear of the beam's centre (rows
     k/4 and k/4 + k/2); shadow mask: the lower half of each line pitch
     shifted half a triad, so the triads sit in a hexagon;
   * the lit channel of a lit element weighs the period's area over its lit
     area (a 4-pixel grille: 4), so each channel averages exactly 1 over a
     period. Blended with 1 by the mask depth.
6. **The light.** The output is scale x beam x mask per channel, in Rec.709
   linear light: in nits for HDR, relative to SDR white for SDR.
   * HDR: scale is WHITE, so a white field averages WHITE. Its brightest
     subpixel is WHITE x the beam's peak x the mask's, which may not pass
     PEAK: the scanlines are kept whole first (a white field's brightest row
     is 1.17-1.26 times its mean), and the mask takes the headroom that is
     left, as deep as it goes. A full mask and scanlines pass 20-28% of the
     light, so at WHITE 200 a 4-pixel mask is whole from PEAK ~1000.
   * SDR: no headroom. The full mask and scanlines, and the picture scaled so
     the brightest subpixel is 1.0: a white field averages 0.20 of SDR white
     with a 4-pixel mask (0.17-0.21 by mask and display; 105's 3-pixel
     grille below 4K gave 0.29), 0.79-0.86 with scanlines alone. SDR is the
     dimmer picture; HDR buys the brightness back.
7. **The encodes.** SDR: sRGB's curve, 8 bits. HDR10: Rec.709 to Rec.2020
   (BT.2087), nits through ST 2084 (PQ). `HDR_EXTENDED_LINEAR` (scRGB) would
   be the nits over 80, Rec.709 primaries, no curve.

The constants (`CRT_GAMMA`, `CRT_BEAM_*`) are phase 1's choices; phase 2
can tune them, and the reference moves with them.

## The reference

`tools/crt_ref.c` (`make crt_ref`) runs the model on the CPU.

    make crt_check          # the checks; route_out/crt_reference/checks.txt
    ./crt_ref crop route_out/new_game/01_title.png route_out/crt_reference \
        title_logo 140 132 32 20
    ./crt_ref crop route_out/climatrol_nei_dies/05_party_fights.png \
        route_out/crt_reference fight_hp 12 172 32 20

`crop` renders a rectangle of a route's PNG (the PNG's PLTE turns back into
CRAM exactly) for 1920x1080, 2560x1440, 3840x2160 and 3840x2400, fullscreen,
TV aspect: flat, scanlines, and each mask the display can have, in SDR (RGB
stripes; OTHER at 3840x2400, the ProArt P16's OLED), and in HDR at PEAK
1000, WHITE 200 for the two HDR screens (3840x2400 OTHER with the shadow
mask; 4K RGB with the grille, the PA32UCX). SDR crops are 8-bit sRGB; HDR
crops are 16-bit Rec.2020 PQ with a cICP chunk, which a browser on an HDR
screen shows as HDR. A crop is at display resolution: look at it at 100%
on a screen of that size, or zoomed with nearest-neighbour. The packed
crops are the title's logo and the fight screen's HP box (Rudo's, HP and
TP, from build 106: 105's fight crop had no recorded source, so it was made
again from one that is written down above).

`check` (27 checks, all passing in builds 105 and 106; 106 moved the light table and the SDR table below 4K to four-pixel triads):

* the encodes: PQ's 10-bit codes at 0, 100, 1000 and 10000 nits (0, 520,
  769, 1023), PQ decode(encode) to 1e-6 from 0.01 to 10000 nits, sRGB at
  known points, 709 to 2020 keeping white;
* the mask's layout: every mask, subpixel order, triad and k (4, 6, 9, 10)
  averages exactly 1 a channel over a period; on stripes the 4-pixel
  triad's lit subpixels every 4, the 3-pixel's two apart with a gap of
  five; OTHER whole pixels; the slot's bridges and the shadow's offset;
* what each display can have fullscreen (the table above);
* the light: every display, mask, subpixel order and PEAK from 400 to 2000
  at WHITE 200 -- a white field's mean within 1% of WHITE and no channel
  over PEAK (the table of depths is in checks.txt); the white field's
  chromaticity D65 through the whole chain; SDR nothing over 1.0 and the
  mean as predicted;
* a frame of the game (the title's logo, the shadow mask at 3840x2400):
  nothing over PEAK at 400 or 1000.

The slot mask's white field comes out up to 0.5% under WHITE (4K, k 9): its
bridge takes the beam's light at its own row, not the line's mean.

Phase 2's shaders are checked against this: the same crops rendered by the
GPU and compared pixel for pixel (to the encode's rounding).

## The display probe

`tools/display_probe.c`, SDL3 only (`make display_probe`), for Linux,
Windows and macOS:

    ./display_probe > display_probe.txt

It opens a window as the game does (960x672 points, resizable, high pixel
density), draws it grey through SDL_GPU so the window system maps it, and
prints, windowed, fullscreen and windowed again: SDL's version and video
driver; every display's desktop and current modes (points, pixel density,
pixels, refresh), content scale, HDR and fullscreen modes; the GPU drivers
and the devices SDL makes preferring performance and low power (the device's
name from SDL 3.4); the window's size in points and pixels, pixel density,
display scale, SDL's HDR state (enabled, headroom, SDR white, and their
product in nits: PEAK's AUTO), which swapchain compositions (SDR,
SDR_LINEAR, HDR_EXTENDED_LINEAR, HDR10_ST2084) and present modes (VSYNC,
IMMEDIATE, MAILBOX) the device offers it; whether the largest fullscreen
mode is the desktop's; and the same state as `PS2_DISPLAY` words, so a
route or a headless run shows what the menu will say on that screen.

What its output settles: whether high pixel density gives a scaled Wayland
desktop's window the panel's pixels (the pixel density should equal the
desktop mode's); what SDL reports for HDR on each screen; whether the
largest fullscreen mode can stand for the panel's native size (DESKTOP
SCALED); which device the P16 hands SDL_GPU.

## The plan for phases 2 and 3

The chain replaces `platform_sdl3.c`'s SDR placeholder with an SDL_GPU
pipeline: phase 2 the SDR swapchain, phase 3 the HDR one. The shaders are
compiled offline and checked in for every backend SDL_GPU uses -- SPIR-V
(Vulkan), DXIL (D3D12), MSL (Metal), with SDL_shadercross -- so a build
needs no shader compiler. The beam spreads the 320 pixels over the active
line, so the DISPLAY aspect comes free and the nearest-then-linear scaling
goes. The frame's indices and the palette go up as textures; the chain is
the reference above, a pass or two.

* **The shaders' toolchain.** The sandbox these builds are made in has no
  shader compiler (no glslc, dxc or SDL_shadercross), so the checked-in
  SPIR-V, DXIL and MSL have to be compiled once on a machine that has
  SDL_shadercross (a `make shaders` target), and again whenever a shader
  changes; the build itself still needs none. The P16 takes SPIR-V
  (Vulkan), the Windows PC DXIL (Direct3D 12) or SPIR-V.
* **Phase 2, SDR.** (Done in build 107 as below; "Phase 2" after this
  list says how it was built.) SDL_GPU replaces SDL_Renderer in `platform_sdl3.c`:
  the index frame and the palette (with the Escape menu's overlay line) as
  textures; one pass for the beam (across and down) into a float target at
  the picture's size, one for the mask and the encode into the swapchain
  (`SDR`, or `SDR_LINEAR` and let the hardware encode). The settings are
  `platform_set_crt`'s, switched off where `CrtDisplay` says the display
  cannot have them. Checked against `crt_ref`'s crops by reading the
  swapchain back.
* **Phase 3, HDR.** (Done in build 108; "Phase 3 (build 108)" below.)
  `HDR10_ST2084` (Rec.2020, PQ) or `HDR_EXTENDED_LINEAR`
  (scRGB), whichever the probe shows each screen offers; PEAK and WHITE
  from the HDR page; the depths from `crt_plan_resolve`. Follows
  `SDL_EVENT_WINDOW_HDR_STATE_CHANGED`.
* Not planned: curvature, halation, NTSC composite artefacts (the port is an
  RGB feed), a slider for the mask's depth (PEAK and WHITE set it).

## Phase 2 (build 107): SDR through SDL_GPU

**Where it draws.** `platform_sdl3.c` opens its window, then tries SDL_GPU:
a device (SPIR-V, DXIL or MSL), the four compiled shaders from `shaders/`,
the window claimed with an SDR swapchain (8-bit, sRGB-encoded) and vsync,
and three pipelines for the swapchain's format. If any of that fails --
no device, no `shaders/` (before `make shaders`), `PS2_GPU=0` -- it draws
through SDL_Renderer as before (the placeholder, kept whole). The startup
line says which: `display: video wayland, SDL_GPU vulkan, SPIR-V shaders,
...` or `..., renderer ... (the placeholder)`. `PS2_GPU_DEBUG=1` asks for
SDL_GPU's debug layer.

**What it draws.** Every frame, from the swapchain texture's size: the CRT
page's settings, each switched off where the display cannot have it by the
rules the page shows (`crt_scanlines_why`, `crt_mask_why` against the same
`CrtDisplay`, so the page's value and the picture agree), resolved into a
`CrtPlan` in SDR. If neither scanlines nor a mask is left -- the page's
summary says OFF -- the flat picture: the placeholder's layout
(`crt_sharp_layout`, the arithmetic `frame_layout` had) and its look,
nearest to the whole multiple and linear down across, the ladder's 8-bit
colours. Else the CRT. HDR ON is drawn SDR until phase 3. A line on stderr
whenever it changes: `crt: scanlines, aperture grille (RGB), 3840x2400,
k 10, triad 4, SDR` or `crt: off, the flat picture, 1824x1277`.

**How: the model as tables, the shaders only sum.** The model's sum at a
display pixel is, over source columns i and lines j, the column's weight
across x the pixel's level x its line's beam share of the row. The weight
across depends on neither j nor the level, so the sum can be taken down
first and across second, exactly:

1. **Down** (`crt_down_rgb.frag.hlsl`; `crt_down.frag.hlsl` until build
   141a), into a float target 320 wide and as tall as the output (RGBA32
   float; 16 only where 32 cannot be a target): at column i, row y, the
   seven lines around y, each gun's value from the **beam table** -- by the
   gun's 8-bit value in the colour plane (256 rows, build 141a; by its DAC
   level, 8 rows, before) and the row's offset from the line's top (-3k ..
   4k-1): the value's drive x `beam_row` with that drive's `beam_sigma` and
   the plan's scan depth. The ladder's eight values have the rows the eight
   levels had, the same numbers.
2. **Across** (`crt_across.frag.hlsl`), into the swapchain: eight columns
   of the down pass from the display column's first (`lo`), each times its
   weight from the **across table** (`across_weight`: the source pixel's
   box through the beam's spread over the column's footprint; zero off the
   frame), times the **mask table** (`crt_mask_at` over two triads by one
   line -- it repeats with that period -- depth applied), times the scale,
   then sRGB's curve (build 108: or phase 3's HDR encodes). The mask table's
   row is (build 108) `(y + mask_phase) % k`, `mask_phase` being -pic_y
   wrapped into 0..k-1 on the CPU, so the remainder's dividend is never
   negative. Build 107 took `(y - pic_y) % k` and added k when it came out
   negative: above the picture's top -- the first line's beam tail --
   that is a remainder of a negative number, which HLSL leaves undefined,
   and the owner's NVIDIA GPU (Vulkan) made it an unsigned remainder, the slot
   and shadow masks' rows shifted a few rows there (his crt_gpu_check run:
   six FAILs, the title whole against the mirror; re-rendering those rows
   as an unsigned remainder gives his worst codes exactly in five of the
   six, the sixth one code off).

`crt_tables_build` (crt.c) makes the three tables with the functions
`crt_render` uses -- `across_weight` and `across_span` were taken out of
`crt_render` for it, its crops byte for byte the same -- so tuning the model
moves the GPU with it, and a shader has no constant of its own. The tables
are made again only when the plan changes (a resize, FULLSCREEN, a setting);
every frame uploads only the frame: (build 141a) its colour plane, 320x224
RGBA8 (each gun's 8-bit value / 255), the game's colours and the Escape
menu's overlay as they are -- the colour path's (docs/palette_effects.md).
Until 141a it was the index plane, R8 (index / 255), and the 80 palette
entries' levels, 80x1 RGBA8 (the game's 64 and the overlay's line). The flat
picture (`crt_sharp_rgb.frag.hlsl`) takes the colour as it is; with
`PS2_DRAW=palette` the platform hands the chain the palette path's colours
instead, the reference.

**The shaders** are HLSL, `src/shaders/`, compiled offline by `make
shaders` with SDL_shadercross into `shaders/<name>.spv | .dxil | .msl`
(entry point `main`; `main0` for Metal, which SPIRV-Cross renames it to).
Resources as SDL_GPU wants them: sampled textures and samplers in space 2,
the uniform block in space 3, every uniform member a whole 16-byte register
so the three backends lay them out alike. The fragment shaders read with
`Load` (no filtering) and work from `SV_Position` alone.

**Cost.** Down: 320 x the output's rows x 7 lines. Across: every display
pixel x 8 taps and a mask read. At 3840x2400 that is about 9 M pixels,
~80 M loads a frame.

## Phase 3 (build 108): HDR

**When.** HDR ON (the HDR page) where SDL says the window has HDR -- the
page's NO HDR rule, `SDL_PROP_WINDOW_HDR_ENABLED_BOOLEAN` -- and the window
offers an HDR swapchain: `HDR10_ST2084` (Rec.2020, PQ, R10G10B10A2; the only
one Direct3D 12 offers on the Windows PC) first, else `HDR_EXTENDED_LINEAR`
(scRGB, RGBA16 float). `PS2_HDR_SWAPCHAIN=scrgb` asks for scRGB first,
`=pq` for HDR10 only. Otherwise SDR, as phase 2. `platform_sdl3.c`
(`gpu_follow_hdr`, each frame before the swapchain texture is acquired)
switches the swapchain with `SDL_SetGPUSwapchainParameters` after
`SDL_WaitForGPUIdle` and remakes the three pipelines for its format; a
composition SDL refuses is not asked for again until the window's HDR
state changes, and SDR is the floor. `SDL_EVENT_WINDOW_HDR_STATE_CHANGED`
(and `..._DISPLAY_CHANGED`) makes it ask the window again what it offers,
so switching HDR off and on in the desktop is followed from the next frame.
Lines on stderr: `display: HDR on, headroom 1.624, SDR white 1.000 (203
nits), AUTO 330 nits` whenever the window's HDR state changes, `display:
swapchain HDR10 (Rec.2020, PQ), texture format 8` on a switch, and the
`crt:` line with the encode, WHITE, PEAK and the two depths, or `SDR (HDR
ON, but the window has no HDR)` / `(... but no HDR swapchain the window
offers)`.

**The plan.** `crt_plan_now` sets `hdr_out` and the encode from the
swapchain, PEAK from the page (AUTO: `crt_peak_auto`) and WHITE, and
`crt_plan_resolve` works the depths out as the model always has (the
scanlines first, the mask the headroom left). The light does not depend on
the encode.

**The encodes** (`CrtEncode`, `crt_encode_params`; the across pass's
`encode_out`, `crt_encode_f` in C):

* HDR10: the nits into Rec.2020 (BT.2087's matrix, `crt_709_to_2020`'s,
  handed up as three uniform rows), over 10000, through ST 2084 -- the
  model's `pq1000` encode.
* scRGB: the nits over the nits of scRGB's 1.0, Rec.709, no curve. SDL's
  SDR white is `sdr_white` of its linear units and `sdr_nits` nits, so 1.0
  is `sdr_nits / sdr_white`: 80 on Windows (scRGB), and on Wayland and macOS
  -- where SDL's SDR white is 1.0 and its nits are not said -- the 203 the
  platform takes it as (BT.2408), the same figure PEAK's AUTO uses.
* The flat picture with HDR ON (CRT off): its SDR colour, sRGB-decoded,
  times WHITE in nits, then the same encode (`crt_encode_flat_f`). The
  game's white is WHITE whether the CRT is on or off. (build 108a) The flat
  pass finds each pixel's place in the step in whole numbers (u = nu / 2w,
  nu = (2 (x - dst.x) + 1) step_w - w) and divides only for the blend's
  fraction: 108 took it in float, and near black the GPU's own rounding of
  that division came out several 10-bit PQ codes from the CPU's on the P16
  (history, Build 108a).

`crt_gpu.c`'s `same_plan` (whether the tables are made again) compares the
encode, scRGB's unit and WHITE too: a plan alike in light but for another
swapchain is not the same tables (the mock caught scRGB reusing HDR10's).

## The read-back check

Three levels, the first two run here:

* `make crt_sim_check` (`crt_ref sim`): the tables through
  `crt_tables_render`, the two passes in C and in float as the shaders do
  them, against `crt_render` for every SDR crop `crop` writes, and the mask
  table against `crt_mask_at` at every pixel of every display: 32 checks;
  every code within 1 of the model, 16 of 3.8 M off by one (the encode's
  rounding). (build 108) 38: the across pass's row at every row of each
  display -- the remainder's dividend never negative, the row the model's
  (fails on 107's arithmetic at row 0 of every display) -- and the pq1000
  crops through the HDR10 and scRGB encodes (scRGB's 1.0 at 203 nits), in
  10-bit PQ codes, within 1. route_out/crt_reference/sim_check.txt.
* `make crt_gpu_check`, then `./crt_gpu_check` (SDL3, run from the tree's
  top after `make shaders`): on the machine's own GPU. It opens a window
  as the game does, claims it with the game's SDR swapchain, and draws with
  crt_gpu.c -- the game's passes and pipelines, built for the swapchain's
  format -- into textures of that format at each display's size, and reads
  them back. (A swapchain texture is for drawing into and presenting;
  SDL_GPU does not promise it can be copied out of, so the frames checked
  are the same pipelines' into the same format. The window shows the flat
  picture meanwhile.) It checks every SDR crop of the title's logo and
  Rudo's HP box against the model and the mirror; every such output of the
  title whole, bars and beam tails too, against the mirror; and the flat
  picture whole at the four displays and a game window's two sizes
  (960x672, the P16's 1824x1277) at each pixel aspect against its own
  mirror. 72 checks, each "within 1 code". It writes the GPU's crops to
  route_out/crt_gpu_check/, named as crt_ref names its own, and report.txt.
  (build 108) 84: the pq1000 crops and the title whole in HDR -- the 4K
  grille and the P16's shadow mask by HDR10 into an R10G10B10A2 target,
  the P16's by scRGB (1.0 at 203 nits) into an RGBA16 float one -- and the
  flat picture at WHITE 200 in HDR10 (1080p, 3840x2400) and scRGB
  (3840x2400), all held in 10-bit PQ codes (scRGB taken back to nits,
  into Rec.2020 and PQ). A crop now passes only within 1 of both the model
  and the mirror, and under a FAIL a line says where: the rows and columns
  more than a code apart, the picture's rows, the worst pixel and its two
  codes.
* Built here only against a mock of SDL_GPU (not packed): textures in
  memory, copy passes that move the bytes, render passes that run the
  four shaders as C written line for line from the HLSL, a load outside a
  texture an abort. (build 108, rebuilt: 107's was not packed) A remainder
  of a negative number an abort too, as HLSL leaves it undefined -- on it
  107's crt_gpu_check with 107's shader aborts at 1080p's first row (-92 %
  4), which 107's mock, taking C's remainder, let through; the swapchain's
  compositions, HDR10's and scRGB's formats, the window's HDR properties
  and SDL_EVENT_WINDOW_HDR_STATE_CHANGED; each cbuffer's size checked
  against its HLSL packing. On it: crt_gpu_check 84 of 84; the game's own
  `platform_present` at 1080p scanlines and grille byte for byte crt_ref's
  crop, the P16 (3840x2400, OTHER, shadow, HDR10 and scRGB) and the 4K RGB
  grille (HDR10) within one 10-bit code of the pq1000 crops, HDR switched
  off and on following, HDR ON on a window without HDR drawn SDR, the flat
  picture in HDR10. (build 108a) Its float divisions are a reciprocal and
  a multiply, as a GPU may do them, not C's correctly rounded quotient: 108's
  flat shader fails there as on the P16. Build 107's run on its own mock: the real
  `crt_gpu_check` against it 72 of 72, most
  crops 0 codes from the model. The game's own `platform_present` against
  it at 1920x1080: scanlines and the grille byte for byte crt_ref's crop; a
  slot mask switched off (1440p only) and the frame the scanlines crop; the
  CRT off, the flat picture. What that proves: the tables, the uploads'
  offsets and sizes, the uniform blocks, the bindings, the BGRA read-back
  and the platform's choice. What it cannot: that the HLSL compiles, that
  SDL_shadercross and SDL_GPU treat it as written, that a GPU gives what
  C gives where HLSL leaves the result undefined (107's lesson), and how
  fast it is.

## Interlaced scanlines (builds 159 to 166; taken out in build 167)

Builds 159 to 159b gave SCANLINES a third value, INTERLACED (The owner:
"an interlaced scanline mode to simulate old televisions alternating
every other scanline"), and a FLICKER row under it (FULL, SOFT, NONE):
each line's beam a quarter line up on one frame and a quarter line down
on the next, as a set draws an interlaced signal's two fields, with the
field before held under each at a third by default. docs/history.md,
builds 159 to 159b, has how it was modelled and why the third.

Build 167 takes it out. The owner, having read that the Mega Drive
draws most games in "240p": "Remove the interlaced effect please". This
game is one of them: the VDP sends 262 lines a field, every field on the
same lines (the dark gaps between them are what SCANLINES ON draws), and
the game sets register 12 to $81 once at boot (H40, the interlace bits
clear; ps2_types.h) and never changes it. INTERLACED showed a picture the
game never sent. SCANLINES is OFF or ON again: the beam table one field,
DownParams four registers, `crt_gpu_draw_crt` without a field, and
`crt.c`, `crt.h`, `crt_gpu.c`, the down shader (`make shaders` gives
build 158d's bytes again) and the tools build 158d's. A settings.json
from builds 159 to 166 with `"scanlines": "interlaced"` reads as ON
(scanlines were what INTERLACED drew); its `"flicker"` is passed over and
not written again (tools/controls_check.py, part 4).
