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
    uint coarse_cell_size_cells;
    float water_alpha;
    uint display_mode;
    float debug_gain;
};

struct CoarseCell
{
    float full_height_feet;
    float pad0;
};

struct FineCell
{
    uint fine_x;
    uint fine_z;
    float base_height_feet;
    float top_height_feet;
    float water_depth_feet;
    uint water_side_mask;
    float velocity_feet_per_second;
    float sediment_depth_feet;
    float terrain_delta_feet;
};

StructuredBuffer<CoarseCell> coarse_cells : register(t0);
StructuredBuffer<FineCell> fine_cells : register(t1);

struct VSOutput
{
    float4 position : SV_POSITION;
    float3 normal : NORMAL0;
    float water_depth : TEXCOORD0;
    float pass_kind : TEXCOORD1;
    nointerpolation uint face_kind : TEXCOORD2;
    nointerpolation uint water_side_mask : TEXCOORD3;
    float velocity : TEXCOORD4;
    float sediment_depth : TEXCOORD5;
    float terrain_delta : TEXCOORD6;
    float2 world_xz : TEXCOORD7;
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
    uint water_side_mask = 0xFu;
    float velocity = 0.0;
    float sediment_depth = 0.0;
    float terrain_delta = 0.0;

    if (pass_kind == 0u)
    {
        const uint coarse_x = column_index % coarse_width;
        const uint coarse_z = column_index / coarse_width;
        const float coarse_size = voxel_size_feet * float(max(1u, coarse_cell_size_cells));
        const CoarseCell cell = coarse_cells[column_index];

        world_min = float3(
            field_origin_x + float(coarse_x) * coarse_size,
            0.0,
            field_origin_z + float(coarse_z) * coarse_size);
        world_max = world_min + float3(coarse_size, cell.full_height_feet, coarse_size);
    }
    else
    {
        const FineCell cell = fine_cells[column_index];

        world_min = float3(
            field_origin_x + float(cell.fine_x) * voxel_size_feet,
            cell.base_height_feet,
            field_origin_z + float(cell.fine_z) * voxel_size_feet);
        world_max = float3(
            world_min.x + voxel_size_feet,
            cell.top_height_feet,
            world_min.z + voxel_size_feet);
        water_depth = cell.water_depth_feet;
        water_side_mask = cell.water_side_mask;
        velocity = cell.velocity_feet_per_second;
        sediment_depth = cell.sediment_depth_feet;
        terrain_delta = cell.terrain_delta_feet;
    }

    const float3 world = lerp(world_min, world_max, corner);

    VSOutput output;
    output.position = mul(float4(world, 1.0), view_projection);
    output.normal = CubeFaceNormal(vertex_in_column / 6u);
    output.water_depth = water_depth;
    output.pass_kind = float(pass_kind);
    output.face_kind = vertex_in_column / 6u;
    output.water_side_mask = water_side_mask;
    output.velocity = velocity;
    output.sediment_depth = sediment_depth;
    output.terrain_delta = terrain_delta;
    output.world_xz = float2(world.x, world.z);
    return output;
}

float4 PSMain(VSOutput input) : SV_TARGET
{
    const float3 light_dir = normalize(float3(-0.35, 0.85, -0.40));
    const float diffuse = saturate(dot(normalize(input.normal), light_dir)) * 0.65 + 0.35;

    const int cell_x = int(floor((input.world_xz.x - field_origin_x) / voxel_size_feet));
    const int cell_z = int(floor((input.world_xz.y - field_origin_z) / voxel_size_feet));
    const bool is_highlight = (cell_x == highlight_x && cell_z == highlight_z);

    if (display_mode == 3u && abs(input.terrain_delta) > 0.00001)
    {
        const float erosion = saturate(-input.terrain_delta * debug_gain);
        const float deposition = saturate(input.terrain_delta * debug_gain);
        float3 change_color = float3(0.16, 0.22, 0.18);
        change_color = lerp(change_color, float3(0.16, 0.95, 0.38), erosion);
        change_color = lerp(change_color, float3(1.00, 0.28, 0.12), deposition);
        return float4(change_color * diffuse, 1.0);
    }

    if (input.water_depth > 0.0005)
    {
        if (input.face_kind >= 2u)
        {
            const uint side_bit = 1u << (input.face_kind - 2u);
            if ((input.water_side_mask & side_bit) == 0u)
                discard;
        }

        float3 water_color = 0.0;
        if (display_mode == 1u)
        {
            const float speed_t = saturate(input.velocity * debug_gain);
            water_color = lerp(float3(0.02, 0.09, 0.22),
                               float3(0.82, 0.98, 1.00),
                               speed_t);
        }
        else if (display_mode == 2u)
        {
            const float sediment_t = saturate(input.sediment_depth * debug_gain);
            water_color = lerp(float3(0.04, 0.20, 0.36),
                               float3(0.96, 0.68, 0.24),
                               sediment_t);
        }
        else
        {
            const float water_t = saturate(input.water_depth * debug_gain);
            const float3 shallow = float3(0.58, 0.86, 1.00);
            const float3 deep = float3(0.02, 0.14, 0.55);
            water_color = lerp(shallow, deep, water_t);
        }

        if (is_highlight)
            water_color = lerp(water_color, float3(1.0, 0.95, 0.25), 0.45);
        return float4(water_color * diffuse, saturate(water_alpha));
    }

    const float3 coarse_color = float3(0.36, 0.50, 0.25);
    const float3 fine_color = float3(0.49, 0.70, 0.31);
    float3 base = lerp(coarse_color, fine_color, saturate(input.pass_kind));
    if (is_highlight)
        base = lerp(base, float3(1.0, 0.95, 0.25), 0.45);

    return float4(base * diffuse, 1.0);
}
