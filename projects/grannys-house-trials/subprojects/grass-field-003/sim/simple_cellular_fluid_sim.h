#pragma once

// SimpleCellularFluidSim -- first fluid experiment for grass-field-003.
//
// This intentionally remains a small heightfield cellular model: terrain is
// fixed, water is a per-cell depth, and each tick transfers water to lower
// neighbors. Water uses fractional inches so a liquid surface can genuinely
// level instead of settling into an integer angle-of-repose cone.
// It is retained as the behavioral baseline for optimization experiments.

#include "i_field_sim.h"

#include "../third_party/imgui/imgui.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace grannys_house_trials::sim
{

class SimpleCellularFluidSim final : public IFieldSim
{
public:
    SimpleCellularFluidSim() = default;

    [[nodiscard]] const char* name() const noexcept override
    {
        return "Cellular Water Flow (Baseline)";
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
            throw std::invalid_argument("SimpleCellularFluidSim: width and depth must be positive.");

        if (static_cast<int>(heights_inches.size()) != new_width * new_depth)
            throw std::invalid_argument("SimpleCellularFluidSim: seed heights size does not match width * depth.");

        width_ = new_width;
        depth_ = new_depth;
        cycle_count_ = 0;
        terrain_heights_ = std::move(heights_inches);
        water_depths_.assign(terrain_heights_.size(), 0.0f);
        clear_water_summary_cache();
    }

    void add_center_water(int radius, float depth_inches)
    {
        add_water_disc(width_ / 2, depth_ / 2, radius, depth_inches);
    }

    void add_uniform_rain(float depth_inches)
    {
        add_rain(depth_inches);
    }

    [[nodiscard]] bool render_ui() override
    {
        ensure_water_summary_current();

        ImGui::Text("Cycles: %d", cycle_count_);
        ImGui::Text("Wet cells: %d", water_summary_.wet_cell_count);
        ImGui::Text("Total water: %.1f in", water_summary_.total_water_inches);
        ImGui::Text("Max depth: %.2f in", water_summary_.max_water_depth_inches);

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
            for (int i = 0; i < 50; ++i)
                changed = step_once() || changed;
        }

        ImGui::SameLine();

        if (ImGui::Button("Evaporate 1 in"))
        {
            evaporate(1);
            changed = true;
        }

        if (changed)
            ensure_water_summary_current();

        ImGui::Separator();
        render_water_heatmap();
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

    [[nodiscard]] std::size_t index_of(int x, int z) const
    {
        if (x < 0 || x >= width_ || z < 0 || z >= depth_)
            throw std::out_of_range("SimpleCellularFluidSim: coordinates out of range.");

        return static_cast<std::size_t>(z * width_ + x);
    }

    [[nodiscard]] float surface_height_at(int x, int z) const
    {
        const std::size_t i = index_of(x, z);
        return static_cast<float>(terrain_heights_[i]) + water_depths_[i];
    }

public:
    [[nodiscard]] bool step_once() override
    {
        if (water_depths_.empty())
            return false;

        std::vector<float> deltas(water_depths_.size(), 0.0f);

        constexpr std::array<std::pair<int, int>, 4> neighbor_offsets{{
            {-1,  0},
            { 1,  0},
            { 0, -1},
            { 0,  1},
        }};

        struct LowerNeighbor
        {
            std::size_t index = 0;
            float surface = 0.0f;
            float desired_inflow = 0.0f;
        };

        for (int z = 0; z < depth_; ++z)
        {
            for (int x = 0; x < width_; ++x)
            {
                const std::size_t current_i = index_of(x, z);
                const float available_water = water_depths_[current_i];
                if (available_water <= minimum_water_inches_)
                    continue;

                const float current_surface =
                    static_cast<float>(terrain_heights_[current_i]) + available_water;
                std::array<LowerNeighbor, 4> lower_neighbors{};
                int lower_neighbor_count = 0;
                float surface_sum = current_surface;

                for (auto [dx, dz] : neighbor_offsets)
                {
                    const int nx = x + dx;
                    const int nz = z + dz;
                    if (nx < 0 || nx >= width_ || nz < 0 || nz >= depth_)
                        continue;

                    const float neighbor_surface = surface_height_at(nx, nz);
                    if (neighbor_surface >= current_surface - minimum_water_inches_)
                        continue;

                    lower_neighbors[lower_neighbor_count++] = {
                        index_of(nx, nz), neighbor_surface, 0.0f
                    };
                    surface_sum += neighbor_surface;
                }

                if (lower_neighbor_count == 0)
                    continue;

                // Equalize the source and lower neighbors toward their shared
                // surface, then relax only a configurable portion this tick.
                // This preserves mass and avoids the one-neighbor edge streaks.
                const float shared_surface =
                    surface_sum / static_cast<float>(lower_neighbor_count + 1);
                float desired_outflow = 0.0f;
                for (int i = 0; i < lower_neighbor_count; ++i)
                {
                    LowerNeighbor& neighbor = lower_neighbors[i];
                    neighbor.desired_inflow =
                        std::max(0.0f, shared_surface - neighbor.surface);
                    desired_outflow += neighbor.desired_inflow;
                }

                if (desired_outflow <= minimum_water_inches_)
                    continue;

                const float total_flow = std::min({
                    available_water,
                    max_flow_inches_,
                    desired_outflow * settle_rate_
                });
                if (total_flow <= minimum_water_inches_)
                    continue;

                const float flow_scale = total_flow / desired_outflow;
                deltas[current_i] -= total_flow;
                for (int i = 0; i < lower_neighbor_count; ++i)
                {
                    const LowerNeighbor& neighbor = lower_neighbors[i];
                    deltas[neighbor.index] += neighbor.desired_inflow * flow_scale;
                }
            }
        }

        for (std::size_t i = 0; i < water_depths_.size(); ++i)
            water_depths_[i] = std::max(0.0f, water_depths_[i] + deltas[i]);

        if (drain_edges_)
            drain_boundary();

        ++cycle_count_;
        mark_water_summary_dirty();
        return true;
    }

private:
    void add_water_disc(int center_x, int center_z, int radius, float depth_inches)
    {
        const int radius_sq = radius * radius;
        for (int z = std::max(0, center_z - radius); z <= std::min(depth_ - 1, center_z + radius); ++z)
        {
            for (int x = std::max(0, center_x - radius); x <= std::min(width_ - 1, center_x + radius); ++x)
            {
                const int dx = x - center_x;
                const int dz = z - center_z;
                if (dx * dx + dz * dz <= radius_sq)
                    water_depths_[index_of(x, z)] += depth_inches;
            }
        }

        mark_water_summary_dirty();
    }

    void add_rain(float depth_inches)
    {
        for (float& water : water_depths_)
            water += depth_inches;

        mark_water_summary_dirty();
    }

    void evaporate(float depth_inches)
    {
        for (float& water : water_depths_)
            water = std::max(0.0f, water - depth_inches);

        mark_water_summary_dirty();
    }

    void drain_boundary()
    {
        if (width_ <= 0 || depth_ <= 0)
            return;

        for (int x = 0; x < width_; ++x)
        {
            water_depths_[index_of(x, 0)] = 0.0f;
            water_depths_[index_of(x, depth_ - 1)] = 0.0f;
        }

        for (int z = 0; z < depth_; ++z)
        {
            water_depths_[index_of(0, z)] = 0.0f;
            water_depths_[index_of(width_ - 1, z)] = 0.0f;
        }
    }

    struct WaterSummary
    {
        int wet_cell_count = 0;
        float total_water_inches = 0.0f;
        float max_water_depth_inches = 0.0f;
    };

    void mark_water_summary_dirty() noexcept
    {
        water_summary_dirty_ = true;
    }

    void clear_water_summary_cache()
    {
        water_summary_ = {};
        water_summary_dirty_ = false;
        heatmap_samples_x_ = 0;
        heatmap_samples_z_ = 0;
        heatmap_sample_max_.clear();
    }

    void ensure_water_summary_current()
    {
        if (!water_summary_dirty_)
            return;

        water_summary_ = {};
        heatmap_samples_x_ = (width_ > 0) ? std::min(width_, 48) : 0;
        heatmap_samples_z_ = (depth_ > 0) ? std::min(depth_, 48) : 0;
        heatmap_sample_max_.assign(
            static_cast<std::size_t>(heatmap_samples_x_ * heatmap_samples_z_), 0.0f);

        if (width_ <= 0 || depth_ <= 0 || water_depths_.empty())
        {
            water_summary_dirty_ = false;
            return;
        }

        for (int z = 0; z < depth_; ++z)
        {
            for (int x = 0; x < width_; ++x)
            {
                const std::size_t i = static_cast<std::size_t>(z * width_ + x);
                const float water = water_depths_[i];

                if (water <= minimum_water_inches_)
                    continue;

                ++water_summary_.wet_cell_count;
                water_summary_.total_water_inches += water;
                water_summary_.max_water_depth_inches =
                    std::max(water_summary_.max_water_depth_inches, water);

                if (heatmap_samples_x_ > 0 && heatmap_samples_z_ > 0)
                {
                    const int sample_x = x * heatmap_samples_x_ / width_;
                    const int sample_z = z * heatmap_samples_z_ / depth_;
                    const std::size_t sample_i =
                        static_cast<std::size_t>(sample_z * heatmap_samples_x_ + sample_x);
                    heatmap_sample_max_[sample_i] =
                        std::max(heatmap_sample_max_[sample_i], water);
                }
            }
        }

        water_summary_dirty_ = false;
    }

    void render_water_heatmap() const
    {
        constexpr float heatmap_size = 160.0f;
        ImGui::Text("Water depth map");

        const ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
        const ImVec2 canvas_size(heatmap_size, heatmap_size);
        ImDrawList* draw_list = ImGui::GetWindowDrawList();

        ImGui::InvisibleButton("##water_depth_heatmap", canvas_size);
        draw_list->AddRectFilled(
            canvas_pos,
            ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y),
            IM_COL32(24, 32, 34, 255));

        if (heatmap_samples_x_ <= 0 || heatmap_samples_z_ <= 0 ||
            heatmap_sample_max_.empty() || water_summary_.wet_cell_count <= 0)
        {
            draw_list->AddRect(
                canvas_pos,
                ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y),
                IM_COL32(100, 112, 116, 255));
            return;
        }

        const float max_depth = std::max(0.001f, water_summary_.max_water_depth_inches);
        const float cell_w = canvas_size.x / static_cast<float>(heatmap_samples_x_);
        const float cell_h = canvas_size.y / static_cast<float>(heatmap_samples_z_);

        for (int sz = 0; sz < heatmap_samples_z_; ++sz)
        {
            for (int sx = 0; sx < heatmap_samples_x_; ++sx)
            {
                const std::size_t sample_i =
                    static_cast<std::size_t>(sz * heatmap_samples_x_ + sx);
                const float sample_max = heatmap_sample_max_[sample_i];

                if (sample_max <= minimum_water_inches_)
                    continue;

                const float t = std::min(1.0f, sample_max / max_depth);
                const int r = static_cast<int>(30.0f + 20.0f * t);
                const int g = static_cast<int>(96.0f + 90.0f * t);
                const int b = static_cast<int>(150.0f + 95.0f * t);
                const int a = static_cast<int>(80.0f + 175.0f * t);

                const ImVec2 p0(canvas_pos.x + sx * cell_w, canvas_pos.y + sz * cell_h);
                const ImVec2 p1(canvas_pos.x + (sx + 1) * cell_w + 0.5f,
                                canvas_pos.y + (sz + 1) * cell_h + 0.5f);
                draw_list->AddRectFilled(p0, p1, IM_COL32(r, g, b, a));
            }
        }

        draw_list->AddRect(
            canvas_pos,
            ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y),
            IM_COL32(100, 112, 116, 255));
    }

    int width_ = 0;
    int depth_ = 0;
    int cycle_count_ = 0;
    int selected_x_ = 0;
    int selected_z_ = 0;

    int add_radius_ = 6;
    int add_depth_inches_ = 12;
    static constexpr float minimum_water_inches_ = 0.001f;

    float max_flow_inches_ = 2.0f;
    float settle_rate_ = 0.5f;
    bool drain_edges_ = true;
    bool selected_cell_valid_ = false;

    std::vector<int> terrain_heights_;
    std::vector<float> water_depths_;

    WaterSummary water_summary_;
    bool water_summary_dirty_ = true;
    int heatmap_samples_x_ = 0;
    int heatmap_samples_z_ = 0;
    std::vector<float> heatmap_sample_max_;
};

} // namespace grannys_house_trials::sim
