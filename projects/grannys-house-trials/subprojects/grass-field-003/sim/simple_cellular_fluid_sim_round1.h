#pragma once

// SimpleCellularFluidSimRound1 -- deterministic performance pass over the
// baseline cellular water experiment.
//
// The flow equation, row-major source order, four-neighbor visitation order,
// synchronous delta application, and drainage behavior match the baseline.
// This variant moves static topology work out of each tick and reuses scratch
// storage so the live experiment can move forward without losing its reference.

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

class SimpleCellularFluidSimRound1 final : public IFieldSim
{
public:
    [[nodiscard]] const char* name() const noexcept override
    {
        return "Cellular Water Flow (Optimized Round 1)";
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
            throw std::invalid_argument("SimpleCellularFluidSimRound1: width and depth must be positive.");

        if (static_cast<int>(heights_inches.size()) != new_width * new_depth)
            throw std::invalid_argument("SimpleCellularFluidSimRound1: seed heights size does not match width * depth.");

        width_ = new_width;
        depth_ = new_depth;
        cycle_count_ = 0;
        terrain_heights_ = std::move(heights_inches);
        water_depths_.assign(terrain_heights_.size(), 0.0f);
        deltas_.assign(terrain_heights_.size(), 0.0f);
        build_static_topology();
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

        std::size_t edge_topology_i = 0;
        const std::size_t row_width = static_cast<std::size_t>(width_);

        if (width_ <= 2 || depth_ <= 2)
        {
            for (std::size_t current_i = 0; current_i < water_depths_.size(); ++current_i)
                accumulate_source_flow(current_i, edge_topology_[edge_topology_i++]);
        }
        else
        {
            // Keep the baseline's row-major source order while allowing the
            // common interior path to avoid boundary tests and topology reads.
            for (int x = 0; x < width_; ++x)
                accumulate_source_flow(static_cast<std::size_t>(x), edge_topology_[edge_topology_i++]);

            for (int z = 1; z + 1 < depth_; ++z)
            {
                const std::size_t row_start = static_cast<std::size_t>(z) * row_width;
                accumulate_source_flow(row_start, edge_topology_[edge_topology_i++]);

                for (int x = 1; x + 1 < width_; ++x)
                {
                    const std::size_t current_i = row_start + static_cast<std::size_t>(x);
                    const CellTopology topology{{
                        current_i - 1,
                        current_i + 1,
                        current_i - row_width,
                        current_i + row_width
                    }, 4};
                    accumulate_source_flow(current_i, topology);
                }

                accumulate_source_flow(
                    row_start + row_width - 1,
                    edge_topology_[edge_topology_i++]);
            }

            const std::size_t last_row_start =
                static_cast<std::size_t>(depth_ - 1) * row_width;
            for (int x = 0; x < width_; ++x)
            {
                accumulate_source_flow(
                    last_row_start + static_cast<std::size_t>(x),
                    edge_topology_[edge_topology_i++]);
            }
        }

        for (std::size_t i = 0; i < water_depths_.size(); ++i)
            water_depths_[i] = std::max(0.0f, water_depths_[i] + deltas_[i]);

        if (drain_edges_)
        {
            for (std::size_t i : drain_indices_)
                water_depths_[i] = 0.0f;
        }

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
        ImGui::Text("Total water: %.1f in", water_summary_.total_water_inches);
        ImGui::Text("Max depth: %.2f in", water_summary_.max_water_depth_inches);
        ImGui::Text("Last step: %.3f ms", last_step_ms_);
        if (cycle_count_ > 0)
            ImGui::Text("Mean step: %.3f ms", total_step_ms_ / static_cast<double>(cycle_count_));
        ImGui::TextDisabled("Round 1: cached topology + reused scratch buffer.");

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
            evaporate(1.0f);
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

    struct CellTopology
    {
        std::array<std::size_t, 4> neighbors{};
        int neighbor_count = 0;
    };

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
            throw std::out_of_range("SimpleCellularFluidSimRound1: coordinates out of range.");

        return static_cast<std::size_t>(z * width_ + x);
    }

    void build_static_topology()
    {
        edge_topology_.clear();
        edge_topology_.reserve(static_cast<std::size_t>(2 * width_ + 2 * depth_));
        drain_indices_.clear();
        drain_indices_.reserve(static_cast<std::size_t>(2 * width_ + 2 * depth_));

        for (int z = 0; z < depth_; ++z)
        {
            for (int x = 0; x < width_; ++x)
            {
                if (x > 0 && x + 1 < width_ && z > 0 && z + 1 < depth_)
                    continue;

                const std::size_t i = static_cast<std::size_t>(z * width_ + x);
                CellTopology topology{};

                // Match the baseline order: left, right, up, down.
                if (x > 0)
                    topology.neighbors[static_cast<std::size_t>(topology.neighbor_count++)] = i - 1;
                if (x + 1 < width_)
                    topology.neighbors[static_cast<std::size_t>(topology.neighbor_count++)] = i + 1;
                if (z > 0)
                    topology.neighbors[static_cast<std::size_t>(topology.neighbor_count++)] =
                        i - static_cast<std::size_t>(width_);
                if (z + 1 < depth_)
                    topology.neighbors[static_cast<std::size_t>(topology.neighbor_count++)] =
                        i + static_cast<std::size_t>(width_);

                edge_topology_.push_back(topology);
            }
        }

        // Match the baseline drain write order, including harmless repeated corners.
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

    void accumulate_source_flow(std::size_t current_i, const CellTopology& topology)
    {
        const float available_water = water_depths_[current_i];
        if (available_water <= minimum_water_inches_)
            return;

        const float current_surface =
            static_cast<float>(terrain_heights_[current_i]) + available_water;
        std::array<LowerNeighbor, 4> lower_neighbors{};
        int lower_neighbor_count = 0;
        float surface_sum = current_surface;

        for (int n = 0; n < topology.neighbor_count; ++n)
        {
            const std::size_t neighbor_i = topology.neighbors[static_cast<std::size_t>(n)];
            const float neighbor_surface =
                static_cast<float>(terrain_heights_[neighbor_i]) + water_depths_[neighbor_i];
            if (neighbor_surface >= current_surface - minimum_water_inches_)
                continue;

            lower_neighbors[static_cast<std::size_t>(lower_neighbor_count++)] = {
                neighbor_i, neighbor_surface, 0.0f
            };
            surface_sum += neighbor_surface;
        }

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
        const int radius_sq = radius * radius;
        for (int z = std::max(0, center_z - radius); z <= std::min(depth_ - 1, center_z + radius); ++z)
        {
            for (int x = std::max(0, center_x - radius); x <= std::min(width_ - 1, center_x + radius); ++x)
            {
                const int dx = x - center_x;
                const int dz = z - center_z;
                if (dx * dx + dz * dz <= radius_sq)
                    water_depths_[static_cast<std::size_t>(z * width_ + x)] += depth_inches;
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
        const std::size_t sample_count =
            static_cast<std::size_t>(heatmap_samples_x_ * heatmap_samples_z_);
        heatmap_sample_max_.resize(sample_count);
        std::fill(heatmap_sample_max_.begin(), heatmap_sample_max_.end(), 0.0f);

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
    std::vector<float> deltas_;
    std::vector<CellTopology> edge_topology_;
    std::vector<std::size_t> drain_indices_;

    WaterSummary water_summary_;
    bool water_summary_dirty_ = true;
    int heatmap_samples_x_ = 0;
    int heatmap_samples_z_ = 0;
    std::vector<float> heatmap_sample_max_;

    double last_step_ms_ = 0.0;
    double total_step_ms_ = 0.0;
};

} // namespace grannys_house_trials::sim
