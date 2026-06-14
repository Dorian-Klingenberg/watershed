#pragma once

// SimpleVirtualPipeFluidSim -- CPU experiment inspired by O'Brien/Hodgins.
//
// The paper models the main fluid volume as vertical columns connected by
// virtual pipes. This experiment keeps that useful part and skips splash
// particles, object collision, and a separate surface mesh for now.

#include "i_field_sim.h"

#include "../third_party/imgui/imgui.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <utility>
#include <vector>

namespace grannys_house_trials::sim
{

class SimpleVirtualPipeFluidSim final : public IFieldSim
{
public:
    [[nodiscard]] const char* name() const noexcept override
    {
        return "Virtual Pipe Fluid (8 Neighbors)";
    }

    [[nodiscard]] int width() const noexcept override { return width_; }
    [[nodiscard]] int depth() const noexcept override { return depth_; }

    void set_selected_cell(int x, int z, bool valid) noexcept override
    {
        selected_x_ = x;
        selected_z_ = z;
        selected_cell_valid_ =
            valid && x >= 0 && x < width_ && z >= 0 && z < depth_;
    }

    [[nodiscard]] float cell_size_feet() const noexcept override
    {
        return 1.0f;
    }

    [[nodiscard]] int height_at(int x, int z) const override
    {
        return static_cast<int>(std::lround(surface_height_inches_at(x, z)));
    }

    [[nodiscard]] int water_depth_at(int x, int z) const override
    {
        return static_cast<int>(std::lround(water_depth_inches_at(x, z)));
    }

    [[nodiscard]] float surface_height_inches_at(int x, int z) const override
    {
        const std::size_t i = index_of(x, z);
        return static_cast<float>(terrain_heights_[i]) + water_depths_[i];
    }

    [[nodiscard]] float water_depth_inches_at(int x, int z) const override
    {
        return water_depths_[index_of(x, z)];
    }

    void reset(int new_width, int new_depth, std::vector<int> heights_inches) override
    {
        if (new_width <= 0 || new_depth <= 0)
            throw std::invalid_argument("SimpleVirtualPipeFluidSim: width and depth must be positive.");

        if (static_cast<int>(heights_inches.size()) != new_width * new_depth)
            throw std::invalid_argument("SimpleVirtualPipeFluidSim: seed size does not match dimensions.");

        width_ = new_width;
        depth_ = new_depth;
        terrain_heights_ = std::move(heights_inches);
        water_depths_.assign(terrain_heights_.size(), 0.0f);
        pipe_flows_.assign(terrain_heights_.size() * k_direction_count, 0.0f);
        proposed_transfers_.assign(terrain_heights_.size() * k_direction_count, 0.0f);
        depth_deltas_.assign(terrain_heights_.size(), 0.0f);
        cycle_count_ = 0;
        last_step_ms_ = 0.0;
        total_step_ms_ = 0.0;
        summary_dirty_ = true;
    }

    [[nodiscard]] bool step_once() override
    {
        if (water_depths_.empty())
            return false;

        const auto start = std::chrono::steady_clock::now();

        update_pipe_flows_and_proposed_transfers();
        apply_proposed_transfers();

        ++cycle_count_;
        summary_dirty_ = true;

        const auto stop = std::chrono::steady_clock::now();
        last_step_ms_ = std::chrono::duration<double, std::milli>(stop - start).count();
        total_step_ms_ += last_step_ms_;
        return true;
    }

    [[nodiscard]] bool render_ui() override
    {
        ensure_summary_current();

        ImGui::Text("Cycles: %d", cycle_count_);
        ImGui::Text("Wet cells: %d", summary_.wet_cell_count);
        ImGui::Text("Total water: %.1f in", summary_.total_water_inches);
        ImGui::Text("Max depth: %.2f in", summary_.max_water_depth_inches);
        ImGui::Text("Max pipe flow: %.3f in/step", summary_.max_pipe_flow_inches);
        ImGui::Text("Last step: %.3f ms", last_step_ms_);
        if (cycle_count_ > 0)
            ImGui::Text("Mean step: %.3f ms", total_step_ms_ / static_cast<double>(cycle_count_));
        ImGui::TextDisabled("O'Brien/Hodgins-inspired volume pipes; no spray model.");
        ImGui::TextDisabled("Coarse experiment: one simulation cell = one foot.");

        ImGui::Separator();

        ImGui::SliderInt("Brush radius", &add_radius_, 1, 32);
        ImGui::SliderInt("Add depth", &add_depth_inches_, 1, 72, "%d in");
        ImGui::SliderFloat("Pressure scale", &pressure_scale_, 0.001f, 0.50f, "%.3f");
        ImGui::SliderFloat("Time step", &time_step_, 0.05f, 1.00f, "%.3f");
        ImGui::SliderFloat("Pipe area", &pipe_area_, 0.05f, 2.00f, "%.3f");
        ImGui::SliderFloat("Flow damping", &flow_damping_, 0.50f, 1.00f, "%.3f");
        ImGui::SliderFloat("Max out/cell", &max_outflow_inches_, 0.25f, 36.0f, "%.2f in");
        ImGui::Checkbox("Diagonal pipes", &use_diagonal_pipes_);
        ImGui::Checkbox("Drain edges", &drain_edges_);

        bool changed = false;
        if (selected_cell_valid_)
            ImGui::TextDisabled("Water brush target: selected [%d, %d]", selected_x_, selected_z_);
        else
            ImGui::TextDisabled("Water brush target: field center.");

        if (ImGui::Button("Add Water at Target"))
        {
            const auto center = water_brush_center();
            add_water_disc(center.first, center.second, add_radius_,
                           static_cast<float>(add_depth_inches_));
            changed = true;
        }

        ImGui::SameLine();
        if (ImGui::Button("Add Rain"))
        {
            add_rain(1.0f);
            changed = true;
        }

        if (ImGui::Button("Step (x1)"))
            changed = step_once() || changed;

        ImGui::SameLine();
        if (ImGui::Button("Step (x25)"))
        {
            for (int step = 0; step < 25; ++step)
                changed = step_once() || changed;
        }

        ImGui::SameLine();
        if (ImGui::Button("Evaporate 1 in"))
        {
            evaporate(1.0f);
            changed = true;
        }

        if (ImGui::Button("Zero Pipe Momentum"))
        {
            std::fill(pipe_flows_.begin(), pipe_flows_.end(), 0.0f);
            summary_dirty_ = true;
        }

        if (changed)
            ensure_summary_current();

        ImGui::Separator();
        ImGui::TextDisabled("Surface = terrain + water depth.");
        ImGui::TextDisabled("Signed pipe flow gives the tunnels memory.");
        return changed;
    }

private:
    [[nodiscard]] std::pair<int, int> water_brush_center() const noexcept
    {
        if (selected_cell_valid_)
            return { selected_x_, selected_z_ };

        return { width_ / 2, depth_ / 2 };
    }

    struct Direction
    {
        int dx = 0;
        int dz = 0;
        int opposite = 0;
        float length = 1.0f;
    };

    struct WaterSummary
    {
        int wet_cell_count = 0;
        float total_water_inches = 0.0f;
        float max_water_depth_inches = 0.0f;
        float max_pipe_flow_inches = 0.0f;
    };

    static constexpr int k_direction_count = 8;
    static constexpr float k_sqrt_two = 1.41421356237f;
    static constexpr std::array<Direction, k_direction_count> k_directions = {{
        { 1,  0, 1, 1.0f       }, // east
        {-1,  0, 0, 1.0f       }, // west
        { 0,  1, 3, 1.0f       }, // south
        { 0, -1, 2, 1.0f       }, // north
        { 1,  1, 5, k_sqrt_two }, // southeast
        {-1, -1, 4, k_sqrt_two }, // northwest
        {-1,  1, 7, k_sqrt_two }, // southwest
        { 1, -1, 6, k_sqrt_two }, // northeast
    }};
    static constexpr std::array<int, 4> k_unique_direction_indices = {{ 0, 2, 4, 6 }};

    [[nodiscard]] std::size_t index_of(int x, int z) const
    {
        if (x < 0 || x >= width_ || z < 0 || z >= depth_)
            throw std::out_of_range("SimpleVirtualPipeFluidSim: coordinates out of range.");
        return unchecked_index(x, z);
    }

    [[nodiscard]] std::size_t unchecked_index(int x, int z) const noexcept
    {
        return static_cast<std::size_t>(z * width_ + x);
    }

    [[nodiscard]] std::size_t pipe_index(std::size_t cell_index, int direction_index) const noexcept
    {
        return cell_index * k_direction_count + static_cast<std::size_t>(direction_index);
    }

    [[nodiscard]] bool in_bounds(int x, int z) const noexcept
    {
        return x >= 0 && x < width_ && z >= 0 && z < depth_;
    }

    [[nodiscard]] float surface_at(std::size_t i) const noexcept
    {
        return static_cast<float>(terrain_heights_[i]) + water_depths_[i];
    }

    [[nodiscard]] float active_head_delta(std::size_t a, std::size_t b) const noexcept
    {
        const float surface_a = surface_at(a);
        const float surface_b = surface_at(b);
        const float delta = surface_a - surface_b;
        if (delta > 0.0f && water_depths_[a] > minimum_water_inches_)
            return delta;
        if (delta < 0.0f && water_depths_[b] > minimum_water_inches_)
            return delta;
        return 0.0f;
    }

    void update_pipe_flows_and_proposed_transfers()
    {
        std::fill(proposed_transfers_.begin(), proposed_transfers_.end(), 0.0f);

        for (int z = 0; z < depth_; ++z)
        {
            for (int x = 0; x < width_; ++x)
            {
                const std::size_t source_i = unchecked_index(x, z);

                for (int direction_index : k_unique_direction_indices)
                {
                    if (!use_diagonal_pipes_ && direction_index >= 4)
                        continue;

                    const Direction direction = k_directions[static_cast<std::size_t>(direction_index)];
                    const int nx = x + direction.dx;
                    const int nz = z + direction.dz;
                    if (!in_bounds(nx, nz))
                        continue;

                    const std::size_t neighbor_i = unchecked_index(nx, nz);
                    const std::size_t flow_i = pipe_index(source_i, direction_index);
                    const std::size_t opposite_flow_i =
                        pipe_index(neighbor_i, direction.opposite);

                    const float old_flow = pipe_flows_[flow_i];
                    const float head_delta = active_head_delta(source_i, neighbor_i);
                    const float acceleration = pressure_scale_ * head_delta / direction.length;
                    float new_flow = (old_flow + time_step_ * pipe_area_ * acceleration)
                        * flow_damping_;

                    if (std::abs(new_flow) <= minimum_pipe_flow_)
                        new_flow = 0.0f;

                    pipe_flows_[flow_i] = new_flow;
                    pipe_flows_[opposite_flow_i] = -new_flow;

                    const float transfer = time_step_ * (old_flow + new_flow) * 0.5f;
                    if (transfer > minimum_water_inches_)
                    {
                        proposed_transfers_[flow_i] = transfer;
                    }
                    else if (transfer < -minimum_water_inches_)
                    {
                        proposed_transfers_[opposite_flow_i] = -transfer;
                    }
                }
            }
        }
    }

    void apply_proposed_transfers()
    {
        std::fill(depth_deltas_.begin(), depth_deltas_.end(), 0.0f);

        for (int z = 0; z < depth_; ++z)
        {
            for (int x = 0; x < width_; ++x)
            {
                const std::size_t source_i = unchecked_index(x, z);
                float requested_outflow = 0.0f;
                for (int direction_index = 0; direction_index < k_direction_count; ++direction_index)
                    requested_outflow += proposed_transfers_[pipe_index(source_i, direction_index)];

                if (requested_outflow <= minimum_water_inches_)
                    continue;

                const float allowed_outflow =
                    std::min(water_depths_[source_i], max_outflow_inches_);
                const float scale = std::min(1.0f, allowed_outflow / requested_outflow);

                for (int direction_index = 0; direction_index < k_direction_count; ++direction_index)
                {
                    const std::size_t transfer_i = pipe_index(source_i, direction_index);
                    const float proposed = proposed_transfers_[transfer_i];
                    if (proposed <= minimum_water_inches_)
                        continue;

                    const Direction direction =
                        k_directions[static_cast<std::size_t>(direction_index)];
                    const int nx = x + direction.dx;
                    const int nz = z + direction.dz;
                    if (!in_bounds(nx, nz))
                        continue;

                    const float actual = proposed * scale;
                    const std::size_t neighbor_i = unchecked_index(nx, nz);
                    depth_deltas_[source_i] -= actual;
                    depth_deltas_[neighbor_i] += actual;

                    if (scale < 1.0f)
                        scale_pipe_flow(source_i, neighbor_i, direction_index, scale);
                }
            }
        }

        for (std::size_t i = 0; i < water_depths_.size(); ++i)
        {
            water_depths_[i] = std::max(0.0f, water_depths_[i] + depth_deltas_[i]);
            if (water_depths_[i] <= minimum_water_inches_)
                water_depths_[i] = 0.0f;
        }

        if (drain_edges_)
            drain_edge_cells();
    }

    void scale_pipe_flow(std::size_t source_i, std::size_t neighbor_i,
                         int direction_index, float scale)
    {
        const int opposite = k_directions[static_cast<std::size_t>(direction_index)].opposite;
        pipe_flows_[pipe_index(source_i, direction_index)] *= scale;
        pipe_flows_[pipe_index(neighbor_i, opposite)] *= scale;
    }

    void drain_edge_cells()
    {
        if (width_ <= 0 || depth_ <= 0)
            return;

        for (int x = 0; x < width_; ++x)
        {
            clear_cell(unchecked_index(x, 0));
            clear_cell(unchecked_index(x, depth_ - 1));
        }
        for (int z = 0; z < depth_; ++z)
        {
            clear_cell(unchecked_index(0, z));
            clear_cell(unchecked_index(width_ - 1, z));
        }
    }

    void clear_cell(std::size_t i)
    {
        water_depths_[i] = 0.0f;
        for (int direction_index = 0; direction_index < k_direction_count; ++direction_index)
        {
            const std::size_t flow_i = pipe_index(i, direction_index);
            pipe_flows_[flow_i] = 0.0f;
        }
    }

    void add_water_disc(int center_x, int center_z, int radius, float depth_inches)
    {
        const int radius_squared = radius * radius;
        for (int z = std::max(0, center_z - radius);
             z <= std::min(depth_ - 1, center_z + radius);
             ++z)
        {
            for (int x = std::max(0, center_x - radius);
                 x <= std::min(width_ - 1, center_x + radius);
                 ++x)
            {
                const int dx = x - center_x;
                const int dz = z - center_z;
                if (dx * dx + dz * dz <= radius_squared)
                    water_depths_[unchecked_index(x, z)] += depth_inches;
            }
        }
        summary_dirty_ = true;
    }

    void add_rain(float depth_inches)
    {
        for (float& water : water_depths_)
            water += depth_inches;
        summary_dirty_ = true;
    }

    void evaporate(float depth_inches)
    {
        for (float& water : water_depths_)
            water = std::max(0.0f, water - depth_inches);
        summary_dirty_ = true;
    }

    void ensure_summary_current()
    {
        if (!summary_dirty_)
            return;

        summary_ = {};
        for (float water : water_depths_)
        {
            if (water > minimum_water_inches_)
                ++summary_.wet_cell_count;
            summary_.total_water_inches += water;
            summary_.max_water_depth_inches =
                std::max(summary_.max_water_depth_inches, water);
        }

        for (float flow : pipe_flows_)
        {
            summary_.max_pipe_flow_inches =
                std::max(summary_.max_pipe_flow_inches, std::abs(flow));
        }

        summary_dirty_ = false;
    }

    int width_ = 0;
    int depth_ = 0;
    int cycle_count_ = 0;
    int selected_x_ = 0;
    int selected_z_ = 0;
    int add_radius_ = 11;
    int add_depth_inches_ = 22;
    float pressure_scale_ = 0.12f;
    float time_step_ = 0.50f;
    float pipe_area_ = 0.75f;
    float flow_damping_ = 0.985f;
    float max_outflow_inches_ = 10.0f;
    float minimum_water_inches_ = 0.001f;
    float minimum_pipe_flow_ = 0.00001f;
    bool use_diagonal_pipes_ = true;
    bool drain_edges_ = true;
    bool selected_cell_valid_ = false;
    bool summary_dirty_ = true;
    double last_step_ms_ = 0.0;
    double total_step_ms_ = 0.0;
    WaterSummary summary_{};

    std::vector<int> terrain_heights_;
    std::vector<float> water_depths_;
    std::vector<float> pipe_flows_;
    std::vector<float> proposed_transfers_;
    std::vector<float> depth_deltas_;
};

} // namespace grannys_house_trials::sim
