#pragma once

// SimpleCellularFluidSimActiveSources -- deterministic sparse-work experiment.
//
// This preserves the Round 1 flow equation and row-major contribution order,
// while only evaluating sources whose water exceeds the minimum flow threshold.
// Delta clearing/application remain full-field operations in this first round
// so the optimization changes as little of the tick contract as possible.

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

class SimpleCellularFluidSimActiveSources final : public IFieldSim
{
public:
    [[nodiscard]] const char* name() const noexcept override
    {
        return "Cellular Water Flow (Optimized Round 2 - Active Sources)";
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
            throw std::invalid_argument("SimpleCellularFluidSimActiveSources: width and depth must be positive.");

        if (static_cast<int>(heights_inches.size()) != new_width * new_depth)
            throw std::invalid_argument("SimpleCellularFluidSimActiveSources: seed size does not match dimensions.");

        width_ = new_width;
        depth_ = new_depth;
        cycle_count_ = 0;
        terrain_heights_ = std::move(heights_inches);
        water_depths_.assign(terrain_heights_.size(), 0.0f);
        deltas_.assign(terrain_heights_.size(), 0.0f);
        active_sources_.clear();
        active_sources_.reserve(terrain_heights_.size());
        build_drain_indices();
        clear_water_summary_cache();
        last_step_ms_ = 0.0;
        total_step_ms_ = 0.0;
    }

    void add_center_water(int radius, float depth_inches)
    {
        add_water_disc(width_ / 2, depth_ / 2, radius, depth_inches);
    }

    void add_uniform_rain(float depth_inches)
    {
        add_rain(depth_inches);
    }

    [[nodiscard]] bool step_once() override
    {
        if (water_depths_.empty())
            return false;

        const auto start = std::chrono::steady_clock::now();
        std::fill(deltas_.begin(), deltas_.end(), 0.0f);

        // Active sources are rebuilt in ascending array order, so skipping
        // dry early-outs leaves all contributing additions in baseline order.
        for (std::size_t current_i : active_sources_)
            accumulate_source_flow(current_i);

        for (std::size_t i = 0; i < water_depths_.size(); ++i)
            water_depths_[i] = std::max(0.0f, water_depths_[i] + deltas_[i]);

        if (drain_edges_)
        {
            for (std::size_t i : drain_indices_)
                water_depths_[i] = 0.0f;
        }

        rebuild_active_sources();
        ++cycle_count_;
        mark_water_summary_dirty();

        const auto stop = std::chrono::steady_clock::now();
        last_step_ms_ = std::chrono::duration<double, std::milli>(stop - start).count();
        total_step_ms_ += last_step_ms_;
        return true;
    }

    [[nodiscard]] bool render_ui() override
    {
        ensure_water_summary_current();

        ImGui::Text("Cycles: %d", cycle_count_);
        ImGui::Text("Wet cells: %d", water_summary_.wet_cell_count);
        ImGui::Text("Active sources: %zu", active_sources_.size());
        ImGui::Text("Total water: %.1f in", water_summary_.total_water_inches);
        ImGui::Text("Max depth: %.2f in", water_summary_.max_water_depth_inches);
        ImGui::Text("Last step: %.3f ms", last_step_ms_);
        if (cycle_count_ > 0)
            ImGui::Text("Mean step: %.3f ms", total_step_ms_ / static_cast<double>(cycle_count_));
        ImGui::TextDisabled("Round 2: ordered active sources + full-field apply.");

        ImGui::Separator();

        ImGui::SliderInt("Brush radius", &add_radius_, 1, 16);
        ImGui::SliderInt("Add depth", &add_depth_inches_, 1, 48, "%d in");
        ImGui::SliderFloat("Max flow", &max_flow_inches_, 0.1f, 8.0f, "%.2f in/step");
        ImGui::SliderFloat("Settle rate", &settle_rate_, 0.05f, 1.0f, "%.2f");
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
            add_uniform_rain(1.0f);
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
            ensure_water_summary_current();

        ImGui::TextDisabled("Renderer height = terrain + water surface.");
        return changed;
    }

private:
    [[nodiscard]] std::pair<int, int> water_brush_center() const noexcept
    {
        if (selected_cell_valid_)
            return { selected_x_, selected_z_ };

        return { width_ / 2, depth_ / 2 };
    }

    struct LowerNeighbor
    {
        std::size_t index = 0;
        float surface = 0.0f;
        float desired_inflow = 0.0f;
    };

    struct WaterSummary
    {
        int wet_cell_count = 0;
        float total_water_inches = 0.0f;
        float max_water_depth_inches = 0.0f;
    };

    [[nodiscard]] std::size_t index_of(int x, int z) const
    {
        if (x < 0 || x >= width_ || z < 0 || z >= depth_)
            throw std::out_of_range("SimpleCellularFluidSimActiveSources: coordinates out of range.");

        return static_cast<std::size_t>(z * width_ + x);
    }

    void build_drain_indices()
    {
        drain_indices_.clear();
        drain_indices_.reserve(static_cast<std::size_t>(2 * width_ + 2 * depth_));
        for (int x = 0; x < width_; ++x)
        {
            drain_indices_.push_back(static_cast<std::size_t>(x));
            drain_indices_.push_back(static_cast<std::size_t>((depth_ - 1) * width_ + x));
        }
        for (int z = 0; z < depth_; ++z)
        {
            drain_indices_.push_back(static_cast<std::size_t>(z * width_));
            drain_indices_.push_back(static_cast<std::size_t>(z * width_ + width_ - 1));
        }
    }

    void rebuild_active_sources()
    {
        active_sources_.clear();
        for (std::size_t i = 0; i < water_depths_.size(); ++i)
        {
            if (water_depths_[i] > minimum_water_inches_)
                active_sources_.push_back(i);
        }
    }

    void accumulate_source_flow(std::size_t current_i)
    {
        const float available_water = water_depths_[current_i];
        if (available_water <= minimum_water_inches_)
            return;

        const std::size_t row_width = static_cast<std::size_t>(width_);
        const int x = static_cast<int>(current_i % row_width);
        const int z = static_cast<int>(current_i / row_width);
        const float current_surface =
            static_cast<float>(terrain_heights_[current_i]) + available_water;
        std::array<LowerNeighbor, 4> lower_neighbors{};
        int lower_neighbor_count = 0;
        float surface_sum = current_surface;

        const auto gather_lower_neighbor =
            [&](std::size_t neighbor_i)
            {
                const float neighbor_surface =
                    static_cast<float>(terrain_heights_[neighbor_i]) + water_depths_[neighbor_i];
                if (neighbor_surface >= current_surface - minimum_water_inches_)
                    return;

                lower_neighbors[static_cast<std::size_t>(lower_neighbor_count++)] = {
                    neighbor_i, neighbor_surface, 0.0f
                };
                surface_sum += neighbor_surface;
            };

        // Preserve baseline neighbor order: left, right, up, down.
        if (x > 0)
            gather_lower_neighbor(current_i - 1);
        if (x + 1 < width_)
            gather_lower_neighbor(current_i + 1);
        if (z > 0)
            gather_lower_neighbor(current_i - row_width);
        if (z + 1 < depth_)
            gather_lower_neighbor(current_i + row_width);

        if (lower_neighbor_count == 0)
            return;

        const float shared_surface =
            surface_sum / static_cast<float>(lower_neighbor_count + 1);
        float desired_outflow = 0.0f;
        for (int i = 0; i < lower_neighbor_count; ++i)
        {
            LowerNeighbor& neighbor = lower_neighbors[static_cast<std::size_t>(i)];
            neighbor.desired_inflow =
                std::max(0.0f, shared_surface - neighbor.surface);
            desired_outflow += neighbor.desired_inflow;
        }

        if (desired_outflow <= minimum_water_inches_)
            return;

        const float total_flow = std::min({
            available_water,
            max_flow_inches_,
            desired_outflow * settle_rate_
        });
        if (total_flow <= minimum_water_inches_)
            return;

        const float flow_scale = total_flow / desired_outflow;
        deltas_[current_i] -= total_flow;
        for (int i = 0; i < lower_neighbor_count; ++i)
        {
            const LowerNeighbor& neighbor = lower_neighbors[static_cast<std::size_t>(i)];
            deltas_[neighbor.index] += neighbor.desired_inflow * flow_scale;
        }
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
                    water_depths_[static_cast<std::size_t>(z * width_ + x)] += depth_inches;
            }
        }
        rebuild_active_sources();
        mark_water_summary_dirty();
    }

    void add_rain(float depth_inches)
    {
        for (float& water : water_depths_)
            water += depth_inches;
        rebuild_active_sources();
        mark_water_summary_dirty();
    }

    void evaporate(float depth_inches)
    {
        for (float& water : water_depths_)
            water = std::max(0.0f, water - depth_inches);
        rebuild_active_sources();
        mark_water_summary_dirty();
    }

    void mark_water_summary_dirty() noexcept
    {
        water_summary_dirty_ = true;
    }

    void clear_water_summary_cache()
    {
        water_summary_ = {};
        water_summary_dirty_ = false;
    }

    void ensure_water_summary_current()
    {
        if (!water_summary_dirty_)
            return;

        water_summary_ = {};
        for (float water : water_depths_)
        {
            if (water <= minimum_water_inches_)
                continue;

            ++water_summary_.wet_cell_count;
            water_summary_.total_water_inches += water;
            water_summary_.max_water_depth_inches =
                std::max(water_summary_.max_water_depth_inches, water);
        }

        water_summary_dirty_ = false;
    }

    int width_ = 0;
    int depth_ = 0;
    int cycle_count_ = 0;
    int selected_x_ = 0;
    int selected_z_ = 0;
    int add_radius_ = 11;
    int add_depth_inches_ = 22;
    static constexpr float minimum_water_inches_ = 0.001f;
    float max_flow_inches_ = 2.0f;
    float settle_rate_ = 0.5f;
    bool drain_edges_ = true;
    bool selected_cell_valid_ = false;

    std::vector<int> terrain_heights_;
    std::vector<float> water_depths_;
    std::vector<float> deltas_;
    std::vector<std::size_t> active_sources_;
    std::vector<std::size_t> drain_indices_;

    WaterSummary water_summary_;
    bool water_summary_dirty_ = true;
    double last_step_ms_ = 0.0;
    double total_step_ms_ = 0.0;
};

} // namespace grannys_house_trials::sim
