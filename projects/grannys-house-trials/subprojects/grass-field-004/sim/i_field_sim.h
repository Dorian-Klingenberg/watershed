#pragma once

// IFieldSim — abstract interface for terrain simulators used by grass-field-004.
//
// Any concrete simulator that plugs into the Application must satisfy this contract.
// The interface is intentionally narrow — it captures exactly the three roles the
// rest of the program needs filled, and nothing else:
//
//   RENDERING CONTRACT  (used by initialize_field_buffer, update_field_buffer,
//                        update_scene_constants, update_mouse_picking):
//       width()      — grid column count along X
//       depth()      — grid column count along Z
//       height_at()  — visible surface height at a given (x, z) grid cell, in inches
//       water_depth_at() — optional water depth at that cell, in inches
//
//   LIFECYCLE CONTRACT  (used by reset_field):
//       reset()      — discard current state, re-seed from fresh height data
//
//   UI CONTRACT         (used by render_imgui):
//       name()       — displayed in the simulator-picker combo
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

#include <cstdint>
#include <vector>

namespace grannys_house_trials::sim
{

class IFieldSim
{
public:
    virtual ~IFieldSim() = default;

    // Human-readable name shown in the simulator-picker combo.
    [[nodiscard]] virtual const char* name() const noexcept = 0;

    // Spatial dimensions of the simulation grid (in columns).
    [[nodiscard]] virtual int width() const noexcept = 0;
    [[nodiscard]] virtual int depth() const noexcept = 0;

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

    // Material id for color-coded renderers. Simulators that do not carry
    // material data render as grass by default.
    [[nodiscard]] virtual std::uint32_t material_id_at(int x, int z) const
    {
        (void)x;
        (void)z;
        return 0;
    }

    [[nodiscard]] virtual const char* material_name_at(int x, int z) const
    {
        (void)x;
        (void)z;
        return "Grass";
    }

    // Discard all simulation state and re-initialise from the provided heights.
    // heights_inches must contain exactly (new_width * new_depth) entries in
    // row-major (z * new_width + x) order.
    // Called by reset_field() whenever the user presses Reset or swaps simulators.
    virtual void reset(int new_width, int new_depth,
                       std::vector<int> heights_inches) = 0;

    // Draw this simulator's ImGui controls inside the caller's already-open window.
    // Each simulator is free to show whatever it likes here: cycle counts, threshold
    // sliders, "Step x1 / x100" buttons, heat-map overlays, etc. Returns true
    // when the simulator changed visible terrain/water data and the GPU field
    // buffer needs to be refreshed.
    // Must NOT call ImGui::Begin() or ImGui::End().
    [[nodiscard]] virtual bool render_ui() = 0;
};

} // namespace grannys_house_trials::sim
