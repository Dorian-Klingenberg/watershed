// wireframe_renderer.hlsl — Top-Face Wireframe Renderer
//
// Renders the terrain as a flat wireframe grid of column top faces.
// Each of the W×D columns contributes one quad (two triangles, 6 vertices).
// No vertex buffer is needed — vertex positions are computed in the VS entirely
// from SV_VertexID and the field_cells StructuredBuffer.
//
// Geometry generation:
//   Each quad has 4 corners at (cx, h, cz), (cx+1, h, cz), (cx+1, h, cz+1),
//   (cx, h, cz+1) where h = field_cells[cz * W + cx].surface_height_feet.
//
//   We split each quad into 2 triangles, 6 vertices (no index buffer):
//
//       (0,1)──(1,1)         tri 0: v0=(0,0), v1=(1,0), v2=(1,1)
//         │  ╲   │    →      tri 1: v3=(0,0), v4=(1,1), v5=(0,1)
//       (0,0)──(1,0)
//
//   With D3D12_FILL_MODE_WIREFRAME on the PSO, only the triangle edges are
//   rasterised — producing the characteristic grid-line appearance.
//
// Coordinate system (same as grass_field_renderer.hlsl):
//   X and Z are horizontal; Y is vertical (height in feet).
//   Column (cx, cz) occupies the square [cx*voxel, (cx+1)*voxel] × [cz*voxel, (cz+1)*voxel].
//
// Root parameter layout (must match WireframeRenderer::build_root_signature):
//   param 0 → inline CBV at b0 (SceneConstants, used: field_origin, voxel_size, field dims, VP)
//   param 1 → descriptor table: 1 SRV at t0, visible to VERTEX shader

// ─────────────────────────────────────────────────────────────────────────────
// GPU inputs
// ─────────────────────────────────────────────────────────────────────────────

// SceneConstants layout must match the CPU-side SceneConstants struct in main.cpp,
// byte-for-byte. Fields unused by this shader are declared but not referenced.
cbuffer SceneConstants : register(b0)
{
    float4x4 inverse_view_projection;   // unused (needed for layout alignment only)

    float4   camera_world_pos;          // unused

    float    field_origin_x;            // world X of column [0, 0]
    float    field_origin_z;            // world Z of column [0, 0]
    float    voxel_size_feet;           // column width in world-space feet
    float    max_height_feet;           // unused

    uint     field_width;               // column count along X
    uint     field_depth;               // column count along Z
    int      highlight_x;
    int      highlight_z;

    // VP matrix added to SceneConstants for mesh-based renderers (Step 9+).
    // inverse_view_projection is used by the raycast PS; view_projection is
    // used here in the wireframe VS to project world-space vertices to clip space.
    float4x4 view_projection;           // world-space → clip-space transform
};

struct ColumnData
{
    float surface_height_feet;
    float water_depth_feet;
};

// One payload per column, row-major order: index = z * field_width + x.
// Heights are in feet (the CPU converts from inches before uploading).
StructuredBuffer<ColumnData> field_cells : register(t0);

struct VSOutput
{
    float4 position    : SV_POSITION;
    float  water_depth : TEXCOORD0;
    nointerpolation int cell_x : TEXCOORD1;
    nointerpolation int cell_z : TEXCOORD2;
};

// ─────────────────────────────────────────────────────────────────────────────
// Quad corner offsets
// ─────────────────────────────────────────────────────────────────────────────
// For each of the 6 vertices in a quad (two counter-clockwise triangles when
// viewed from above), this table gives the (X, Z) offset within the 1×1 cell.
//
// We use float2 here because uint2 offsets would require a cast anyway when
// computing the float world position.
static const float2 QUAD_OFFSETS[6] =
{
    // tri 0
    { 0.0f, 0.0f },   // v0: bottom-left
    { 1.0f, 0.0f },   // v1: bottom-right
    { 1.0f, 1.0f },   // v2: top-right
    // tri 1
    { 0.0f, 0.0f },   // v3: bottom-left (shared with tri 0 v0)
    { 1.0f, 1.0f },   // v4: top-right   (shared with tri 0 v2)
    { 0.0f, 1.0f },   // v5: top-left
};

// ─────────────────────────────────────────────────────────────────────────────
// Vertex shader: generate top-face quad geometry from SV_VertexID
// ─────────────────────────────────────────────────────────────────────────────
// The CPU calls DrawInstanced(W * D * 6, 1, 0, 0). SV_VertexID runs 0 .. W*D*6-1.
//
// For vertex i:
//   col_idx = i / 6          → which column (0 .. W*D-1)
//   vert    = i % 6          → which corner of that column's quad (0..5)
//   cx      = col_idx % W    → column X index
//   cz      = col_idx / W    → column Z index
//   h       = heights[cz*W + cx]  → height of this column in feet
//   world   = (cx + dx, h, cz + dz) * voxel_size + field_origin
//   clip    = mul(float4(world, 1), view_projection)

VSOutput VSMain(uint vertex_id : SV_VertexID)
{
    // Decompose vertex_id into column index and quad-corner index.
    const uint col_idx = vertex_id / 6u;
    const uint vert    = vertex_id % 6u;

    // Map column index to a 2D grid position.
    const uint cx = col_idx % field_width;
    const uint cz = col_idx / field_width;

    // Height of this column (all four corners of the top face share this height).
    const ColumnData cell = field_cells[cz * field_width + cx];
    const float h = cell.surface_height_feet;

    // Offset within the 1×1 cell for this particular corner.
    const float2 off = QUAD_OFFSETS[vert];

    // World-space position: scale by voxel size and apply field origin offset.
    const float wx = (float(cx) + off.x) * voxel_size_feet + field_origin_x;
    const float wz = (float(cz) + off.y) * voxel_size_feet + field_origin_z;

    VSOutput output;

    // Transform to clip space using the forward VP matrix from SceneConstants.
    // view_projection is already transposed on the CPU side (row-major → HLSL),
    // so mul(row_vec, matrix) here produces the correct result.
    output.position = mul(float4(wx, h, wz, 1.0f), view_projection);
    output.water_depth = cell.water_depth_feet;
    output.cell_x = int(cx);
    output.cell_z = int(cz);
    return output;
}

// ─────────────────────────────────────────────────────────────────────────────
// Pixel shader: flat wire colour
// ─────────────────────────────────────────────────────────────────────────────
// With FILL_MODE_WIREFRAME only edge pixels reach the PS. We output a pale
// chartreuse colour that is clearly visible against the sky-blue clear colour.
float4 PSMain(VSOutput input) : SV_TARGET
{
    if (input.cell_x == highlight_x && input.cell_z == highlight_z)
        return float4(1.0f, 0.95f, 0.25f, 1.0f);

    if (input.water_depth > 0.01f)
        return float4(0.18f, 0.62f, 1.0f, 1.0f);

    // Pale chartreuse: distinct from the sky-blue (0.53, 0.81, 0.98) clear colour.
    return float4(0.75f, 0.95f, 0.55f, 1.0f);
}
