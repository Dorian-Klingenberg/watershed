// fluid_compute_phase2_tiled.hlsl -- tiled GPU-resident cellular-water experiment.
//
// ProposeTiledCS computes the same source proposals as Phase 1, but each
// 16x16 thread group cooperatively loads an 18x18 surface tile including a
// one-cell neighbor halo into group-shared memory before evaluating flow.

cbuffer FluidConstants : register(b0)
{
    uint field_width;
    uint field_depth;
    float max_flow_inches;
    float settle_rate;

    float minimum_water_inches;
    uint drain_edges;
    uint pad0;
    uint pad1;
};

StructuredBuffer<int> terrain_heights : register(t0);
StructuredBuffer<float> water_in : register(t1);

struct FlowProposal
{
    float total_outflow;
    float send_left;
    float send_right;
    float send_up;
    float send_down;
};

RWStructuredBuffer<FlowProposal> proposals : register(u0);
RWStructuredBuffer<float> water_out : register(u1);

FlowProposal ZeroProposal()
{
    FlowProposal proposal;
    proposal.total_outflow = 0.0f;
    proposal.send_left = 0.0f;
    proposal.send_right = 0.0f;
    proposal.send_up = 0.0f;
    proposal.send_down = 0.0f;
    return proposal;
}

static const uint tile_width = 16;
static const uint tile_depth = 16;
static const uint halo_width = 18;
static const uint halo_depth = 18;
groupshared float tile_surfaces[halo_width * halo_depth];

float SurfaceAt(uint x, uint z)
{
    const uint index = z * field_width + x;
    return (float)terrain_heights[index] + water_in[index];
}

[numthreads(16, 16, 1)]
void ProposeTiledCS(
    uint3 group_id : SV_GroupID,
    uint3 local_id : SV_GroupThreadID,
    uint3 dispatch_id : SV_DispatchThreadID)
{
    const uint local_linear = local_id.y * tile_width + local_id.x;
    const int2 group_origin = int2(
        (int)(group_id.x * tile_width),
        (int)(group_id.y * tile_depth));

    for (uint load_index = local_linear;
         load_index < halo_width * halo_depth;
         load_index += tile_width * tile_depth)
    {
        const int tile_x = (int)(load_index % halo_width);
        const int tile_z = (int)(load_index / halo_width);
        const int2 grid_cell = clamp(
            group_origin + int2(tile_x - 1, tile_z - 1),
            int2(0, 0),
            int2((int)field_width - 1, (int)field_depth - 1));
        tile_surfaces[load_index] = SurfaceAt((uint)grid_cell.x, (uint)grid_cell.y);
    }

    GroupMemoryBarrierWithGroupSync();

    if (dispatch_id.x >= field_width || dispatch_id.y >= field_depth)
        return;

    const uint current_i = dispatch_id.y * field_width + dispatch_id.x;
    FlowProposal proposal = ZeroProposal();
    const float available_water = water_in[current_i];
    if (available_water <= minimum_water_inches)
    {
        proposals[current_i] = proposal;
        return;
    }

    const uint shared_center =
        (local_id.y + 1) * halo_width + (local_id.x + 1);
    const float current_surface = tile_surfaces[shared_center];
    uint neighbor_directions[4];
    float neighbor_surfaces[4];
    uint lower_count = 0;
    float surface_sum = current_surface;

    if (dispatch_id.x > 0)
    {
        const float neighbor_surface = tile_surfaces[shared_center - 1];
        if (neighbor_surface < current_surface - minimum_water_inches)
        {
            neighbor_directions[lower_count] = 0;
            neighbor_surfaces[lower_count] = neighbor_surface;
            surface_sum += neighbor_surface;
            ++lower_count;
        }
    }
    if (dispatch_id.x + 1 < field_width)
    {
        const float neighbor_surface = tile_surfaces[shared_center + 1];
        if (neighbor_surface < current_surface - minimum_water_inches)
        {
            neighbor_directions[lower_count] = 1;
            neighbor_surfaces[lower_count] = neighbor_surface;
            surface_sum += neighbor_surface;
            ++lower_count;
        }
    }
    if (dispatch_id.y > 0)
    {
        const float neighbor_surface = tile_surfaces[shared_center - halo_width];
        if (neighbor_surface < current_surface - minimum_water_inches)
        {
            neighbor_directions[lower_count] = 2;
            neighbor_surfaces[lower_count] = neighbor_surface;
            surface_sum += neighbor_surface;
            ++lower_count;
        }
    }
    if (dispatch_id.y + 1 < field_depth)
    {
        const float neighbor_surface = tile_surfaces[shared_center + halo_width];
        if (neighbor_surface < current_surface - minimum_water_inches)
        {
            neighbor_directions[lower_count] = 3;
            neighbor_surfaces[lower_count] = neighbor_surface;
            surface_sum += neighbor_surface;
            ++lower_count;
        }
    }

    if (lower_count == 0)
    {
        proposals[current_i] = proposal;
        return;
    }

    const float shared_surface = surface_sum / (float)(lower_count + 1);
    float desired_inflows[4];
    float desired_outflow = 0.0f;
    for (uint lower_index = 0; lower_index < lower_count; ++lower_index)
    {
        desired_inflows[lower_index] =
            max(0.0f, shared_surface - neighbor_surfaces[lower_index]);
        desired_outflow += desired_inflows[lower_index];
    }

    if (desired_outflow <= minimum_water_inches)
    {
        proposals[current_i] = proposal;
        return;
    }

    const float total_flow = min(
        min(available_water, max_flow_inches),
        desired_outflow * settle_rate);
    if (total_flow <= minimum_water_inches)
    {
        proposals[current_i] = proposal;
        return;
    }

    proposal.total_outflow = total_flow;
    const float flow_scale = total_flow / desired_outflow;
    for (uint transfer_index = 0; transfer_index < lower_count; ++transfer_index)
    {
        const float inflow = desired_inflows[transfer_index] * flow_scale;
        if (neighbor_directions[transfer_index] == 0)
            proposal.send_left = inflow;
        else if (neighbor_directions[transfer_index] == 1)
            proposal.send_right = inflow;
        else if (neighbor_directions[transfer_index] == 2)
            proposal.send_up = inflow;
        else
            proposal.send_down = inflow;
    }

    proposals[current_i] = proposal;
}

// Apply is intentionally unchanged from Phase 1. This isolates tiling to the
// neighbor-heavy proposal pass and preserves its ordered gather behavior.
[numthreads(256, 1, 1)]
void ApplyCS(uint3 dispatch_id : SV_DispatchThreadID)
{
    const uint cell_count = field_width * field_depth;
    const uint current_i = dispatch_id.x;
    if (current_i >= cell_count)
        return;

    const uint x = current_i % field_width;
    const uint z = current_i / field_width;
    float delta = 0.0f;

    if (z > 0)
        delta += proposals[current_i - field_width].send_down;
    if (x > 0)
        delta += proposals[current_i - 1].send_right;

    delta -= proposals[current_i].total_outflow;

    if (x + 1 < field_width)
        delta += proposals[current_i + 1].send_left;
    if (z + 1 < field_depth)
        delta += proposals[current_i + field_width].send_up;

    float new_water = max(0.0f, water_in[current_i] + delta);
    if (drain_edges != 0 &&
        (x == 0 || x + 1 == field_width || z == 0 || z + 1 == field_depth))
    {
        new_water = 0.0f;
    }

    water_out[current_i] = new_water;
}
