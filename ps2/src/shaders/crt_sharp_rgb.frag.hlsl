// (build 107; build 141a the colour frame) The picture with the CRT off:
// what the SDR placeholder draws through SDL_Renderer, on the GPU. The frame nearest to the whole
// multiple step_w x step_h (320 m across, 224 k down), then linear down to
// the frame's place (dst) -- at most one blended column at each pixel's
// edge at a 32:35 or 14:15 pixel, nothing blended at 1:1. The colours are
// the frame's colour plane as it is (build 141a; until then the index plane
// through a palette texture of DAC levels and the ladder's 8-bit values,
// crt_sharp.frag.hlsl), written to the SDR swapchain as they are. Black
// outside dst.
//
// crt.c's crt_sharp_layout gives dst and the step (platform_sdl3.c's
// frame_layout); tools/crt_gpu_check.c has this in C.
//
// (build 108) On an HDR swapchain (HDR ON where the window has HDR, the
// CRT otherwise off) the same colour, sRGB-decoded, times the HDR page's
// WHITE in nits (`enc.y`), then the across pass's encode: HDR10's
// Rec.2020 and ST 2084, or scRGB's linear. crt.c's crt_encode_flat_f is
// that part in C.

Texture2D<float4> Frame   : register(t0, space2); // 320x224 RGBA8: colour / 255
SamplerState      FrameS  : register(s0, space2);

cbuffer Sharp : register(b0, space3)
{
    float4 dst;         // x, y, w, h in the target's pixels
    int4   step;        // step_w, step_h, 320, 224
    float4 to2020_0;    // (build 108) Rec.709 to Rec.2020, row by row (xyz)
    float4 to2020_1;
    float4 to2020_2;
    float4 enc;         // x: the light's factor into the encode, y: WHITE
    int4   mode;        // x: the encode, 0 sRGB (as is), 1 PQ, 2 scRGB
};

float srgb_decode(float v)
{
    if (v <= 0.0) {
        return 0.0;
    }
    if (v >= 1.0) {
        return 1.0;
    }
    return (v <= 0.04045) ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4);
}

// ST 2084: Y (nits / 10000) to the signal, clamped to 0..1.
float pq_encode(float y)
{
    if (y <= 0.0) {
        return 0.0;
    }
    y = min(y, 1.0);
    float p = pow(y, 2610.0 / 16384.0);
    return pow((3424.0 / 4096.0 + (2413.0 / 4096.0 * 32.0) * p)
               / (1.0 + (2392.0 / 4096.0 * 32.0) * p),
               2523.0 / 4096.0 * 128.0);
}

float4 encode_out(float3 code)
{
    if (mode.x == 0) {
        return float4(code, 1.0);
    }
    float3 v = float3(srgb_decode(code.r), srgb_decode(code.g),
                      srgb_decode(code.b)) * enc.y;
    if (mode.x == 1) {
        float3 c = float3(dot(to2020_0.xyz, v), dot(to2020_1.xyz, v),
                          dot(to2020_2.xyz, v)) * enc.x;
        return float4(pq_encode(c.r), pq_encode(c.g), pq_encode(c.b), 1.0);
    }
    return float4(v * enc.x, 1.0);
}

// Pixel (c, r) of the whole-multiple step: the source pixel under it.
float3 step_texel(int c, int r)
{
    int sx = (c * step.z) / step.x;
    int sy = (r * step.w) / step.y;
    return Frame.Load(int3(sx, sy, 0)).rgb;
}

// (build 108a) The pixel centre's place in the step, exactly. With dst's
// four numbers whole (crt_sharp_layout's), linear filtering's texel
// coordinate u = (x - dst.x + 0.5) * step_w / dst.w - 0.5 is nu / (2 dst.w)
// with nu = (2 (x - dst.x) + 1) step_w - dst.w, a whole number and never
// negative (step_w >= dst.w), so its texel and its remainder are integer
// arithmetic and only the blend's fraction is a division. Build 108 took u
// in float, px * step / dst, at magnitudes near 1000-3000, where a float's
// step is ~1e-4: a GPU rounds that division its own way, and where the CPU
// landed exactly on a texel beside black the GPU blended a trace of it in
// -- nothing in 8-bit sRGB, up to 6 10-bit codes through PQ's steep foot
// (The owner's P16: crt_gpu_check's three HDR flat pictures). A fraction that
// is exactly 0 on the CPU is 0 on every GPU now (0 divided is 0).
float4 main(float4 position : SV_Position) : SV_Target0
{
    int x  = (int)position.x;
    int y  = (int)position.y;
    int dx = (int)dst.x;
    int dy = (int)dst.y;
    int dw = (int)dst.z;
    int dh = (int)dst.w;
    if (x < dx || y < dy || x >= dx + dw || y >= dy + dh) {
        return encode_out(float3(0.0, 0.0, 0.0));
    }
    int   nu = (2 * (x - dx) + 1) * step.x - dw;
    int   nv = (2 * (y - dy) + 1) * step.y - dh;
    int   cu = nu / (2 * dw);
    int   cv = nv / (2 * dh);
    float fu = (float)(nu - cu * 2 * dw) / (float)(2 * dw);
    float fv = (float)(nv - cv * 2 * dh) / (float)(2 * dh);
    int   c0 = min(cu, step.x - 1);
    int   c1 = min(cu + 1, step.x - 1);
    int   r0 = min(cv, step.y - 1);
    int   r1 = min(cv + 1, step.y - 1);

    float3 t00 = step_texel(c0, r0);
    float3 t01 = step_texel(c1, r0);
    float3 t10 = step_texel(c0, r1);
    float3 t11 = step_texel(c1, r1);
    float3 top = t00 + fu * (t01 - t00);
    float3 bot = t10 + fu * (t11 - t10);
    return encode_out(top + fv * (bot - top));
}
