#pragma once

// SimpleTerrainHeadPipeFlowSim -- terrain-aware virtual pipe experiment.
//
// CPU 09 keeps the O'Brien/Hodgins volume equations isolated. This branch asks
// the game-terrain question: what if the same pipe network treats the terrain
// height as bed elevation, so free-surface height determines where water pools?

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

class SimpleTerrainHeadPipeFlowSim final : public IFieldSim
{
public:
    [[nodiscard]] const char* name() const noexcept override
    {
        return "Terrain-Head Pipe Flow";
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
        return static_cast<float>(terrain_heights_[i]) +
            water_depths_feet_[i] * k_inches_per_foot;
    }

    [[nodiscard]] float water_depth_inches_at(int x, int z) const override
    {
        return water_depths_feet_[index_of(x, z)] * k_inches_per_foot;
    }

    void reset(int new_width, int new_depth, std::vector<int> heights_inches) override
    {
        if (new_width <= 0 || new_depth <= 0)
            throw std::invalid_argument("SimpleTerrainHeadPipeFlowSim: width and depth must be positive.");

        if (static_cast<int>(heights_inches.size()) != new_width * new_depth)
            throw std::invalid_argument("SimpleTerrainHeadPipeFlowSim: seed size does not match dimensions.");

        width_ = new_width;
        depth_ = new_depth;
        terrain_heights_ = std::move(heights_inches);
        water_depths_feet_.assign(terrain_heights_.size(), 0.0f);
        pipe_flow_rates_.assign(terrain_heights_.size() * k_direction_count, 0.0f);
        proposed_volume_transfers_.assign(
            terrain_heights_.size() * k_direction_count, 0.0f);
        volume_deltas_.assign(terrain_heights_.size(), 0.0f);
        cycle_count_ = 0;
        last_step_ms_ = 0.0;
        total_step_ms_ = 0.0;
        last_max_depth_delta_feet_ = 0.0f;
        last_max_pipe_flow_cfs_ = 0.0f;
        quiet_step_count_ = k_sleep_quiet_step_count;
        sleeping_ = true;
        summary_dirty_ = true;
    }

    [[nodiscard]] bool step_once() override
    {
        if (water_depths_feet_.empty() || sleeping_)
            return false;

        const auto start = std::chrono::steady_clock::now();

        update_pipe_flow_rates_and_transfers();
        apply_volume_transfers_with_positivity();
        update_sleep_state();

        ++cycle_count_;
        summary_dirty_ = true;

        const auto stop = std::chrono::steady_clock::now();
        last_step_ms_ =
            std::chrono::duration<double, std::milli>(stop - start).count();
        total_step_ms_ += last_step_ms_;
        return true;
    }

    [[nodiscard]] bool render_ui() override
    {
        ensure_summary_current();

        ImGui::Text("Cycles: %d", cycle_count_);
        ImGui::Text("Wet cells: %d", summary_.wet_cell_count);
        ImGui::Text("Total volume: %.3f ft^3", summary_.total_volume_cubic_feet);
        ImGui::Text("Max depth: %.2f in", summary_.max_water_depth_feet * k_inches_per_foot);
        ImGui::Text("Max pipe flow: %.4f ft^3/s", summary_.max_pipe_flow_cfs);
        ImGui::Text("State: %s", sleeping_ ? "Sleeping (steady)" : "Active");
        ImGui::Text("Quiet steps: %d / %d", quiet_step_count_, k_sleep_quiet_step_count);
        ImGui::Text("Last max change: %.4f in/step",
                    last_max_depth_delta_feet_ * k_inches_per_foot);
        ImGui::Text("Last step: %.3f ms", last_step_ms_);
        if (cycle_count_ > 0)
            ImGui::Text("Mean step: %.3f ms", total_step_ms_ / static_cast<double>(cycle_count_));
        ImGui::TextDisabled("Terrain-aware branch: pressure uses water-surface elevation.");

        bool changed = false;
        if (selected_cell_valid_)
            ImGui::TextDisabled("Water brush target: selected [%d, %d]", selected_x_, selected_z_);
        else
            ImGui::TextDisabled("Water brush target: field center.");

        if (ImGui::Button("Add Water at Target"))
        {
            const auto center = water_brush_center();
            add_water_disc(center.first, center.second, k_add_radius,
                           k_add_depth_inches);
            changed = true;
        }

        if (ImGui::Button("Step (x1)"))
        {
            wake_for_manual_step();
            changed = step_once() || changed;
        }

        ImGui::SameLine();
        if (ImGui::Button("Step (x25)"))
        {
            wake_for_manual_step();
            for (int step = 0; step < 25; ++step)
            {
                changed = step_once() || changed;
                if (sleeping_)
                    break;
            }
        }

        if (changed)
            ensure_summary_current();

        ImGui::Separator();
        ImGui::TextDisabled("Head = terrain bed elevation + water depth.");
        ImGui::TextDisabled("Dry uphill neighbors stay disconnected until water reaches their bed.");
        ImGui::TextDisabled("Pipe energy loss uses Manning rough-bed friction.");
        ImGui::TextDisabled("No solver sliders; this branch isolates topographic pooling.");
        ImGui::TextDisabled("Boundary pipes are closed walls.");
        ImGui::TextDisabled("One simulation cell = one foot; flow uses cubic feet per second.");
        return changed;
    }

private:
    struct Direction
    {
        int dx = 0;
        int dz = 0;
        int opposite = 0;
        float length_feet = 1.0f;
    };

    struct VolumeSummary
    {
        int wet_cell_count = 0;
        float total_volume_cubic_feet = 0.0f;
        float max_water_depth_feet = 0.0f;
        float max_pipe_flow_cfs = 0.0f;
    };

    static constexpr int k_direction_count = 8;
    static constexpr float k_sqrt_two = 1.41421356237f;
    static constexpr float k_inches_per_foot = 12.0f;
    static constexpr int k_add_radius = 6;
    static constexpr float k_add_depth_inches = 22.0f;
    static constexpr float k_gravity_feet_per_second2 = 32.174f;
    static constexpr float k_time_step_seconds = 0.005f;
    static constexpr float k_pipe_area_square_feet = 0.05f;
    static constexpr float k_manning_roughness = 0.045f;
    static constexpr float k_sleep_max_depth_delta_inches = 0.001f;
    static constexpr float k_sleep_max_pipe_flow_cfs = 0.01f;
    static constexpr int k_sleep_quiet_step_count = 240;
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

    [[nodiscard]] std::pair<int, int> water_brush_center() const noexcept
    {
        if (selected_cell_valid_)
            return { selected_x_, selected_z_ };

        return { width_ / 2, depth_ / 2 };
    }

    [[nodiscard]] std::size_t index_of(int x, int z) const
    {
        if (x < 0 || x >= width_ || z < 0 || z >= depth_)
            throw std::out_of_range("SimpleTerrainHeadPipeFlowSim: coordinates out of range.");
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

    [[nodiscard]] float cell_area_square_feet() const noexcept
    {
        return cell_size_feet() * cell_size_feet();
    }

    [[nodiscard]] float terrain_height_feet_at(std::size_t i) const noexcept
    {
        return static_cast<float>(terrain_heights_[i]) / k_inches_per_foot;
    }

    [[nodiscard]] float water_volume_at(std::size_t i) const noexcept
    {
        return water_depths_feet_[i] * cell_area_square_feet();
    }

    [[nodiscard]] float water_surface_elevation_feet_at(std::size_t i) const noexcept
    {
        return terrain_height_feet_at(i) + water_depths_feet_[i];
    }

    [[nodiscard]] bool can_flow_from_to(std::size_t source_i, std::size_t target_i) const noexcept
    {
        return water_depths_feet_[source_i] > 0.0f &&
            water_surface_elevation_feet_at(source_i) > terrain_height_feet_at(target_i);
    }

    [[nodiscard]] float active_surface_head_delta_feet(std::size_t a, std::size_t b) const noexcept
    {
        const float delta =
            water_surface_elevation_feet_at(a) - water_surface_elevation_feet_at(b);

        if (delta > 0.0f && can_flow_from_to(a, b))
            return delta;
        if (delta < 0.0f && can_flow_from_to(b, a))
            return delta;
        return 0.0f;
    }

    [[nodiscard]] float edge_flow_depth_feet(
        std::size_t upstream_i, std::size_t downstream_i) const noexcept
    {
        const float edge_bed_elevation =
            std::max(terrain_height_feet_at(upstream_i),
                     terrain_height_feet_at(downstream_i));
        return std::max(
            0.0f, water_surface_elevation_feet_at(upstream_i) - edge_bed_elevation);
    }

    [[nodiscard]] float manning_drag_coefficient_per_cubic_foot(
        std::size_t source_i, std::size_t neighbor_i, float signed_flow_cfs) const noexcept
    {
        if (signed_flow_cfs == 0.0f)
            return 0.0f;

        const std::size_t upstream_i =
            signed_flow_cfs > 0.0f ? source_i : neighbor_i;
        const std::size_t downstream_i =
            signed_flow_cfs > 0.0f ? neighbor_i : source_i;
        const float hydraulic_radius_feet =
            edge_flow_depth_feet(upstream_i, downstream_i);
        if (hydraulic_radius_feet <= 0.0f)
            return 0.0f;

        const float radius_term =
            std::pow(hydraulic_radius_feet, 4.0f / 3.0f);
        return k_gravity_feet_per_second2 *
            k_manning_roughness * k_manning_roughness /
            (k_pipe_area_square_feet * radius_term);
    }

    [[nodiscard]] float apply_manning_rough_bed_loss(
        std::size_t source_i, std::size_t neighbor_i, float signed_flow_cfs) const noexcept
    {
        const float drag_coefficient =
            manning_drag_coefficient_per_cubic_foot(
                source_i, neighbor_i, signed_flow_cfs);
        return signed_flow_cfs /
            (1.0f + k_time_step_seconds * drag_coefficient *
                std::abs(signed_flow_cfs));
    }

    void wake_for_new_water() noexcept
    {
        sleeping_ = false;
        quiet_step_count_ = 0;
    }

    void wake_for_manual_step() noexcept
    {
        if (sleeping_)
            quiet_step_count_ = k_sleep_quiet_step_count - 1;
        else
            quiet_step_count_ = 0;
        sleeping_ = false;
    }

    void update_pipe_flow_rates_and_transfers()
    {
        std::fill(proposed_volume_transfers_.begin(),
                  proposed_volume_transfers_.end(), 0.0f);

        for (int z = 0; z < depth_; ++z)
        {
            for (int x = 0; x < width_; ++x)
            {
                const std::size_t source_i = unchecked_index(x, z);

                for (int direction_index : k_unique_direction_indices)
                {
                    const Direction direction =
                        k_directions[static_cast<std::size_t>(direction_index)];
                    const int nx = x + direction.dx;
                    const int nz = z + direction.dz;
                    if (!in_bounds(nx, nz))
                        continue;

                    const std::size_t neighbor_i = unchecked_index(nx, nz);
                    const std::size_t flow_i = pipe_index(source_i, direction_index);
                    const std::size_t opposite_flow_i =
                        pipe_index(neighbor_i, direction.opposite);

                    const float old_flow = pipe_flow_rates_[flow_i];
                    const float head_delta =
                        active_surface_head_delta_feet(source_i, neighbor_i);
                    const float acceleration =
                        k_gravity_feet_per_second2 * head_delta / direction.length_feet;
                    const float gravity_flow =
                        old_flow +
                        k_time_step_seconds * k_pipe_area_square_feet * acceleration;
                    float new_flow = gravity_flow;

                    if (new_flow > 0.0f && !can_flow_from_to(source_i, neighbor_i))
                        new_flow = 0.0f;
                    else if (new_flow < 0.0f && !can_flow_from_to(neighbor_i, source_i))
                        new_flow = 0.0f;
                    else
                        new_flow = apply_manning_rough_bed_loss(
                            source_i, neighbor_i, new_flow);

                    pipe_flow_rates_[flow_i] = new_flow;
                    pipe_flow_rates_[opposite_flow_i] = -new_flow;

                    const float average_flow = 0.5f * (old_flow + new_flow);
                    const float transfer_volume =
                        k_time_step_seconds * average_flow;
                    if (transfer_volume > 0.0f &&
                        can_flow_from_to(source_i, neighbor_i))
                    {
                        proposed_volume_transfers_[flow_i] = transfer_volume;
                    }
                    else if (transfer_volume < 0.0f &&
                             can_flow_from_to(neighbor_i, source_i))
                    {
                        proposed_volume_transfers_[opposite_flow_i] =
                            -transfer_volume;
                    }
                }
            }
        }
    }

    void apply_volume_transfers_with_positivity()
    {
        std::fill(volume_deltas_.begin(), volume_deltas_.end(), 0.0f);
        last_max_depth_delta_feet_ = 0.0f;

        for (int z = 0; z < depth_; ++z)
        {
            for (int x = 0; x < width_; ++x)
            {
                const std::size_t source_i = unchecked_index(x, z);
                float requested_outgoing_volume = 0.0f;

                for (int direction_index = 0; direction_index < k_direction_count; ++direction_index)
                {
                    requested_outgoing_volume +=
                        proposed_volume_transfers_[pipe_index(source_i, direction_index)];
                }

                if (requested_outgoing_volume <= 0.0f)
                    continue;

                const float available_volume = water_volume_at(source_i);
                float scale = 1.0f;
                if (requested_outgoing_volume > available_volume)
                {
                    scale = available_volume > 0.0f
                        ? available_volume / requested_outgoing_volume
                        : 0.0f;
                }

                for (int direction_index = 0; direction_index < k_direction_count; ++direction_index)
                {
                    const std::size_t transfer_i =
                        pipe_index(source_i, direction_index);
                    const float proposed = proposed_volume_transfers_[transfer_i];
                    if (proposed <= 0.0f)
                        continue;

                    const Direction direction =
                        k_directions[static_cast<std::size_t>(direction_index)];
                    const int nx = x + direction.dx;
                    const int nz = z + direction.dz;
                    if (!in_bounds(nx, nz))
                        continue;

                    const float actual = proposed * scale;
                    if (scale < 1.0f)
                    {
                        proposed_volume_transfers_[transfer_i] = actual;
                        scale_pipe_flow(source_i, unchecked_index(nx, nz),
                                        direction_index, scale);
                    }

                    if (actual <= 0.0f)
                        continue;

                    const std::size_t neighbor_i = unchecked_index(nx, nz);
                    volume_deltas_[source_i] -= actual;
                    volume_deltas_[neighbor_i] += actual;
                }
            }
        }

        const float cell_area = cell_area_square_feet();
        for (int z = 0; z < depth_; ++z)
        {
            for (int x = 0; x < width_; ++x)
            {
                const std::size_t i = unchecked_index(x, z);
                const float new_volume =
                    std::max(0.0f, water_volume_at(i) + volume_deltas_[i]);
                const float new_depth_feet = new_volume / cell_area;
                last_max_depth_delta_feet_ =
                    std::max(last_max_depth_delta_feet_,
                             std::abs(new_depth_feet - water_depths_feet_[i]));
                water_depths_feet_[i] = new_depth_feet;
            }
        }
    }

    [[nodiscard]] float max_pipe_flow_cfs() const noexcept
    {
        float max_flow = 0.0f;
        for (float flow : pipe_flow_rates_)
            max_flow = std::max(max_flow, std::abs(flow));
        return max_flow;
    }

    void update_sleep_state()
    {
        last_max_pipe_flow_cfs_ = max_pipe_flow_cfs();
        const bool quiet =
            last_max_depth_delta_feet_ * k_inches_per_foot <=
                k_sleep_max_depth_delta_inches &&
            last_max_pipe_flow_cfs_ <= k_sleep_max_pipe_flow_cfs;

        if (quiet)
            quiet_step_count_ = std::min(quiet_step_count_ + 1,
                                         k_sleep_quiet_step_count);
        else
            quiet_step_count_ = 0;

        if (quiet_step_count_ >= k_sleep_quiet_step_count)
        {
            std::fill(pipe_flow_rates_.begin(), pipe_flow_rates_.end(), 0.0f);
            last_max_pipe_flow_cfs_ = 0.0f;
            sleeping_ = true;
        }
    }

    void scale_pipe_flow(std::size_t source_i, std::size_t neighbor_i,
                         int direction_index, float scale)
    {
        const int opposite =
            k_directions[static_cast<std::size_t>(direction_index)].opposite;
        pipe_flow_rates_[pipe_index(source_i, direction_index)] *= scale;
        pipe_flow_rates_[pipe_index(neighbor_i, opposite)] *= scale;
    }

    void add_water_disc(int center_x, int center_z, int radius, float depth_inches)
    {
        wake_for_new_water();
        const int radius_squared = radius * radius;
        const float depth_feet = depth_inches / k_inches_per_foot;
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
                    water_depths_feet_[unchecked_index(x, z)] += depth_feet;
            }
        }
        summary_dirty_ = true;
    }

    void ensure_summary_current()
    {
        if (!summary_dirty_)
            return;

        summary_ = {};
        const float cell_area = cell_area_square_feet();
        for (float water_depth_feet : water_depths_feet_)
        {
            if (water_depth_feet > 0.0f)
                ++summary_.wet_cell_count;

            summary_.total_volume_cubic_feet += water_depth_feet * cell_area;
            summary_.max_water_depth_feet =
                std::max(summary_.max_water_depth_feet, water_depth_feet);
        }

        for (float flow : pipe_flow_rates_)
        {
            const float abs_flow = std::abs(flow);
            summary_.max_pipe_flow_cfs =
                std::max(summary_.max_pipe_flow_cfs, abs_flow);
        }

        summary_dirty_ = false;
    }

    int width_ = 0;
    int depth_ = 0;
    int cycle_count_ = 0;
    int selected_x_ = 0;
    int selected_z_ = 0;
    int quiet_step_count_ = k_sleep_quiet_step_count;
    bool selected_cell_valid_ = false;
    bool summary_dirty_ = true;
    bool sleeping_ = true;
    float last_max_depth_delta_feet_ = 0.0f;
    float last_max_pipe_flow_cfs_ = 0.0f;
    double last_step_ms_ = 0.0;
    double total_step_ms_ = 0.0;
    VolumeSummary summary_{};

    std::vector<int> terrain_heights_;
    std::vector<float> water_depths_feet_;
    std::vector<float> pipe_flow_rates_;
    std::vector<float> proposed_volume_transfers_;
    std::vector<float> volume_deltas_;
};

} // namespace grannys_house_trials::sim
