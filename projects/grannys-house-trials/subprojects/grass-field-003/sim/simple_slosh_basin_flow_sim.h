#pragma once

// SimpleSloshBasinFlowSim -- fixed-terrain pre-erosion flow on a slosh map.
//
// This is the branch point for the "sloshiness" experiment. Step one keeps the
// known terrain-head pipe flow behavior and swaps only the seed terrain, so we
// can evaluate whether the basin, shelf, island, and spillway invite interesting
// rebound/wave motion before replacing the solver.

#include "i_field_sim.h"
#include "simple_terrain_head_pipe_flow_sim.h"

#include "../third_party/imgui/imgui.h"

#include <vector>

namespace grannys_house_trials::sim
{

class SimpleSloshBasinFlowSim final : public IFieldSim
{
public:
    [[nodiscard]] const char* name() const noexcept override
    {
        return "Slosh Basin Flow";
    }

    [[nodiscard]] SeedMapProfile seed_map_profile() const noexcept override
    {
        return SeedMapProfile::SloshBasin;
    }

    [[nodiscard]] int width() const noexcept override { return flow_.width(); }
    [[nodiscard]] int depth() const noexcept override { return flow_.depth(); }
    [[nodiscard]] float cell_size_feet() const noexcept override
    {
        return flow_.cell_size_feet();
    }

    void set_selected_cell(int x, int z, bool valid) noexcept override
    {
        flow_.set_selected_cell(x, z, valid);
    }

    [[nodiscard]] int height_at(int x, int z) const override
    {
        return flow_.height_at(x, z);
    }

    [[nodiscard]] int water_depth_at(int x, int z) const override
    {
        return flow_.water_depth_at(x, z);
    }

    [[nodiscard]] float surface_height_inches_at(int x, int z) const override
    {
        return flow_.surface_height_inches_at(x, z);
    }

    [[nodiscard]] float water_depth_inches_at(int x, int z) const override
    {
        return flow_.water_depth_inches_at(x, z);
    }

    void reset(int new_width, int new_depth, std::vector<int> heights_inches) override
    {
        flow_.reset(new_width, new_depth, std::move(heights_inches));
    }

    [[nodiscard]] bool step_once() override
    {
        return flow_.step_once();
    }

    [[nodiscard]] bool render_ui() override
    {
        const bool changed = flow_.render_ui();
        ImGui::Separator();
        ImGui::TextDisabled("Slosh branch point: fixed terrain, no erosion.");
        ImGui::TextDisabled("Map: bowl, shelf, island, baffle ridge, and spillway.");
        ImGui::TextDisabled("Next: replace this flow core with explicit slosh velocities.");
        return changed;
    }

private:
    SimpleTerrainHeadPipeFlowSim flow_;
};

} // namespace grannys_house_trials::sim
