// (build 107) One triangle over the whole target, no vertex buffer: the
// three passes' fragment shaders work from SV_Position alone (the target's
// pixel centres, from its top left, on every SDL_GPU backend).
//
// Compiled offline by `make shaders` (SDL_shadercross) to SPIR-V, DXIL and
// MSL in shaders/; docs/crt.md, "Phase 2".

struct VertexOut
{
    float4 position : SV_Position;
};

VertexOut main(uint id : SV_VertexID)
{
    VertexOut o;
    float x = (id == 1) ? 3.0 : -1.0;
    float y = (id == 2) ? 3.0 : -1.0;
    o.position = float4(x, y, 0.0, 1.0);
    return o;
}
