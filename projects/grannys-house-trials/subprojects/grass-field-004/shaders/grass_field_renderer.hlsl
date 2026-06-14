// grass_field_renderer.hlsl — Step 5: Column Raycast Renderer
//
// This shader renders a terrain of square columns without a mesh. A single
// full-screen triangle covers the viewport; the pixel shader fires a ray
// through each pixel and steps through the column grid until it either hits
// a column or escapes out the far side.
//
// Technique: DDA (Digital Differential Analysis) traversal
//
//   For a ray travelling at some angle through a 2D grid, DDA precomputes
//   exactly how much further the ray must travel (in t) to cross the next
//   column boundary in X, and the next column boundary in Z. At each step it
//   takes the shorter hop — always landing precisely on the next boundary,
//   never wasting steps crossing empty air.
//
//   This is the same algorithm Wolfenstein 3D used for wall raycasting,
//   adapted here to a top-down grid of variable-height columns.
//
// Coordinate system:
//   - X / Z are horizontal (column indices × voxel_size_feet)
//   - Y is vertical (column height in feet, zero at ground level)
//   - The field occupies [ field_origin_x .. field_origin_x + width * voxel_size ]
//                      × [ 0 .. max_height_feet ]
//                      × [ field_origin_z .. field_origin_z + depth * voxel_size ]

// ─────────────────────────────────────────────────────────────────────────────
// GPU inputs
// ─────────────────────────────────────────────────────────────────────────────

cbuffer SceneConstants : register(b0)
{
    // Inverse of (view * projection). Lets us reconstruct a world-space ray
    // from a 2D screen pixel. The CPU computes this each frame and uploads it.
    float4x4 inverse_view_projection;

    // World-space position of the camera. Every pixel's ray starts here.
    float4 camera_world_pos;

    // World-space X and Z of column [0, 0]. Normally (0, 0) but kept as a
    // parameter so the field can be repositioned without recompiling.
    float field_origin_x;
    float field_origin_z;

    // Size of one column in world-space feet.
    float voxel_size_feet;

    // Height ceiling for the AABB pre-rejection test (in feet).
    // Set to the tallest possible column height with some headroom.
    float max_height_feet;

    // Number of columns in X and Z.
    uint field_width;
    uint field_depth;
    int highlight_x;
    int highlight_z;
};

struct ColumnData
{
    float surface_height_feet;
    float water_depth_feet;
    uint material_id;
    uint pad0;
};

// One payload per column, in row-major order: index = z * field_width + x.
// Heights are in feet (the CPU converts from inches before uploading).
StructuredBuffer<ColumnData> field_cells : register(t0);

// ─────────────────────────────────────────────────────────────────────────────
// Shared types
// ─────────────────────────────────────────────────────────────────────────────

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD0;
};

struct CameraRay
{
    float3 origin;
    float3 direction;
};

struct TraceResult
{
    bool   hit;
    float3 position;  // world-space hit point
    float3 normal;    // surface normal at the hit point
    float  water_depth;
    int    cell_x;    // column index along X
    int    cell_z;    // column index along Z
};

// ─────────────────────────────────────────────────────────────────────────────
// Vertex shader: the full-screen triangle trick
// ─────────────────────────────────────────────────────────────────────────────
//
// We draw exactly 3 vertices — no vertex buffer. SV_VertexID is automatically
// provided by the GPU as 0, 1, or 2 when DrawInstanced(3, ...) is called.
//
// Clip-space corners:
//   vertex 0 → (-1, -1)   vertex 1 → (-1,  3)   vertex 2 → ( 3, -1)
//
// These three points form a triangle that contains the entire NDC square
// [(-1,-1), (1,1)] and extends beyond it. The rasterizer clips the overhang
// to the viewport; every pixel inside is visited exactly once.
//
// Why one triangle instead of two? One triangle avoids the diagonal seam that
// can appear on the diagonal of a full-screen quad under some GPU rounding
// modes, and saves one vertex.

VSOutput VSMain(uint vertex_id : SV_VertexID)
{
    // Derive clip-space XY purely from the vertex index — no buffer read.
    const float2 clip = float2(
        vertex_id == 2 ? 3.0 : -1.0,
        vertex_id == 1 ? 3.0 : -1.0);

    VSOutput output;
    output.position = float4(clip, 0.0, 1.0);

    // Convert clip-space [-1,1] → UV [0,1].
    // Y is flipped: clip +Y is screen top, UV y=0 is also top — they agree,
    // but the formula (1 - clip.y) * 0.5 makes the relationship explicit.
    output.uv = float2(
        (clip.x + 1.0) * 0.5,
        (1.0 - clip.y) * 0.5);

    return output;
}

// ─────────────────────────────────────────────────────────────────────────────
// Ray reconstruction
// ─────────────────────────────────────────────────────────────────────────────
//
// The vertex shader runs in forward mode: world-space vertices → clip space.
// Here we run it backward: a clip-space pixel → a world-space ray direction.
//
// The trick: construct a point on the far clip plane (z=1, w=1), multiply it
// by the inverse view-projection to get its world-space position, then
// subtract the camera origin to get the direction vector for this pixel.

CameraRay BuildCameraRay(float2 uv)
{
    // UV [0,1] → NDC [-1,1]. Y is negated because clip +Y = top, UV y=0 = top.
    const float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);

    // A point on the far clip plane in homogeneous clip space.
    float4 far_clip = float4(ndc, 1.0, 1.0);

    // mul(row_vector, matrix) in HLSL: the CPU must upload the matrix
    // transposed relative to DirectXMath's row-major convention so that
    // this row-vector × matrix multiply gives the correct result.
    float4 far_world = mul(far_clip, inverse_view_projection);

    // Perspective divide: homogeneous w ≠ 1 after the inverse projection;
    // dividing by w recovers true Cartesian world coordinates.
    far_world.xyz /= far_world.w;

    CameraRay ray;
    ray.origin    = camera_world_pos.xyz;
    ray.direction = normalize(far_world.xyz - ray.origin);
    return ray;
}

// ─────────────────────────────────────────────────────────────────────────────
// AABB intersection — the slab method
// ─────────────────────────────────────────────────────────────────────────────
//
// An axis-aligned bounding box is the intersection of three pairs of parallel
// planes (one pair per axis). For each pair we solve:
//   t = (plane_position - ray.origin) / ray.direction
// to find where the ray crosses each slab.
//
// The entry point is the largest of the three near crossings;
// the exit point is the smallest of the three far crossings.
// If exit >= entry the ray hits the box.

bool IntersectAabb(
    float3 origin,
    float3 direction,
    float3 aabb_min,
    float3 aabb_max,
    out float t_enter,
    out float t_exit)
{
    const float3 t0 = (aabb_min - origin) / direction;
    const float3 t1 = (aabb_max - origin) / direction;

    // Per axis: the smaller t is where the ray enters that slab,
    // the larger t is where it exits.
    const float3 t_near = min(t0, t1);
    const float3 t_far  = max(t0, t1);

    t_enter = max(max(t_near.x, t_near.y), t_near.z);
    t_exit  = min(min(t_far.x,  t_far.y),  t_far.z);

    // t_enter < 0 means the camera is already inside the box — still valid.
    return t_exit >= max(t_enter, 0.0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Surface normal estimation
// ─────────────────────────────────────────────────────────────────────────────
//
// When the ray hits the top face of a column (Y ≈ column_top), we estimate
// the surface normal from the height difference between neighbouring columns.
// This gives a smooth-looking slope instead of a flat top.
//
// When the ray hits a side face of a column, we return the axis-aligned
// outward normal of whichever face the hit point is closest to.

float3 EstimateSurfaceNormal(int cell_x, int cell_z, float3 hit_pos, float col_top)
{
    // If the hit is within 8% of a voxel of the column top, treat it as a
    // top-face hit and estimate the slope from neighbours.
    const float top_epsilon = voxel_size_feet * 0.08;

    if (abs(hit_pos.y - col_top) <= top_epsilon)
    {
        // Clamp neighbour indices to the grid boundary.
        const int left_x  = max(cell_x - 1, 0);
        const int right_x = min(cell_x + 1, (int)field_width  - 1);
        const int back_z  = max(cell_z - 1, 0);
        const int front_z = min(cell_z + 1, (int)field_depth  - 1);

        const float left_h  = field_cells[left_x  + cell_z  * field_width].surface_height_feet;
        const float right_h = field_cells[right_x + cell_z  * field_width].surface_height_feet;
        const float back_h  = field_cells[cell_x  + back_z  * field_width].surface_height_feet;
        const float front_h = field_cells[cell_x  + front_z * field_width].surface_height_feet;

        // Central difference gradient: the height difference over two
        // column widths (2 * voxel_size_feet) in each horizontal direction.
        // The Y component is 2 * voxel_size_feet to match the denominator.
        return normalize(float3(
            left_h - right_h,
            voxel_size_feet * 2.0,
            back_h - front_h));
    }
    else
    {
        // Side face: find which of the four vertical faces the hit is closest to.
        const float col_min_x = field_origin_x + cell_x * voxel_size_feet;
        const float col_max_x = col_min_x + voxel_size_feet;
        const float col_min_z = field_origin_z + cell_z * voxel_size_feet;
        const float col_max_z = col_min_z + voxel_size_feet;

        float3 normal  = float3(-1.0, 0.0, 0.0);  // assume -X face first
        float  closest = abs(hit_pos.x - col_min_x);

        float d = abs(hit_pos.x - col_max_x);
        if (d < closest) { closest = d; normal = float3(1.0, 0.0, 0.0); }

        d = abs(hit_pos.z - col_min_z);
        if (d < closest) { closest = d; normal = float3(0.0, 0.0, -1.0); }

        d = abs(hit_pos.z - col_max_z);
        if (d < closest) {              normal = float3(0.0, 0.0,  1.0); }

        return normal;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Sky colour
// ─────────────────────────────────────────────────────────────────────────────

float3 SkyColor(float3 dir)
{
    // A simple horizon-to-zenith gradient.
    const float3 horizon = float3(0.61, 0.71, 0.79);
    const float3 zenith  = float3(0.53, 0.81, 0.98);
    const float  blend   = saturate(dir.y * 2.0 + 0.3);
    return lerp(horizon, zenith, blend);
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

// ─────────────────────────────────────────────────────────────────────────────
// Column traversal — DDA
// ─────────────────────────────────────────────────────────────────────────────
//
// The DDA loop visits each column the ray passes through in order:
//
//   tDeltaX = how much t increases per column step in X
//            = voxel_size / |ray.direction.x|
//   tDeltaZ = same for Z
//
//   tMaxX   = the t-value at which the ray crosses the NEXT X boundary
//   tMaxZ   = same for Z
//
//   At each iteration: advance to whichever boundary is closer (min tMax),
//   update tMax for that axis, and step the column index.
//
// The loop runs at most (field_width + field_depth) iterations — that is the
// maximum number of column boundaries a ray can cross in a grid of this size.
// In practice for a top-down view it is much less.

TraceResult TraceField(CameraRay ray)
{
    TraceResult miss = (TraceResult)0;
    miss.hit = false;

    // ── Early rejection: does the ray miss the field AABB? ─────────────────
    const float3 field_min = float3(field_origin_x, 0.0, field_origin_z);
    const float3 field_max = float3(
        field_origin_x + field_width * voxel_size_feet,
        max_height_feet,
        field_origin_z + field_depth * voxel_size_feet);

    float t_enter = 0.0;
    float t_exit  = 0.0;
    if (!IntersectAabb(ray.origin, ray.direction, field_min, field_max, t_enter, t_exit))
        return miss;

    // ── Initialise DDA at the ray's entry point ─────────────────────────────

    float t = max(t_enter, 0.0);

    // Step 0.0005 feet inside the first column to avoid landing exactly on a
    // boundary edge where floor() is ambiguous due to floating-point error.
    const float3 entry_pos = ray.origin + ray.direction * (t + 0.0005);

    // Which column cell does the entry point land in?
    int cell_x = clamp(
        (int)floor((entry_pos.x - field_origin_x) / voxel_size_feet),
        0, (int)field_width - 1);
    int cell_z = clamp(
        (int)floor((entry_pos.z - field_origin_z) / voxel_size_feet),
        0, (int)field_depth - 1);

    // Step direction: +1 if ray goes positive, -1 if negative, 0 if axis-parallel.
    const int step_x = ray.direction.x > 0.0 ? 1 : (ray.direction.x < 0.0 ? -1 : 0);
    const int step_z = ray.direction.z > 0.0 ? 1 : (ray.direction.z < 0.0 ? -1 : 0);

    const float large_t = 1.0e20;  // stand-in for "never crosses this axis"

    // tDelta: t increment per column step.
    // If the ray is parallel to an axis (step == 0), set delta to large_t
    // so tMax for that axis is never chosen as the minimum.
    const float t_delta_x = (step_x != 0) ? (voxel_size_feet / abs(ray.direction.x)) : large_t;
    const float t_delta_z = (step_z != 0) ? (voxel_size_feet / abs(ray.direction.z)) : large_t;

    // tMax: t-value of the next boundary crossing in each axis.
    // For X: find the X coordinate of the boundary the ray crosses first.
    float t_max_x = large_t;
    float t_max_z = large_t;

    if (step_x != 0)
    {
        // If stepping +X, the next boundary is the right edge of cell_x.
        // If stepping -X, the next boundary is the left edge of cell_x.
        const float next_x = field_origin_x
            + (step_x > 0 ? (cell_x + 1) : cell_x) * voxel_size_feet;
        t_max_x = (next_x - ray.origin.x) / ray.direction.x;
    }

    if (step_z != 0)
    {
        const float next_z = field_origin_z
            + (step_z > 0 ? (cell_z + 1) : cell_z) * voxel_size_feet;
        t_max_z = (next_z - ray.origin.z) / ray.direction.z;
    }

    // ── DDA loop ─────────────────────────────────────────────────────────────

    // Upper bound on iterations: width + depth column boundaries + margin.
    const int max_steps = (int)(field_width + field_depth) + 4;

    [loop]
    for (int step = 0; step < max_steps; ++step)
    {
        // Stop if the ray has left the grid.
        if (cell_x < 0 || cell_x >= (int)field_width ||
            cell_z < 0 || cell_z >= (int)field_depth)
            break;

        const ColumnData cell_data = field_cells[cell_x + cell_z * field_width];
        const float col_top = cell_data.surface_height_feet;

        // t at which the ray exits this XZ cell (crosses the next boundary).
        const float t_cell_exit = min(t_max_x, t_max_z);

        // Y height of the ray at cell entry and exit.
        const float y_entry = ray.origin.y + t           * ray.direction.y;
        const float y_exit  = ray.origin.y + t_cell_exit * ray.direction.y;

        const float seg_min_y = min(y_entry, y_exit);
        const float seg_max_y = max(y_entry, y_exit);

        // Does the ray's Y span within this cell overlap [0, col_top]?
        // If yes, the ray passes through the solid volume of this column.
        if (seg_max_y >= 0.0 && seg_min_y <= col_top)
        {
            // Find the precise hit t within [t, t_cell_exit].
            float hit_t = t;

            // If the ray travels upward and entered below ground, advance
            // hit_t to where it crosses y=0 (the ground plane).
            if (ray.direction.y > 0.0 && y_entry < 0.0)
                hit_t = max(hit_t, (0.0 - ray.origin.y) / ray.direction.y);

            // If the ray travels downward and entered above the column top,
            // advance hit_t to where it crosses the top face (y = col_top).
            if (ray.direction.y < 0.0 && y_entry > col_top)
                hit_t = max(hit_t, (col_top - ray.origin.y) / ray.direction.y);

            // Confirm the adjusted hit is still within this XZ cell.
            if (hit_t <= t_cell_exit + 0.0001)
            {
                TraceResult result;
                result.hit      = true;
                result.position = ray.origin + ray.direction * hit_t;
                result.normal   = EstimateSurfaceNormal(cell_x, cell_z, result.position, col_top);
                result.water_depth = cell_data.water_depth_feet;
                result.cell_x   = cell_x;
                result.cell_z   = cell_z;
                return result;
            }
        }

        // ── Advance DDA to the next boundary ─────────────────────────────
        // Choose the axis whose next boundary is closer.
        if (t_max_x < t_max_z)
        {
            t       = t_max_x;
            t_max_x += t_delta_x;
            cell_x  += step_x;
        }
        else
        {
            t       = t_max_z;
            t_max_z += t_delta_z;
            cell_z  += step_z;
        }

        // Stop if the ray has passed the far side of the AABB.
        if (t > t_exit)
            break;
    }

    return miss;
}

// ─────────────────────────────────────────────────────────────────────────────
// Pixel shader
// ─────────────────────────────────────────────────────────────────────────────

float4 PSMain(VSOutput input) : SV_TARGET
{
    CameraRay   ray    = BuildCameraRay(input.uv);
    TraceResult result = TraceField(ray);

    // Miss: draw sky.
    if (!result.hit)
        return float4(SkyColor(ray.direction), 1.0);

    // ── Simple diffuse shading ────────────────────────────────────────────
    // One directional sun from above-and-to-the-side, plus a low ambient
    // term so shadowed faces are not pure black.
    // Step 6 will add material colours and multiple terrain tints.

    const float3 sun_dir   = normalize(float3(0.4, 1.0, 0.3));
    const float3 sun_color = float3(1.00, 0.96, 0.88);
    const float3 ambient   = float3(0.18, 0.22, 0.28);

    // Fluid experiments upload water_depth_feet; authored canal water also
    // carries material id 6 so the visual map reads correctly before fluid sim.
    const ColumnData hit_cell = field_cells[result.cell_x + result.cell_z * field_width];
    const bool water_cell = result.water_depth > 0.01 || hit_cell.material_id == 6;
    const float3 base_color = water_cell
        ? float3(0.05, 0.34, 0.82)
        : MaterialColor(hit_cell.material_id);

    const float  n_dot_l = saturate(dot(result.normal, sun_dir));
    float3 color = base_color * (ambient + sun_color * n_dot_l);
    if (water_cell)
    {
        const float depth_t = saturate(result.water_depth * 2.0);
        color = lerp(color, float3(0.18, 0.60, 1.0), 0.25 + 0.25 * depth_t);
    }

    if (result.cell_x == highlight_x && result.cell_z == highlight_z)
        color = lerp(color, float3(1.0, 0.95, 0.25), 0.45);

    return float4(color, 1.0);
}
