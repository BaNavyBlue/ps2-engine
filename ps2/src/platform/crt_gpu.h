#ifndef PS2_CRT_GPU_H
#define PS2_CRT_GPU_H

#include <SDL3/SDL.h>

#include "../core/crt.h"
#include "../core/video.h"

/* (build 107) The CRT simulation's phase 2: the chain on SDL_GPU, in SDR;
 * (build 108) phase 3, the HDR swapchains' encodes (the plan's, and the
 * flat picture's). docs/crt.md, "Phase 2" and "Phase 3".
 *
 * The frame goes up as a texture every frame: (build 141a) its colour
 * plane, the colour path's (until 141a the palette indices and each
 * index's three DAC levels). With the CRT on, two passes draw it -- the beams
 * down into a float target 320 wide and as tall as the output, then across,
 * the mask and sRGB's curve into the target -- from tables crt.c works out
 * with the model's own functions (crt_tables_build), made again only when
 * the plan changes. With it off, one pass draws the picture as the
 * SDL_Renderer placeholder did (crt_sharp_layout).
 *
 * The shaders are src/shaders/ (HLSL), compiled offline by `make shaders`
 * (SDL_shadercross) into shaders/ as SPIR-V, DXIL and MSL; this reads the
 * one the device takes. Used by platform_sdl3.c and tools/crt_gpu_check.c. */

typedef struct CrtGpu CrtGpu;

/* The shaders from `dir` in the format the device takes, and a sampler.
 * NULL, with the reason on stderr, when a file is missing (before `make
 * shaders` has been run) or the device refuses one. */
CrtGpu *crt_gpu_create(SDL_GPUDevice *dev, const char *dir);

/* The three pipelines, for a colour target of `format` (the swapchain's:
 * 8-bit UNORM for SDR, R10G10B10A2 for HDR10, RGBA16 float for scRGB).
 * Called again whenever the swapchain's composition changes. */
bool crt_gpu_set_target(CrtGpu *g, SDL_GPUTextureFormat format);

void crt_gpu_destroy(CrtGpu *g);

/* "SPIR-V", "DXIL" or "MSL": the shaders it read. */
const char *crt_gpu_shader_format(const CrtGpu *g);

/* A copy pass on `cmd`, before any render pass: the frame -- (build 141a)
 * its colour plane, 320 x 224 (the game's colours and the Escape menu's
 * overlay as they are; until 141a the index plane and FB_INDICES CRAM
 * words). With a plan (resolved), its tables when it differs from the last
 * one, and the down pass's target. False when the plan's tables could not
 * be made: draw the flat picture instead. */
bool crt_gpu_upload(CrtGpu *g, SDL_GPUCommandBuffer *cmd, const Rgb8 *colour,
                    const CrtPlan *plan);

/* The CRT, for the plan last uploaded, into the whole of `target` (the
 * plan's out_w x out_h). */
void crt_gpu_draw_crt(CrtGpu *g, SDL_GPUCommandBuffer *cmd,
                      SDL_GPUTexture *target);

/* The flat picture, where `l` says, into the whole of `target`, encoded
 * as `enc` says (build 108; NULL: the SDR swapchain's codes, as 107). */
void crt_gpu_draw_sharp(CrtGpu *g, SDL_GPUCommandBuffer *cmd,
                        SDL_GPUTexture *target, const CrtSharp *l,
                        const CrtEncodeParams *enc);

#endif /* PS2_CRT_GPU_H */
