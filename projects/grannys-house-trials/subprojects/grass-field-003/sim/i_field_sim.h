#pragma once

// IFieldSim — abstract interface for terrain simulators used by grass-field-003.
//
// Any concrete simulator that plugs into the Application must satisfy this contract.
// The interface is intentionally narrow — it captures exactly the three roles the
// rest of the program needs filled, and nothing else:
//
//   RENDERING CONTRACT  (used by initialize_field_buffer, update_field_buffer,
//                        update_scene_constants, update_mouse_picking):
//       width()      — grid column count along X
//       depth()      — grid column count along Z
//       height_at()  — legacy integer visible height at a given (x, z), in inches
//       water_depth_at() — legacy integer water depth at that cell, in inches
//       surface_height_inches_at() / water_depth_inches_at()
//                    — optional precise values for fractional-fluid renderers
//       cell_size_feet()
//                    — render/simulation footprint of one grid cell
//       append_visual_points()
//                    — optional lightweight overlay markers, such as falling
//                      rain drops, drawn by the app after the terrain pass
//       diagnostic scalar channels
//                    — optional values for renderer/debug overlays, such as
//                      velocity, suspended sediment, and recent erosion
//
//   LIFECYCLE CONTRACT  (used by reset_field):
//       seed_map_profile()
//                    — optional request for a purpose-built seed map. Most
//                      experiments use the shared GrassField seed; specialized
//                      experiments can ask for a map that better reveals their
//                      behavior.
//       reset()      — discard current state, re-seed from fresh height data
//       step_once()  — advance one logical simulation tick when running
//
//   UI CONTRACT         (used by render_imgui):
//       name()       — displayed in the simulator-picker combo
//       set_selected_cell()
//                    — receives the app's current interaction target before UI
//                      rendering; simulators may use it for brush-style tools
//       render_ui()  — draws this simulator's own ImGui controls (step buttons,
//                      cycle counters, tuning sliders, etc.) inside an already-
//                      open ImGui window; must NOT call Begin() / End(); returns
//                      true when visible field data changed
//
// The render_ui() method is the anti-coupling keystone. Application never asks
// "how many cycles have run?" or "what is your erosion threshold?". Each simulator
// describes and exposes its own internals, while Application stays completely
// ignorant of them. Adding a new simulator type with different parameters never
// requires a single change to Application.

#include <vector>

struct ID3D12Resource;

namespace grannys_house_trials::sim
{

enum class SeedMapProfile
{
    SharedGrassField,
    ErosionInclineValleys,
    SloshBasin,
};

struct FieldVisualPoint
{
    float x_feet = 0.0f;
    float y_feet = 0.0f;
    float z_feet = 0.0f;
    float radius_pixels = 3.0f;
    float r = 0.2f;
    float g = 0.55f;
    float b = 1.0f;
    float a = 0.85f;
};

class IFieldSim
{
public:
    virtual ~IFieldSim() = default;

    // Human-readable name shown in the simulator-picker combo.
    [[nodiscard]] virtual const char* name() const noexcept = 0;

    // Spatial dimensions of the simulation grid (in columns).
    [[nodiscard]] virtual int width() const noexcept = 0;
    [[nodiscard]] virtual int depth() const noexcept = 0;

    // World-space footprint of one simulation/render cell. Most GF003
    // experiments use the default 1-inch grid; coarse experiments can override
    // this to trade detail for speed.
    [[nodiscard]] virtual float cell_size_feet() const noexcept
    {
        return 1.0f / 12.0f;
    }

    // Column height at grid position (x, z), in inches.
    // The caller is responsible for bounds-checking via width() / depth() first.
    [[nodiscard]] virtual int height_at(int x, int z) const = 0;

    // Water depth at grid position (x, z), in inches. Most non-fluid
    // simulators return zero; fluid experiments override this so renderers can
    // draw water separately from terrain.
    [[nodiscard]] virtual int water_depth_at(int x, int z) const
    {
        (void)x;
        (void)z;
        return 0;
    }

    // Precise visible surface and water-depth values in inches. Integer-based
    // experiments inherit these adapters unchanged; fluid experiments override
    // them so rendering does not throw away fractional water levels.
    [[nodiscard]] virtual float surface_height_inches_at(int x, int z) const
    {
        return static_cast<float>(height_at(x, z));
    }

    [[nodiscard]] virtual float water_depth_inches_at(int x, int z) const
    {
        return static_cast<float>(water_depth_at(x, z));
    }

    [[nodiscard]] virtual float velocity_magnitude_feet_per_second_at(
        int x, int z) const
    {
        (void)x;
        (void)z;
        return 0.0f;
    }

    [[nodiscard]] virtual float suspended_sediment_inches_at(int x, int z) const
    {
        (void)x;
        (void)z;
        return 0.0f;
    }

    // Positive means deposition raised the terrain; negative means erosion cut it.
    [[nodiscard]] virtual float terrain_delta_inches_at(int x, int z) const
    {
        (void)x;
        (void)z;
        return 0.0f;
    }

    // Optional GPU-resident field resources. CPU simulations and validation
    // GPU modes inherit null resources; a resident compute experiment exposes
    // terrain and current water buffers for a matching renderer.
    [[nodiscard]] virtual bool has_gpu_resident_field() const noexcept { return false; }
    [[nodiscard]] virtual ID3D12Resource* gpu_terrain_resource() const noexcept { return nullptr; }
    [[nodiscard]] virtual ID3D12Resource* gpu_water_resource() const noexcept { return nullptr; }

    // Optional per-frame visual markers. These do not affect terrain buffers and
    // are intentionally separate from the column renderer, so a simulator can
    // show transient state without inventing fake terrain/water columns.
    virtual void append_visual_points(std::vector<FieldVisualPoint>& points) const
    {
        (void)points;
    }

    // Seed map profile requested at reset time. This is intentionally a small
    // catalog rather than a runtime map switch: each simulator can declare the
    // terrain that best exposes its behavior while Application keeps the reset
    // and rendering path generic.
    [[nodiscard]] virtual SeedMapProfile seed_map_profile() const noexcept
    {
        return SeedMapProfile::SharedGrassField;
    }

    // Discard all simulation state and re-initialise from the provided heights.
    // heights_inches must contain exactly (new_width * new_depth) entries in
    // row-major (z * new_width + x) order — the same layout SimpleErosionField uses.
    // Called by reset_field() whenever the user presses Reset or swaps simulators.
    virtual void reset(int new_width, int new_depth,
                       std::vector<int> heights_inches) = 0;

    // Advance by one logical simulation tick. Returns true when visible
    // terrain/water data changed and the GPU field buffer needs refreshing.
    [[nodiscard]] virtual bool step_once() = 0;

    // Provide the currently selected app cell before drawing simulator-specific
    // controls. Simulators that do not expose brush-style interactions can ignore it.
    virtual void set_selected_cell(int x, int z, bool valid) noexcept
    {
        (void)x;
        (void)z;
        (void)valid;
    }

    // Draw this simulator's ImGui controls inside the caller's already-open window.
    // Each simulator is free to show whatever it likes here: cycle counts, threshold
    // sliders, "Step x1 / x100" buttons, heat-map overlays, etc. Returns true
    // when the simulator changed visible terrain/water data and the GPU field
    // buffer needs to be refreshed.
    // Must NOT call ImGui::Begin() or ImGui::End().
    [[nodiscard]] virtual bool render_ui() = 0;
};

} // namespace grannys_house_trials::sim
