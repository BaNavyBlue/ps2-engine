// (build 107) The CRT chain's second pass: the beams across, the mask, the
// light and the encode, into the swapchain.
//
// At display pixel (x, y): eight columns of the down pass from Across's
// `lo` for this display column, each times its weight (the source pixel's
// box through the beam's spread, over the display column's footprint;
// zero off the frame), times the Mask table (by x modulo two triads and
// the row within its line, the plan's depth applied), times `gain` (the
// plan's scale and white), then the encode for the swapchain. Every weight
// and every encode number is crt.c's (crt_tables_build, crt_encode_params);
// src/core/crt.c's crt_tables_render, crt_tables_row and crt_encode_f are
// this, in C.
//
// (build 108) The row within the line is (y + mask_phase) % k, never a
// remainder of a negative number: HLSL leaves `%` undefined when the signs
// differ, and build 107's (y - pic_y) % k, above the picture's top, came
// out as an unsigned remainder on an NVIDIA GPU (Vulkan).
// (build 108) The encode: 0 the SDR swapchain's sRGB codes; 1 HDR10 --
// the nits into Rec.2020, times `enc.x` (1/10000), through ST 2084; 2
// scRGB -- the nits times `enc.x` (1 / the nits of 1.0), linear.

Texture2D<float4> Down    : register(t0, space2); // 320 x out_h RGBA32F
SamplerState      DownS   : register(s0, space2);
Texture2D<float4> Across  : register(t1, space2); // out_w x 3 RGBA32F
SamplerState      AcrossS : register(s1, space2);
Texture2D<float4> Mask    : register(t2, space2); // 2 triads x k RGBA32F
SamplerState      MaskS   : register(s2, space2);

cbuffer AcrossParams : register(b0, space3)
{
    float4 gain;        // rgb: scale x white_gain
    float4 to2020_0;    // (build 108) Rec.709 to Rec.2020, row by row (xyz)
    float4 to2020_1;
    float4 to2020_2;
    float4 enc;         // x: the light's factor into the encode
    int mask_phase;     // (build 108) -pic_y wrapped into 0..k-1
    int k;              // display rows a line
    int mask_w;         // the mask table's width: two triads
    int src_w;          // 320
    int encode;         // (build 108) 0 sRGB, 1 PQ, 2 scRGB
    int pad0;
    int pad1;
    int pad2;
};

float srgb_encode(float v)
{
    if (v <= 0.0) {
        return 0.0;
    }
    if (v >= 1.0) {
        return 1.0;
    }
    return (v <= 0.0031308) ? 12.92 * v : 1.055 * pow(v, 1.0 / 2.4) - 0.055;
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

float4 encode_out(float3 v)
{
    if (encode == 1) {
        float3 c = float3(dot(to2020_0.xyz, v), dot(to2020_1.xyz, v),
                          dot(to2020_2.xyz, v)) * enc.x;
        return float4(pq_encode(c.r), pq_encode(c.g), pq_encode(c.b), 1.0);
    }
    if (encode == 2) {
        return float4(v * enc.x, 1.0);
    }
    return float4(srgb_encode(v.r), srgb_encode(v.g), srgb_encode(v.b), 1.0);
}

float4 main(float4 position : SV_Position) : SV_Target0
{
    int x = (int)position.x;
    int y = (int)position.y;

    float4 w0 = Across.Load(int3(x, 0, 0));
    float4 w1 = Across.Load(int3(x, 1, 0));
    int    lo = (int)Across.Load(int3(x, 2, 0)).r;
    float  w[8] = { w0.x, w0.y, w0.z, w0.w, w1.x, w1.y, w1.z, w1.w };

    float3 acc = float3(0.0, 0.0, 0.0);
    [unroll]
    for (int t = 0; t < 8; t++) {
        int ic = clamp(lo + t, 0, src_w - 1);
        acc += w[t] * Down.Load(int3(ic, y, 0)).rgb;
    }

    int my = (y + mask_phase) % k;
    float3 m = Mask.Load(int3(x % mask_w, my, 0)).rgb;
    float3 v = gain.rgb * acc * m;
    return encode_out(v);
}
