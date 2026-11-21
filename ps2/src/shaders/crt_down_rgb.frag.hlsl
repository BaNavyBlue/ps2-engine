// (build 107; build 141a the colour frame) The CRT chain's first pass: the
// beams down.
//
// Target: 320 x the output's height, RGBA32 float. At source column i and
// display row y, the light of lines jmid-3 .. jmid+3 (jmid the line row y
// is in): for each gun, the Beam table's value at the gun's 8-bit value and
// the row's offset from the line's top -- the value's drive times its
// beam's share of that row, worked out on the CPU by crt.c
// (crt_tables_build) from the model's own functions. src/core/crt.c's
// down_texel is this, in C.
//
// (build 141a) The frame is the game's colour plane, each gun's 8-bit value
// (the DAC ladder's eight are among them), and the Beam table has a row for
// each of the 256: until 141a the frame was the index plane, a palette
// texture gave each index's three DAC levels, and the table had eight rows
// (crt_down.frag.hlsl). The ladder's eight rows are the same numbers.
//
// Resources, as SDL_GPU wants them for a fragment shader: sampled textures
// and their samplers in space2, the uniform buffer in space3.

Texture2D<float4> Frame   : register(t0, space2); // 320x224 RGBA8: colour / 255
SamplerState      FrameS  : register(s0, space2);
Texture2D<float4> Beam    : register(t1, space2); // 7k x 256 R32F, by value
SamplerState      BeamS   : register(s1, space2);

cbuffer Down : register(b0, space3)
{
    int pic_y;      // the picture's top, in display rows
    int k;          // display rows a line
    int reach;      // lines either side (3)
    int lines;      // 224
};

int floor_div(int a, int b)
{
    return (a >= 0) ? a / b : -((-a + b - 1) / b);
}

float4 main(float4 position : SV_Position) : SV_Target0
{
    int i    = (int)position.x;
    int y    = (int)position.y;
    int r0   = y - pic_y;
    int jmid = floor_div(r0, k);

    float3 acc = float3(0.0, 0.0, 0.0);
    for (int dj = -reach; dj <= reach; dj++) {
        int j = jmid + dj;
        if (j >= 0 && j < lines) {
            int  col = r0 - j * k + reach * k;
            int3 v   = (int3)(Frame.Load(int3(i, j, 0)).rgb * 255.0 + 0.5);
            acc.r += Beam.Load(int3(col, v.r, 0)).r;
            acc.g += Beam.Load(int3(col, v.g, 0)).r;
            acc.b += Beam.Load(int3(col, v.b, 0)).r;
        }
    }
    return float4(acc, 1.0);
}
