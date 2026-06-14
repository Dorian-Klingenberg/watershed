#pragma once

// SimpleShallowWaterSim -- first clean CPU-only shallow-water experiment.
//
// This is intentionally plain: cell-centered water depth plus cell-centered
// horizontal velocity, updated with simple row-major loops. It is a new
// simulation experiment, not an optimization of the cellular flow model.

#include "i_field_sim.h"

#include "../third_party/imgui/imgui.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <utility>
#include <vector>

namespace grannys_house_trials::sim
{

class SimpleShallowWaterSim final : public IFieldSim
{
public:
    [[nodiscard]] const char* name() const noexcept override
    {
        return "Shallow Water Heightfield (CPU Basic)";
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
            throw std::invalid_argument("SimpleShallowWaterSim: width and depth must be positive.");

        if (static_cast<int>(heights_inches.size()) != new_width * new_depth)
            throw std::invalid_argument("SimpleShallowWaterSim: seed size does not match dimensions.");

        width_ = new_width;
        depth_ = new_depth;
        terrain_heights_ = std::move(heights_inches);
        water_depths_.assign(terrain_heights_.size(), 0.0f);
        velocity_x_.assign(terrain_heights_.size(), 0.0f);
        velocity_z_.assign(terrain_heights_.size(), 0.0f);
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

        update_velocities_from_surface_slope();
        advect_water_with_velocity();
        apply_depth_deltas();

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
        ImGui::Text("Max speed: %.2f cells/step", summary_.max_speed);
        ImGui::Text("Last step: %.3f ms", last_step_ms_);
        if (cycle_count_ > 0)
            ImGui::Text("Mean step: %.3f ms", total_step_ms_ / static_cast<double>(cycle_count_));
        ImGui::TextDisabled("CPU basic loops: depth + velocity, no optimization pass.");

        ImGui::Separator();

        ImGui::SliderInt("Brush radius", &add_radius_, 1, 32);
        ImGui::SliderInt("Add depth", &add_depth_inches_, 1, 72, "%d in");
        ImGui::SliderFloat("Gravity", &gravity_, 0.001f, 0.20f, "%.3f");
        ImGui::SliderFloat("Damping", &damping_, 0.80f, 1.00f, "%.3f");
        ImGui::SliderFloat("Flow scale", &flow_scale_, 0.01f, 1.00f, "%.3f");
        ImGui::SliderFloat("Max velocity", &max_velocity_, 0.1f, 12.0f, "%.2f");
        ImGui::SliderFloat("Max flow", &max_flow_inches_, 0.1f, 24.0f, "%.2f in/step");
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
        if (ImGui::Button("Step (x50)"))
        {
            for (int step = 0; step < 50; ++step)
                changed = step_once() || changed;
        }

        ImGui::SameLine();
        if (ImGui::Button("Evaporate 1 in"))
        {
            evaporate(1.0f);
            changed = true;
        }

        if (changed)
            ensure_summary_current();

        ImGui::Separator();
        ImGui::TextDisabled("Renderer height = terrain + shallow-water depth.");
        ImGui::TextDisabled("Velocity gives this experiment directional memory.");
        ImGui::TextDisabled("Coarse experiment: one simulation cell = one foot.");
        return changed;
    }

private:
    [[nodiscard]] std::pair<int, int> water_brush_center() const noexcept
    {
        if (selected_cell_valid_)
            return { selected_x_, selected_z_ };

        return { width_ / 2, depth_ / 2 };
    }

    struct WaterSummary
    {
        int wet_cell_count = 0;
        float total_water_inches = 0.0f;
        float max_water_depth_inches = 0.0f;
        float max_speed = 0.0f;
    };

    [[nodiscard]] std::size_t index_of(int x, int z) const
    {
        if (x < 0 || x >= width_ || z < 0 || z >= depth_)
            throw std::out_of_range("SimpleShallowWaterSim: coordinates out of range.");
        return static_cast<std::size_t>(z * width_ + x);
    }

    [[nodiscard]] std::size_t unchecked_index(int x, int z) const noexcept
    {
        return static_cast<std::size_t>(z * width_ + x);
    }

    [[nodiscard]] float surface_at_clamped(int x, int z) const noexcept
    {
        x = std::clamp(x, 0, width_ - 1);
        z = std::clamp(z, 0, depth_ - 1);
        const std::size_t i = unchecked_index(x, z);
        return static_cast<float>(terrain_heights_[i]) + water_depths_[i];
    }

    void update_velocities_from_surface_slope()
    {
        for (int z = 0; z < depth_; ++z)
        {
            for (int x = 0; x < width_; ++x)
            {
                const std::size_t i = unchecked_index(x, z);
                const float water = water_depths_[i];
                if (water <= minimum_water_inches_)
                {
                    velocity_x_[i] = 0.0f;
                    velocity_z_[i] = 0.0f;
                    continue;
                }

                const float left = surface_at_clamped(x - 1, z);
                const float right = surface_at_clamped(x + 1, z);
                const float up = surface_at_clamped(x, z - 1);
                const float down = surface_at_clamped(x, z + 1);

                const float slope_x = (right - left) * 0.5f;
                const float slope_z = (down - up) * 0.5f;

                velocity_x_[i] = std::clamp(
                    (velocity_x_[i] - gravity_ * slope_x) * damping_,
                    -max_velocity_,
                    max_velocity_);
                velocity_z_[i] = std::clamp(
                    (velocity_z_[i] - gravity_ * slope_z) * damping_,
                    -max_velocity_,
                    max_velocity_);
            }
        }
    }

    void advect_water_with_velocity()
    {
        std::fill(depth_deltas_.begin(), depth_deltas_.end(), 0.0f);

        for (int z = 0; z < depth_; ++z)
        {
            for (int x = 0; x < width_; ++x)
            {
                const std::size_t i = unchecked_index(x, z);
                const float water = water_depths_[i];
                if (water <= minimum_water_inches_)
                    continue;

                float flow_left = std::max(0.0f, -velocity_x_[i]) * water * flow_scale_;
                float flow_right = std::max(0.0f, velocity_x_[i]) * water * flow_scale_;
                float flow_up = std::max(0.0f, -velocity_z_[i]) * water * flow_scale_;
                float flow_down = std::max(0.0f, velocity_z_[i]) * water * flow_scale_;

                if (x == 0 && !drain_edges_)
                    flow_left = 0.0f;
                if (x + 1 == width_ && !drain_edges_)
                    flow_right = 0.0f;
                if (z == 0 && !drain_edges_)
                    flow_up = 0.0f;
                if (z + 1 == depth_ && !drain_edges_)
                    flow_down = 0.0f;

                const float total_requested =
                    flow_left + flow_right + flow_up + flow_down;
                if (total_requested <= minimum_water_inches_)
                    continue;

                const float allowed = std::min(water, max_flow_inches_);
                const float scale = std::min(1.0f, allowed / total_requested);
                flow_left *= scale;
                flow_right *= scale;
                flow_up *= scale;
                flow_down *= scale;

                const float total_outflow =
                    flow_left + flow_right + flow_up + flow_down;
                depth_deltas_[i] -= total_outflow;

                if (x > 0)
                    depth_deltas_[i - 1] += flow_left;
                if (x + 1 < width_)
                    depth_deltas_[i + 1] += flow_right;
                if (z > 0)
                    depth_deltas_[i - static_cast<std::size_t>(width_)] += flow_up;
                if (z + 1 < depth_)
                    depth_deltas_[i + static_cast<std::size_t>(width_)] += flow_down;
            }
        }
    }

    void apply_depth_deltas()
    {
        for (std::size_t i = 0; i < water_depths_.size(); ++i)
        {
            water_depths_[i] = std::max(0.0f, water_depths_[i] + depth_deltas_[i]);
            if (water_depths_[i] <= minimum_water_inches_)
            {
                water_depths_[i] = 0.0f;
                velocity_x_[i] = 0.0f;
                velocity_z_[i] = 0.0f;
            }
        }

        if (!drain_edges_)
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
        velocity_x_[i] = 0.0f;
        velocity_z_[i] = 0.0f;
    }

    void add_water_disc(int center_x, int center_z, int radius, float depth_inches)
    {
        const int radius_squared = radius * radius;
        for (int z = std::max(0, center_z - radius); z <= std::min(depth_ - 1, center_z + radius); ++z)
        {
            for (int x = std::max(0, center_x - radius); x <= std::min(width_ - 1, center_x + radius); ++x)
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
        for (std::size_t i = 0; i < water_depths_.size(); ++i)
        {
            const float water = water_depths_[i];
            if (water > minimum_water_inches_)
                ++summary_.wet_cell_count;
            summary_.total_water_inches += water;
            summary_.max_water_depth_inches =
                std::max(summary_.max_water_depth_inches, water);
            const float speed = std::sqrt(
                velocity_x_[i] * velocity_x_[i] +
                velocity_z_[i] * velocity_z_[i]);
            summary_.max_speed = std::max(summary_.max_speed, speed);
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
    float gravity_ = 0.035f;
    float damping_ = 0.985f;
    float flow_scale_ = 0.30f;
    float max_velocity_ = 4.0f;
    float max_flow_inches_ = 8.0f;
    float minimum_water_inches_ = 0.001f;
    bool drain_edges_ = true;
    bool selected_cell_valid_ = false;
    bool summary_dirty_ = true;
    double last_step_ms_ = 0.0;
    double total_step_ms_ = 0.0;
    WaterSummary summary_{};

    std::vector<int> terrain_heights_;
    std::vector<float> water_depths_;
    std::vector<float> velocity_x_;
    std::vector<float> velocity_z_;
    std::vector<float> depth_deltas_;
};

} // namespace grannys_house_trials::sim
