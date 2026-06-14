#pragma once

// SimpleErosionSim — IFieldSim adapter around SimpleErosionField.
//
// SimpleErosionField (simple_erosion_field.h) is a pure C++ data type that knows
// nothing about ImGui or the Application. This thin adapter class adds the two
// things Application needs that SimpleErosionField does not provide on its own:
//
//   • reset(w, d, heights) — conforms to the IFieldSim lifecycle contract,
//     forwarding to the SimpleErosionField constructor.
//
//   • render_ui()          — draws the simulation's own ImGui controls
//     (cycle count display and step buttons) entirely inside this class,
//     so Application never needs to reach into m_field directly. Returns true
//     when a button mutates visible terrain data.
//
// To add a new simulator variant in the future, write another IFieldSim subclass
// and register it in Application's simulator factory. Nothing else changes.

#include "i_field_sim.h"
#include "simple_erosion_field.h"

#include "../third_party/imgui/imgui.h"

namespace grannys_house_trials::sim
{

class SimpleErosionSim final : public IFieldSim
{
public:
    // Default-constructs an empty (unusable) field. Must call reset() before
    // any rendering or picking queries are made.
    SimpleErosionSim() = default;

    // ── IFieldSim identity ────────────────────────────────────────────────────

    [[nodiscard]] const char* name() const noexcept override
    {
        return "Simple Gravity Erosion";
    }

    [[nodiscard]] SeedMapProfile seed_map_profile() const noexcept override
    {
        return SeedMapProfile::ErosionInclineValleys;
    }

    // ── IFieldSim rendering contract ──────────────────────────────────────────

    [[nodiscard]] int width() const noexcept override { return m_field.width(); }
    [[nodiscard]] int depth() const noexcept override { return m_field.depth(); }

    [[nodiscard]] int height_at(int x, int z) const override
    {
        return m_field.height_at(x, z);
    }

    // ── IFieldSim lifecycle contract ──────────────────────────────────────────

    // Discard the current field and re-build it from the provided seed heights.
    // Called by Application::reset_field() — either at startup, on Reset button,
    // or when the user switches to a different simulator.
    void reset(int new_width, int new_depth,
               std::vector<int> heights_inches) override
    {
        m_field = SimpleErosionField(new_width, new_depth, std::move(heights_inches));
    }

    [[nodiscard]] bool step_once() override
    {
        m_field.step_cycle();
        return true;
    }

    // ── IFieldSim UI contract ─────────────────────────────────────────────────

    // Draw cycle count and step controls inside the caller's ImGui window.
    // Application knows nothing about cycles, thresholds, or step counts —
    // this class owns all of that display logic.
    [[nodiscard]] bool render_ui() override
    {
        ImGui::Text("Cycles: %d", m_field.cycle_count());
        ImGui::TextDisabled("Seed map: incline, valleys, ridges, and lower basin.");
        ImGui::Separator();

        bool changed = false;

        // Advance by one gravity-settling pass.
        if (ImGui::Button("Step (x1)"))
            changed = step_once() || changed;

        ImGui::SameLine();

        // Advance by 100 passes — useful for watching long-run convergence.
        if (ImGui::Button("Step (x100)"))
        {
            for (int i = 0; i < 100; ++i)
                changed = step_once() || changed;
        }

        return changed;
    }

private:
    SimpleErosionField m_field;
};

} // namespace grannys_house_trials::sim
