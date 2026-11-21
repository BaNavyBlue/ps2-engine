#include "crt_gpu.h"

#include "../core/assetfs.h"

#include <stdio.h>
#include <string.h>

/* (build 107) crt_gpu.h says what this is; docs/crt.md, "Phase 2", why. */

/* The shaders, by file stem (shaders/<stem>.<spv|dxil|msl>), and what each
 * binds: sampled textures (each with a sampler) and uniform buffers. */
typedef enum {
    SH_VERT = 0,        /* one triangle over the target                  */
    SH_DOWN,            /* the beams down                                */
    SH_ACROSS,          /* across, the mask, the encode                  */
    SH_SHARP,           /* the flat picture                              */
    SH_COUNT
} ShaderId;

static const struct {
    const char        *stem;
    SDL_GPUShaderStage stage;
    Uint32             samplers;
    Uint32             uniforms;
} SHADER[SH_COUNT] = {
    { "crt_fullscreen.vert", SDL_GPU_SHADERSTAGE_VERTEX, 0, 0 },
    /* (build 141a) The colour plane's: the frame and the beam by value,
     * and the frame alone (crt_down and crt_sharp took the index plane and
     * a palette texture, three and two). */
    { "crt_down_rgb.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, 2, 1 },
    { "crt_across.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, 3, 1 },
    { "crt_sharp_rgb.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1 },
};

/* The uniform blocks, laid out as the shaders' cbuffers: every member a
 * whole 16-byte register, so HLSL's packing, std140 (SPIR-V) and Metal's
 * all agree. */
typedef struct {
    Sint32 pic_y, k, reach, lines;
} DownParams;

typedef struct {
    float  gain[4];
    float  to2020[3][4];            /* (build 108) rows, w unused */
    float  enc[4];                  /* x: the light into the encode */
    Sint32 mask_phase, k, mask_w, src_w;
    Sint32 encode, pad[3];          /* (build 108) CrtEncode */
} AcrossParams;

typedef struct {
    float  dst[4];
    Sint32 step[4];
    float  to2020[3][4];            /* (build 108) as AcrossParams */
    float  enc[4];                  /* x: into the encode, y: WHITE */
    Sint32 mode[4];                 /* x: CrtEncode */
} SharpParams;

/* (build 108) The encode's numbers into a uniform block's rows. */
static void put_encode(const CrtEncodeParams *e, float to2020[3][4],
                       float enc[4])
{
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) {
            to2020[r][c] = e->to2020[r][c];
        }
        to2020[r][3] = 0.0f;
    }
    enc[0] = e->scale;
    enc[1] = e->white;
    enc[2] = 0.0f;
    enc[3] = 0.0f;
}

/* The frame's upload: (build 141a) the colour plane, four bytes a pixel
 * (until 141a the index plane, then the palette's levels). */
#define FRAME_BYTES    (SCREEN_W * SCREEN_H * 4)
#define UPLOAD_BYTES   FRAME_BYTES

struct CrtGpu {
    SDL_GPUDevice          *dev;
    SDL_GPUShaderFormat     format;
    SDL_GPUShader          *shader[SH_COUNT];
    SDL_GPUGraphicsPipeline *down_pipe, *across_pipe, *sharp_pipe;
    SDL_GPUTextureFormat    target_format;
    SDL_GPUTextureFormat    down_format;    /* RGBA32 float, else 16 */
    SDL_GPUSampler         *nearest;

    SDL_GPUTexture         *frame;          /* 320x224 RGBA8: colour     */
    SDL_GPUTransferBuffer  *upload;

    /* The plan's: remade when it changes. */
    bool                    have_plan;
    CrtPlan                 plan;
    CrtTables               tables;
    SDL_GPUTexture         *beam, *across, *mask;
    SDL_GPUTexture         *down;           /* 320 x out_h               */
    int                     down_h;
};

static const char *ext_of(SDL_GPUShaderFormat f)
{
    if (f == SDL_GPU_SHADERFORMAT_SPIRV) {
        return "spv";
    }
    if (f == SDL_GPU_SHADERFORMAT_DXIL) {
        return "dxil";
    }
    return "msl";
}

const char *crt_gpu_shader_format(const CrtGpu *g)
{
    if (g->format == SDL_GPU_SHADERFORMAT_SPIRV) {
        return "SPIR-V";
    }
    if (g->format == SDL_GPU_SHADERFORMAT_DXIL) {
        return "DXIL";
    }
    return "MSL";
}

static SDL_GPUShader *load_shader(CrtGpu *g, const char *dir, ShaderId id)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s.%s", dir, SHADER[id].stem,
             ext_of(g->format));
    size_t size = 0;
    /* (build 125) The game's pack first (assetfs.h), else the disk. */
    const u8 *packed = assetfs_pack_find(path, &size);
    void  *code = (packed != NULL) ? SDL_malloc(size)
                                   : SDL_LoadFile(path, &size);
    if (code == NULL) {
        fprintf(stderr, "crt: no %s (`make shaders` writes it)\n", path);
        return NULL;
    }
    if (packed != NULL) {
        memcpy(code, packed, size);
    }
    SDL_GPUShaderCreateInfo info;
    SDL_zero(info);
    info.code_size   = size;
    info.code        = (const Uint8 *)code;
    /* SPIRV-Cross renames an entry point called main to main0 for Metal,
     * where main is reserved. */
    info.entrypoint  = (g->format == SDL_GPU_SHADERFORMAT_MSL) ? "main0"
                                                               : "main";
    info.format      = g->format;
    info.stage       = SHADER[id].stage;
    info.num_samplers        = SHADER[id].samplers;
    info.num_uniform_buffers = SHADER[id].uniforms;
    SDL_GPUShader *sh = SDL_CreateGPUShader(g->dev, &info);
    SDL_free(code);
    if (sh == NULL) {
        fprintf(stderr, "crt: %s refused (%s)\n", path, SDL_GetError());
    }
    return sh;
}

static SDL_GPUTexture *make_texture(SDL_GPUDevice *dev,
                                    SDL_GPUTextureFormat format, int w,
                                    int h, SDL_GPUTextureUsageFlags usage)
{
    SDL_GPUTextureCreateInfo info;
    SDL_zero(info);
    info.type                 = SDL_GPU_TEXTURETYPE_2D;
    info.format               = format;
    info.usage                = usage;
    info.width                = (Uint32)w;
    info.height               = (Uint32)h;
    info.layer_count_or_depth = 1;
    info.num_levels           = 1;
    info.sample_count         = SDL_GPU_SAMPLECOUNT_1;
    SDL_GPUTexture *t = SDL_CreateGPUTexture(dev, &info);
    if (t == NULL) {
        fprintf(stderr, "crt: a %dx%d texture failed (%s)\n", w, h,
                SDL_GetError());
    }
    return t;
}

CrtGpu *crt_gpu_create(SDL_GPUDevice *dev, const char *dir)
{
    CrtGpu *g = SDL_calloc(1, sizeof *g);
    if (g == NULL) {
        return NULL;
    }
    g->dev = dev;
    SDL_GPUShaderFormat have = SDL_GetGPUShaderFormats(dev);
    if (have & SDL_GPU_SHADERFORMAT_SPIRV) {
        g->format = SDL_GPU_SHADERFORMAT_SPIRV;
    } else if (have & SDL_GPU_SHADERFORMAT_DXIL) {
        g->format = SDL_GPU_SHADERFORMAT_DXIL;
    } else if (have & SDL_GPU_SHADERFORMAT_MSL) {
        g->format = SDL_GPU_SHADERFORMAT_MSL;
    } else {
        fprintf(stderr, "crt: the device takes none of SPIR-V, DXIL, MSL\n");
        SDL_free(g);
        return NULL;
    }
    for (int i = 0; i < SH_COUNT; i++) {
        g->shader[i] = load_shader(g, dir, (ShaderId)i);
        if (g->shader[i] == NULL) {
            crt_gpu_destroy(g);
            return NULL;
        }
    }

    SDL_GPUSamplerCreateInfo s;
    SDL_zero(s);
    s.min_filter     = SDL_GPU_FILTER_NEAREST;
    s.mag_filter     = SDL_GPU_FILTER_NEAREST;
    s.mipmap_mode    = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    s.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    s.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    s.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    g->nearest = SDL_CreateGPUSampler(dev, &s);

    /* The down pass's sums in 32-bit float; 16 only where 32 cannot be a
     * target (a half's 11 bits would move some codes by one). */
    SDL_GPUTextureUsageFlags rt = SDL_GPU_TEXTUREUSAGE_SAMPLER
                                | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    g->down_format = SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
    if (!SDL_GPUTextureSupportsFormat(dev, g->down_format,
                                      SDL_GPU_TEXTURETYPE_2D, rt)) {
        g->down_format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
        fprintf(stderr, "crt: no RGBA32 float target; the beams down in "
                        "16-bit float\n");
    }

    g->frame   = make_texture(dev, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                              SCREEN_W, SCREEN_H,
                              SDL_GPU_TEXTUREUSAGE_SAMPLER);
    SDL_GPUTransferBufferCreateInfo tb;
    SDL_zero(tb);
    tb.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tb.size  = UPLOAD_BYTES;
    g->upload = SDL_CreateGPUTransferBuffer(dev, &tb);
    if (g->nearest == NULL || g->frame == NULL || g->upload == NULL) {
        fprintf(stderr, "crt: SDL_GPU resources failed (%s)\n",
                SDL_GetError());
        crt_gpu_destroy(g);
        return NULL;
    }
    return g;
}

static SDL_GPUGraphicsPipeline *make_pipeline(CrtGpu *g, ShaderId frag,
                                              SDL_GPUTextureFormat format)
{
    SDL_GPUColorTargetDescription target;
    SDL_zero(target);
    target.format = format;             /* no blending: every pixel written */

    SDL_GPUGraphicsPipelineCreateInfo info;
    SDL_zero(info);
    info.vertex_shader   = g->shader[SH_VERT];
    info.fragment_shader = g->shader[frag];
    info.primitive_type  = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    info.rasterizer_state.fill_mode  = SDL_GPU_FILLMODE_FILL;
    info.rasterizer_state.cull_mode  = SDL_GPU_CULLMODE_NONE;
    info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    info.target_info.color_target_descriptions = &target;
    info.target_info.num_color_targets         = 1;
    SDL_GPUGraphicsPipeline *p = SDL_CreateGPUGraphicsPipeline(g->dev, &info);
    if (p == NULL) {
        fprintf(stderr, "crt: the %s pipeline failed (%s)\n",
                SHADER[frag].stem, SDL_GetError());
    }
    return p;
}

static void release_pipelines(CrtGpu *g)
{
    SDL_GPUGraphicsPipeline **all[3] = { &g->down_pipe, &g->across_pipe,
                                         &g->sharp_pipe };
    for (int i = 0; i < 3; i++) {
        if (*all[i] != NULL) {
            SDL_ReleaseGPUGraphicsPipeline(g->dev, *all[i]);
            *all[i] = NULL;
        }
    }
}

bool crt_gpu_set_target(CrtGpu *g, SDL_GPUTextureFormat format)
{
    release_pipelines(g);
    g->target_format = format;
    g->down_pipe   = make_pipeline(g, SH_DOWN, g->down_format);
    g->across_pipe = make_pipeline(g, SH_ACROSS, format);
    g->sharp_pipe  = make_pipeline(g, SH_SHARP, format);
    return g->down_pipe != NULL && g->across_pipe != NULL
        && g->sharp_pipe != NULL;
}

static void release_texture(CrtGpu *g, SDL_GPUTexture **t)
{
    if (*t != NULL) {
        SDL_ReleaseGPUTexture(g->dev, *t);
        *t = NULL;
    }
}

static void drop_plan(CrtGpu *g)
{
    release_texture(g, &g->beam);
    release_texture(g, &g->across);
    release_texture(g, &g->mask);
    crt_tables_free(&g->tables);
    g->have_plan = false;
}

void crt_gpu_destroy(CrtGpu *g)
{
    if (g == NULL) {
        return;
    }
    drop_plan(g);
    release_texture(g, &g->down);
    release_texture(g, &g->frame);
    release_pipelines(g);
    if (g->upload != NULL) {
        SDL_ReleaseGPUTransferBuffer(g->dev, g->upload);
    }
    if (g->nearest != NULL) {
        SDL_ReleaseGPUSampler(g->dev, g->nearest);
    }
    for (int i = 0; i < SH_COUNT; i++) {
        if (g->shader[i] != NULL) {
            SDL_ReleaseGPUShader(g->dev, g->shader[i]);
        }
    }
    SDL_free(g);
}

/* What the tables are made from: two plans with these the same draw the
 * same. */
static bool same_plan(const CrtPlan *a, const CrtPlan *b)
{
    return a->out_w == b->out_w && a->out_h == b->out_h
        && a->par == b->par && a->k == b->k && a->pic_x == b->pic_x
        && a->pic_y == b->pic_y && a->pic_w == b->pic_w
        && a->triad == b->triad && a->set.mask == b->set.mask
        && a->set.subpixels == b->set.subpixels
        && a->scan_depth == b->scan_depth && a->mask_depth == b->mask_depth
        && a->scale == b->scale && a->white_gain[0] == b->white_gain[0]
        && a->white_gain[1] == b->white_gain[1]
        && a->white_gain[2] == b->white_gain[2]
        /* (build 108) The encode: two plans alike in light but not in
         * swapchain (HDR10 and scRGB) are not the same tables. */
        && a->hdr_out == b->hdr_out && a->encode == b->encode
        && a->scrgb_nits == b->scrgb_nits
        && a->set.white_nits == b->set.white_nits;
}

/* One texture's upload from a transfer buffer at `offset`. */
static void upload_region(SDL_GPUCopyPass *pass, SDL_GPUTransferBuffer *tb,
                          Uint32 offset, SDL_GPUTexture *t, int w, int h)
{
    SDL_GPUTextureTransferInfo src;
    SDL_zero(src);
    src.transfer_buffer = tb;
    src.offset          = offset;
    src.pixels_per_row  = (Uint32)w;
    src.rows_per_layer  = (Uint32)h;
    SDL_GPUTextureRegion dst;
    SDL_zero(dst);
    dst.texture = t;
    dst.w       = (Uint32)w;
    dst.h       = (Uint32)h;
    dst.d       = 1;
    SDL_UploadToGPUTexture(pass, &src, &dst, false);
}

static Uint32 align512(Uint32 v)
{
    return (v + 511u) / 512u * 512u;
}

/* The plan's tables as textures, uploaded in `pass`. */
static bool make_plan(CrtGpu *g, SDL_GPUCopyPass *pass, const CrtPlan *p)
{
    drop_plan(g);
    if (!crt_tables_build(p, &g->tables)) {
        fprintf(stderr, "crt: the plan's tables could not be made\n");
        return false;
    }
    const CrtTables *t = &g->tables;
    g->beam   = make_texture(g->dev, SDL_GPU_TEXTUREFORMAT_R32_FLOAT,
                             t->beam_w, CRT_BEAM_ROWS,
                             SDL_GPU_TEXTUREUSAGE_SAMPLER);
    g->across = make_texture(g->dev, SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT,
                             t->across_w, 3, SDL_GPU_TEXTUREUSAGE_SAMPLER);
    g->mask   = make_texture(g->dev, SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT,
                             t->mask_w, t->mask_h,
                             SDL_GPU_TEXTUREUSAGE_SAMPLER);
    if (p->out_h != g->down_h || g->down == NULL) {
        release_texture(g, &g->down);
        g->down   = make_texture(g->dev, g->down_format, SCREEN_W, p->out_h,
                                 SDL_GPU_TEXTUREUSAGE_SAMPLER
                                 | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET);
        g->down_h = (g->down != NULL) ? p->out_h : 0;
    }
    if (g->beam == NULL || g->across == NULL || g->mask == NULL
        || g->down == NULL) {
        drop_plan(g);
        return false;
    }

    Uint32 beam_bytes   = (Uint32)(sizeof(float) * t->beam_w
                                   * CRT_BEAM_ROWS);
    Uint32 across_bytes = (Uint32)(sizeof(float) * 4 * t->across_w * 3);
    Uint32 mask_bytes   = (Uint32)(sizeof(float) * 4 * t->mask_w * t->mask_h);
    Uint32 at_across    = align512(beam_bytes);
    Uint32 at_mask      = at_across + align512(across_bytes);

    SDL_GPUTransferBufferCreateInfo info;
    SDL_zero(info);
    info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    info.size  = at_mask + mask_bytes;
    SDL_GPUTransferBuffer *tb = SDL_CreateGPUTransferBuffer(g->dev, &info);
    Uint8 *map = (tb != NULL) ? SDL_MapGPUTransferBuffer(g->dev, tb, false)
                              : NULL;
    if (map == NULL) {
        if (tb != NULL) {
            SDL_ReleaseGPUTransferBuffer(g->dev, tb);
        }
        drop_plan(g);
        return false;
    }
    memcpy(map, t->beam, beam_bytes);
    memcpy(map + at_across, t->across, across_bytes);
    memcpy(map + at_mask, t->mask, mask_bytes);
    SDL_UnmapGPUTransferBuffer(g->dev, tb);
    upload_region(pass, tb, 0, g->beam, t->beam_w, CRT_BEAM_ROWS);
    upload_region(pass, tb, at_across, g->across, t->across_w, 3);
    upload_region(pass, tb, at_mask, g->mask, t->mask_w, t->mask_h);
    /* Released now, freed by SDL once the copy has run. */
    SDL_ReleaseGPUTransferBuffer(g->dev, tb);

    g->plan      = *p;
    g->have_plan = true;
    return true;
}

bool crt_gpu_upload(CrtGpu *g, SDL_GPUCommandBuffer *cmd, const Rgb8 *colour,
                    const CrtPlan *plan)
{
    Uint8 *map = SDL_MapGPUTransferBuffer(g->dev, g->upload, true);
    if (map == NULL) {
        return false;
    }
    for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
        Uint8 *o = map + i * 4;
        o[0] = colour[i].r;
        o[1] = colour[i].g;
        o[2] = colour[i].b;
        o[3] = 255;
    }
    SDL_UnmapGPUTransferBuffer(g->dev, g->upload);

    SDL_GPUCopyPass *pass = SDL_BeginGPUCopyPass(cmd);
    if (pass == NULL) {
        return false;
    }
    upload_region(pass, g->upload, 0, g->frame, SCREEN_W, SCREEN_H);
    bool ok = true;
    if (plan != NULL && !(g->have_plan && same_plan(&g->plan, plan))) {
        ok = make_plan(g, pass, plan);
    }
    SDL_EndGPUCopyPass(pass);
    return ok;
}

static SDL_GPURenderPass *begin(SDL_GPUCommandBuffer *cmd,
                                SDL_GPUTexture *target)
{
    SDL_GPUColorTargetInfo info;
    SDL_zero(info);
    info.texture  = target;
    info.load_op  = SDL_GPU_LOADOP_DONT_CARE;  /* every pixel is written */
    info.store_op = SDL_GPU_STOREOP_STORE;
    return SDL_BeginGPURenderPass(cmd, &info, 1, NULL);
}

static void bind3(CrtGpu *g, SDL_GPURenderPass *pass, SDL_GPUTexture *a,
                  SDL_GPUTexture *b, SDL_GPUTexture *c, Uint32 n)
{
    SDL_GPUTextureSamplerBinding bind[3];
    SDL_GPUTexture *tex[3] = { a, b, c };
    for (Uint32 i = 0; i < 3; i++) {
        bind[i].texture = tex[i];
        bind[i].sampler = g->nearest;
    }
    SDL_BindGPUFragmentSamplers(pass, 0, bind, n);
}

void crt_gpu_draw_crt(CrtGpu *g, SDL_GPUCommandBuffer *cmd,
                      SDL_GPUTexture *target)
{
    if (!g->have_plan) {
        return;
    }
    const CrtTables *t = &g->tables;

    SDL_GPURenderPass *pass = begin(cmd, g->down);
    if (pass == NULL) {
        return;
    }
    DownParams dp = { t->pic_y, t->k, CRT_LINE_REACH, SCREEN_H };
    SDL_BindGPUGraphicsPipeline(pass, g->down_pipe);
    bind3(g, pass, g->frame, g->beam, NULL, 2);
    SDL_PushGPUFragmentUniformData(cmd, 0, &dp, sizeof dp);
    SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
    SDL_EndGPURenderPass(pass);

    pass = begin(cmd, target);
    if (pass == NULL) {
        return;
    }
    AcrossParams ap;
    SDL_zero(ap);
    ap.gain[0] = t->gain[0];
    ap.gain[1] = t->gain[1];
    ap.gain[2] = t->gain[2];
    ap.gain[3] = 1.0f;
    put_encode(&t->enc, ap.to2020, ap.enc);
    ap.mask_phase = t->mask_phase;
    ap.k          = t->k;
    ap.mask_w     = t->mask_w;
    ap.src_w      = SCREEN_W;
    ap.encode     = (Sint32)t->enc.encode;
    SDL_BindGPUGraphicsPipeline(pass, g->across_pipe);
    bind3(g, pass, g->down, g->across, g->mask, 3);
    SDL_PushGPUFragmentUniformData(cmd, 0, &ap, sizeof ap);
    SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
    SDL_EndGPURenderPass(pass);
}

void crt_gpu_draw_sharp(CrtGpu *g, SDL_GPUCommandBuffer *cmd,
                        SDL_GPUTexture *target, const CrtSharp *l,
                        const CrtEncodeParams *enc)
{
    CrtEncodeParams sdr;
    if (enc == NULL) {
        crt_encode_params(CRT_ENCODE_SRGB, 0.0, 0.0, &sdr);
        enc = &sdr;
    }
    SDL_GPURenderPass *pass = begin(cmd, target);
    if (pass == NULL) {
        return;
    }
    SharpParams sp;
    SDL_zero(sp);
    sp.dst[0]  = (float)l->x;
    sp.dst[1]  = (float)l->y;
    sp.dst[2]  = (float)l->w;
    sp.dst[3]  = (float)l->h;
    sp.step[0] = l->step_w;
    sp.step[1] = l->step_h;
    sp.step[2] = SCREEN_W;
    sp.step[3] = SCREEN_H;
    put_encode(enc, sp.to2020, sp.enc);
    sp.mode[0] = (Sint32)enc->encode;
    SDL_BindGPUGraphicsPipeline(pass, g->sharp_pipe);
    bind3(g, pass, g->frame, NULL, NULL, 1);
    SDL_PushGPUFragmentUniformData(cmd, 0, &sp, sizeof sp);
    SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
    SDL_EndGPURenderPass(pass);
}
