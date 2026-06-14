// split_lod_renderer.hlsl -- coarse full columns + fine remainder columns.
//
// This is a mesh-style renderer: no vertex buffers, just StructuredBuffers and
// SV_VertexID. The CPU compacts fine remainder cells so the second pass only
// draws cells that actually rise above the coarse fully-filled base.

cbuffer SceneConstants : register(b0)
{
    float4x4 inverse_view_projection;
    float4 camera_world_pos;

    float field_origin_x;
    float field_origin_z;
    float voxel_size_feet;
    float max_height_feet;

    uint field_width;
    uint field_depth;
    int highlight_x;
    int highlight_z;

    float4x4 view_projection;
};

cbuffer SplitPassConstants : register(b1)
{
    uint pass_kind;     // 0 = coarse, 1 = fine
    uint coarse_width;
    uint coarse_depth;
    uint pass_pad0;
};

struct CoarseCell
{
    float full_height_feet;
    uint material_id;
};

struct FineCell
{
    uint fine_x;
    uint fine_z;
    float base_height_feet;
    float top_height_feet;
    float water_depth_feet;
    uint material_id;
};

StructuredBuffer<CoarseCell> coarse_cells : register(t0);
StructuredBuffer<FineCell> fine_cells : register(t1);

struct VSOutput
{
    float4 position : SV_POSITION;
    float3 normal : NORMAL0;
    float water_depth : TEXCOORD0;
    float pass_kind : TEXCOORD1;
    nointerpolation uint material_id : TEXCOORD2;
    float2 world_xz : TEXCOORD3;
};

static const uint CUBE_INDICES[36] = {
    0, 5, 1, 0, 4, 5, // bottom
    3, 2, 6, 3, 6, 7, // top
    0, 1, 2, 0, 2, 3, // -Z
    4, 7, 6, 4, 6, 5, // +Z
    0, 3, 7, 0, 7, 4, // -X
    1, 5, 6, 1, 6, 2  // +X
};

float3 CubeCorner(uint corner)
{
    switch (corner)
    {
    case 0: return float3(0.0, 0.0, 0.0);
    case 1: return float3(1.0, 0.0, 0.0);
    case 2: return float3(1.0, 1.0, 0.0);
    case 3: return float3(0.0, 1.0, 0.0);
    case 4: return float3(0.0, 0.0, 1.0);
    case 5: return float3(1.0, 0.0, 1.0);
    case 6: return float3(1.0, 1.0, 1.0);
    default: return float3(0.0, 1.0, 1.0);
    }
}

float3 CubeFaceNormal(uint face)
{
    switch (face)
    {
    case 0: return float3(0.0, -1.0, 0.0);
    case 1: return float3(0.0,  1.0, 0.0);
    case 2: return float3(0.0, 0.0, -1.0);
    case 3: return float3(0.0, 0.0,  1.0);
    case 4: return float3(-1.0, 0.0, 0.0);
    default: return float3(1.0, 0.0, 0.0);
    }
}

VSOutput VSMain(uint vertex_id : SV_VertexID)
{
    const uint vertex_in_column = vertex_id % 36u;
    const uint column_index = vertex_id / 36u;
    const uint corner_index = CUBE_INDICES[vertex_in_column];
    const float3 corner = CubeCorner(corner_index);

    float3 world_min = 0.0;
    float3 world_max = 0.0;
    float water_depth = 0.0;
    uint material_id = 0;

    if (pass_kind == 0u)
    {
        const uint coarse_x = column_index % coarse_width;
        const uint coarse_z = column_index / coarse_width;
        const float coarse_size = voxel_size_feet * 12.0;
        const CoarseCell cell = coarse_cells[column_index];
        material_id = cell.material_id;

        world_min = float3(
            field_origin_x + float(coarse_x) * coarse_size,
            0.0,
            field_origin_z + float(coarse_z) * coarse_size);
        world_max = world_min + float3(coarse_size, cell.full_height_feet, coarse_size);
    }
    else
    {
        const FineCell cell = fine_cells[column_index];
        material_id = cell.material_id;

        world_min = float3(
            field_origin_x + float(cell.fine_x) * voxel_size_feet,
            cell.base_height_feet,
            field_origin_z + float(cell.fine_z) * voxel_size_feet);
        world_max = float3(
            world_min.x + voxel_size_feet,
            cell.top_height_feet,
            world_min.z + voxel_size_feet);
        water_depth = cell.water_depth_feet;
    }

    const float3 world = lerp(world_min, world_max, corner);

    VSOutput output;
    output.position = mul(float4(world, 1.0), view_projection);
    output.normal = CubeFaceNormal(vertex_in_column / 6u);
    output.water_depth = water_depth;
    output.pass_kind = float(pass_kind);
    output.material_id = material_id;
    output.world_xz = world.xz;
    return output;
}

float3 MaterialColor(uint material_id)
{
    switch (material_id)
    {
    case 1: return float3(0.33, 0.50, 0.22);
    case 2: return float3(0.55, 0.42, 0.26);
    case 3: return float3(0.58, 0.56, 0.50);
    case 4: return float3(0.27, 0.22, 0.16);
    case 5: return float3(0.47, 0.55, 0.52);
    case 6: return float3(0.05, 0.34, 0.82);
    case 7: return float3(0.45, 0.33, 0.18);
    case 8: return float3(0.55, 0.55, 0.47);
    case 9: return float3(0.48, 0.43, 0.36);
    case 10: return float3(0.42, 0.25, 0.13);
    case 11: return float3(0.38, 0.37, 0.32);
    case 12: return float3(0.54, 0.28, 0.78);
    case 13: return float3(0.42, 0.62, 0.37);
    case 14: return float3(0.34, 0.32, 0.28);
    case 15: return float3(0.45, 0.57, 0.60);
    case 16: return float3(0.36, 0.49, 0.50);
    default: return float3(0.24, 0.55, 0.18);
    }
}

float4 PSMain(VSOutput input) : SV_TARGET
{
    const float3 light_dir = normalize(float3(-0.35, 0.85, -0.40));
    const float diffuse = saturate(dot(normalize(input.normal), light_dir)) * 0.65 + 0.35;
    const int cell_x = int(floor((input.world_xz.x - field_origin_x) / voxel_size_feet));
    const int cell_z = int(floor((input.world_xz.y - field_origin_z) / voxel_size_feet));
    const bool is_highlight = (cell_x == highlight_x && cell_z == highlight_z);

    if (input.water_depth > 0.01 || input.material_id == 6)
    {
        const float water_t = saturate(input.water_depth * 4.0);
        const float3 shallow = float3(0.18, 0.55, 0.92);
        const float3 deep = float3(0.03, 0.22, 0.62);
        float3 water = lerp(shallow, deep, water_t);
        if (is_highlight)
            water = lerp(water, float3(1.0, 0.95, 0.25), 0.45);
        return float4(water * diffuse, 1.0);
    }

    float3 base = MaterialColor(input.material_id);
    if (input.pass_kind > 0.5)
        base = lerp(base, float3(0.75, 0.82, 0.68), 0.10);
    if (is_highlight)
        base = lerp(base, float3(1.0, 0.95, 0.25), 0.45);
    return float4(base * diffuse, 1.0);
}
