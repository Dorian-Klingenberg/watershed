#pragma once

// SimpleHydraulicErosionRainSim -- FastErosion PG07 baseline experiment.
//
// This keeps the CPU 11 workbench shell, visible rain drops, selected-cell
// water source, and erosion valley map, but the simulation core follows the
// Fast Hydraulic Erosion Simulation and Visualization on GPU loop:
//   1. water increment from rainfall or sources
//   2. four-neighbor shallow-water pipe flux
//   3. velocity reconstruction from flux
//   4. sediment-capacity erosion/deposition
//   5. semi-Lagrangian sediment transport
//   6. evaporation

#include "i_field_sim.h"

#include "../third_party/imgui/imgui.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <random>
#include <stdexcept>
#include <utility>
#include <vector>

namespace grannys_house_trials::sim
{

class SimpleHydraulicErosionRainSim final : public IFieldSim
{
public:
    [[nodiscard]] const char* name() const noexcept override
    {
        return "Hydraulic Erosion + Rainfall";
    }

    [[nodiscard]] SeedMapProfile seed_map_profile() const noexcept override
    {
        return SeedMapProfile::ErosionInclineValleys;
    }

    [[nodiscard]] int width() const noexcept override { return width_; }
    [[nodiscard]] int depth() const noexcept override { return depth_; }

    [[nodiscard]] float cell_size_feet() const noexcept override
    {
        return 1.0f;
    }

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
        return (terrain_heights_feet_[i] + water_depths_feet_[i]) *
            k_inches_per_foot;
    }

    [[nodiscard]] float water_depth_inches_at(int x, int z) const override
    {
        return water_depths_feet_[index_of(x, z)] * k_inches_per_foot;
    }

    [[nodiscard]] float velocity_magnitude_feet_per_second_at(int x, int z) const override
    {
        const std::size_t i = index_of(x, z);
        return std::sqrt(
            velocity_x_feet_per_second_[i] * velocity_x_feet_per_second_[i] +
            velocity_z_feet_per_second_[i] * velocity_z_feet_per_second_[i]);
    }

    [[nodiscard]] float suspended_sediment_inches_at(int x, int z) const override
    {
        return sediment_depths_feet_[index_of(x, z)] * k_inches_per_foot;
    }

    [[nodiscard]] float terrain_delta_inches_at(int x, int z) const override
    {
        return last_terrain_delta_feet_[index_of(x, z)] * k_inches_per_foot;
    }

    void append_visual_points(std::vector<FieldVisualPoint>& points) const override
    {
        points.reserve(points.size() + rain_drops_.size());
        for (const RainDrop& drop : rain_drops_)
        {
            FieldVisualPoint point;
            point.x_feet = drop.x_feet;
            point.y_feet = drop.y_feet;
            point.z_feet = drop.z_feet;
            point.radius_pixels = 2.8f;
            point.r = 0.25f;
            point.g = 0.62f;
            point.b = 1.0f;
            point.a = 0.88f;
            points.push_back(point);
        }
    }

    void reset(int new_width, int new_depth, std::vector<int> heights_inches) override
    {
        if (new_width <= 0 || new_depth <= 0)
            throw std::invalid_argument("SimpleHydraulicErosionRainSim: width and depth must be positive.");

        if (static_cast<int>(heights_inches.size()) != new_width * new_depth)
            throw std::invalid_argument("SimpleHydraulicErosionRainSim: seed size does not match dimensions.");

        width_ = new_width;
        depth_ = new_depth;

        terrain_heights_feet_.clear();
        terrain_heights_feet_.reserve(heights_inches.size());
        for (int height_inches : heights_inches)
            terrain_heights_feet_.push_back(
                static_cast<float>(height_inches) / k_inches_per_foot);

        const std::size_t cell_count = terrain_heights_feet_.size();
        water_depths_feet_.assign(cell_count, 0.0f);
        previous_water_depths_feet_.assign(cell_count, 0.0f);
        sediment_depths_feet_.assign(cell_count, 0.0f);
        sediment_after_erosion_feet_.assign(cell_count, 0.0f);
        sediment_transport_buffer_feet_.assign(cell_count, 0.0f);
        last_terrain_delta_feet_.assign(cell_count, 0.0f);
        flux_left_cfs_.assign(cell_count, 0.0f);
        flux_right_cfs_.assign(cell_count, 0.0f);
        flux_top_cfs_.assign(cell_count, 0.0f);
        flux_bottom_cfs_.assign(cell_count, 0.0f);
        velocity_x_feet_per_second_.assign(cell_count, 0.0f);
        velocity_z_feet_per_second_.assign(cell_count, 0.0f);
        rain_drops_.clear();

        rng_.seed(k_rng_seed);
        rain_drop_accumulator_ = 0.0f;
        cycle_count_ = 0;
        landed_rain_drop_count_ = 0;
        quiet_step_count_ = k_sleep_quiet_step_count;
        sleeping_ = true;
        last_max_water_delta_feet_ = 0.0f;
        last_max_terrain_delta_feet_ = 0.0f;
        last_max_velocity_feet_per_second_ = 0.0f;
        next_manual_phase_ = PaperPhase::WaterIncrement;
        last_completed_phase_ = PaperPhase::Evaporation;
        last_step_ms_ = 0.0;
        total_step_ms_ = 0.0;
        total_eroded_volume_cubic_feet_ = 0.0f;
        total_deposited_volume_cubic_feet_ = 0.0f;
        summary_dirty_ = true;
    }

    [[nodiscard]] bool step_once() override
    {
        if (terrain_heights_feet_.empty())
            return false;

        const auto start = std::chrono::steady_clock::now();

        bool field_changed = advance_rainfall();
        last_completed_phase_ = PaperPhase::WaterIncrement;
        const bool should_simulate_water =
            has_meaningful_water() || has_meaningful_flux() ||
            has_meaningful_sediment();

        if (should_simulate_water)
        {
            run_paper_simulation_step();
            update_sleep_state();
            next_manual_phase_ = PaperPhase::WaterIncrement;
            field_changed = true;
        }
        else if (!rain_is_active())
        {
            sleeping_ = true;
        }

        const bool did_work = field_changed || rain_is_active();
        if (!did_work)
            return false;

        ++cycle_count_;
        summary_dirty_ = true;

        const auto stop = std::chrono::steady_clock::now();
        last_step_ms_ =
            std::chrono::duration<double, std::milli>(stop - start).count();
        total_step_ms_ += last_step_ms_;
        return field_changed;
    }

    [[nodiscard]] bool render_ui() override
    {
        ensure_summary_current();

        ImGui::Text("Cycles: %d", cycle_count_);
        ImGui::Text("Wet cells: %d", summary_.wet_cell_count);
        ImGui::Text("Total water: %.3f ft^3", summary_.total_water_volume_cubic_feet);
        ImGui::Text("Max depth: %.2f in", summary_.max_water_depth_feet * k_inches_per_foot);
        ImGui::Text("Max velocity: %.2f ft/s", summary_.max_velocity_feet_per_second);
        ImGui::Text("Suspended sediment: %.4f ft^3", summary_.suspended_sediment_cubic_feet);
        ImGui::Text("Eroded / deposited: %.4f / %.4f ft^3",
                    total_eroded_volume_cubic_feet_,
                    total_deposited_volume_cubic_feet_);
        ImGui::Text("Active drops: %d", static_cast<int>(rain_drops_.size()));
        ImGui::Text("Landed drops: %d", landed_rain_drop_count_);
        ImGui::Text("State: %s", sleeping_ ? "Sleeping (steady)" : "Active");
        ImGui::Text("Last phase: %s", phase_name(last_completed_phase_));
        ImGui::Text("Next phase step: %s", phase_name(next_manual_phase_));
        ImGui::Text("Last terrain change: %.4f in/step",
                    last_max_terrain_delta_feet_ * k_inches_per_foot);
        ImGui::Text("Last water change: %.4f in/step",
                    last_max_water_delta_feet_ * k_inches_per_foot);
        ImGui::Text("Last step: %.3f ms", last_step_ms_);
        if (cycle_count_ > 0)
            ImGui::Text("Mean step: %.3f ms",
                        total_step_ms_ / static_cast<double>(cycle_count_));

        bool changed = false;

        ImGui::Separator();
        if (selected_cell_valid_)
            ImGui::TextDisabled("Water source target: selected [%d, %d]", selected_x_, selected_z_);
        else
            ImGui::TextDisabled("Water source target: field center.");

        if (ImGui::Button("Add Water at Target"))
        {
            const auto center = water_brush_center();
            add_water_disc(center.first, center.second, k_source_radius_cells,
                           source_depth_inches_);
            changed = true;
        }

        ImGui::SliderFloat("Source depth", &source_depth_inches_,
                           1.0f, 36.0f, "%.1f in");

        ImGui::Separator();
        ImGui::Checkbox("Simulate Rainfall", &rain_enabled_);
        ImGui::SliderFloat("Rain intensity", &rain_intensity_inches_per_hour_,
                           0.0f, 8.0f, "%.2f in/hr");
        if (ImGui::Button("Spawn 25 Falling Drops"))
            spawn_rain_burst(25);

        ImGui::SameLine();
        if (ImGui::Button("Clear Falling Drops"))
        {
            rain_drops_.clear();
            rain_drop_accumulator_ = 0.0f;
        }

        ImGui::SliderFloat("Evaporation", &evaporation_per_second_,
                           0.0f, 0.08f, "%.3f /s");
        ImGui::SliderFloat("Sediment capacity", &sediment_capacity_constant_,
                           0.001f, 0.120f, "%.3f");
        ImGui::SliderFloat("Dissolving", &dissolving_constant_,
                           0.001f, 0.200f, "%.3f");
        ImGui::SliderFloat("Deposition", &deposition_constant_,
                           0.001f, 0.200f, "%.3f");
        ImGui::SliderFloat("Max terrain step", &max_terrain_change_per_step_feet_,
                           0.0002f, 0.0100f, "%.4f ft");

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
                changed = step_once() || changed;
        }

        if (ImGui::Button("Step Next Phase"))
            changed = step_next_phase() || changed;

        if (changed)
            ensure_summary_current();

        ImGui::Separator();
        ImGui::TextDisabled("FastErosion PG07 baseline: four-neighbor pipe flux.");
        ImGui::TextDisabled("Loop: water, flux/velocity, erosion/deposition, sediment advection, evaporation.");
        ImGui::TextDisabled("Rain drops are visible source markers; erosion begins after water lands.");
        ImGui::TextDisabled("Stava force/dissolution/slippage are intentionally deferred.");
        return changed;
    }

private:
    enum class DirectionIndex : int
    {
        Left = 0,
        Right = 1,
        Top = 2,
        Bottom = 3,
    };

    enum class PaperPhase : int
    {
        WaterIncrement = 0,
        Flux,
        WaterVelocity,
        ErosionDeposition,
        SedimentTransport,
        Evaporation,
    };

    struct RainDrop
    {
        int x = 0;
        int z = 0;
        float x_feet = 0.0f;
        float y_feet = 0.0f;
        float z_feet = 0.0f;
        float volume_cubic_feet = 0.0f;
    };

    struct Summary
    {
        int wet_cell_count = 0;
        float total_water_volume_cubic_feet = 0.0f;
        float max_water_depth_feet = 0.0f;
        float suspended_sediment_cubic_feet = 0.0f;
        float max_velocity_feet_per_second = 0.0f;
    };

    static constexpr std::uint32_t k_rng_seed = 0x5EED003u;
    static constexpr float k_inches_per_foot = 12.0f;
    static constexpr float k_gravity_feet_per_second2 = 32.174f;
    static constexpr float k_time_step_seconds = 0.02f;
    static constexpr float k_pipe_area_square_feet = 0.12f;
    static constexpr int k_source_radius_cells = 6;
    static constexpr float k_rain_drop_depth_inches = 0.05f;
    static constexpr float k_rain_spawn_height_feet = 15.0f;
    static constexpr float k_rain_fall_speed_feet_per_second = 36.0f;
    static constexpr int k_max_active_rain_drops = 800;
    static constexpr int k_max_spawned_drops_per_step = 64;
    static constexpr float k_min_water_depth_feet = 0.0005f;
    static constexpr float k_velocity_water_depth_epsilon_feet = 0.001f;
    static constexpr float k_min_tilt_sine = 0.015f;
    static constexpr float k_min_terrain_height_feet = 0.25f;
    static constexpr float k_velocity_cap_feet_per_second = 30.0f;
    static constexpr float k_sleep_max_water_delta_inches = 0.001f;
    static constexpr float k_sleep_max_terrain_delta_inches = 0.0005f;
    static constexpr float k_sleep_max_velocity_feet_per_second = 0.01f;
    static constexpr int k_sleep_quiet_step_count = 240;

    [[nodiscard]] static const char* phase_name(PaperPhase phase) noexcept
    {
        switch (phase)
        {
        case PaperPhase::WaterIncrement: return "water increment";
        case PaperPhase::Flux: return "pipe flux";
        case PaperPhase::WaterVelocity: return "water + velocity";
        case PaperPhase::ErosionDeposition: return "erosion/deposition";
        case PaperPhase::SedimentTransport: return "sediment transport";
        case PaperPhase::Evaporation: return "evaporation";
        }

        return "unknown";
    }

    [[nodiscard]] std::pair<int, int> water_brush_center() const noexcept
    {
        if (selected_cell_valid_)
            return { selected_x_, selected_z_ };

        return { width_ / 2, depth_ / 2 };
    }

    [[nodiscard]] std::size_t index_of(int x, int z) const
    {
        if (x < 0 || x >= width_ || z < 0 || z >= depth_)
            throw std::out_of_range("SimpleHydraulicErosionRainSim: coordinates out of range.");
        return unchecked_index(x, z);
    }

    [[nodiscard]] std::size_t unchecked_index(int x, int z) const noexcept
    {
        return static_cast<std::size_t>(z * width_ + x);
    }

    [[nodiscard]] bool in_bounds(int x, int z) const noexcept
    {
        return x >= 0 && x < width_ && z >= 0 && z < depth_;
    }

    [[nodiscard]] float cell_area_square_feet() const noexcept
    {
        return cell_size_feet() * cell_size_feet();
    }

    [[nodiscard]] float surface_elevation_feet_at(std::size_t i) const noexcept
    {
        return terrain_heights_feet_[i] + water_depths_feet_[i];
    }

    [[nodiscard]] bool rain_is_active() const noexcept
    {
        return !rain_drops_.empty() ||
            (rain_enabled_ && rain_intensity_inches_per_hour_ > 0.0f);
    }

    [[nodiscard]] bool has_meaningful_water() const noexcept
    {
        for (float water_depth : water_depths_feet_)
        {
            if (water_depth > k_min_water_depth_feet)
                return true;
        }
        return false;
    }

    [[nodiscard]] bool has_meaningful_sediment() const noexcept
    {
        for (float sediment_depth : sediment_depths_feet_)
        {
            if (sediment_depth > 0.00001f)
                return true;
        }
        return false;
    }

    [[nodiscard]] bool has_meaningful_flux() const noexcept
    {
        for (std::size_t i = 0; i < flux_left_cfs_.size(); ++i)
        {
            if (std::abs(flux_left_cfs_[i]) > 0.0001f ||
                std::abs(flux_right_cfs_[i]) > 0.0001f ||
                std::abs(flux_top_cfs_[i]) > 0.0001f ||
                std::abs(flux_bottom_cfs_[i]) > 0.0001f)
            {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] float& flux_for(std::vector<float>& left,
                                  std::vector<float>& right,
                                  std::vector<float>& top,
                                  std::vector<float>& bottom,
                                  DirectionIndex direction,
                                  std::size_t i) noexcept
    {
        switch (direction)
        {
        case DirectionIndex::Left: return left[i];
        case DirectionIndex::Right: return right[i];
        case DirectionIndex::Top: return top[i];
        case DirectionIndex::Bottom: return bottom[i];
        }

        return left[i];
    }

    [[nodiscard]] float neighbor_surface_delta_feet(
        int x, int z, DirectionIndex direction) const noexcept
    {
        const std::size_t i = unchecked_index(x, z);
        int nx = x;
        int nz = z;
        switch (direction)
        {
        case DirectionIndex::Left: --nx; break;
        case DirectionIndex::Right: ++nx; break;
        case DirectionIndex::Top: --nz; break;
        case DirectionIndex::Bottom: ++nz; break;
        }

        if (!in_bounds(nx, nz))
            return 0.0f;

        return surface_elevation_feet_at(i) -
            surface_elevation_feet_at(unchecked_index(nx, nz));
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

    void run_paper_simulation_step()
    {
        previous_water_depths_feet_ = water_depths_feet_;

        compute_outflow_flux();
        last_completed_phase_ = PaperPhase::Flux;
        update_water_height_and_velocity();
        last_completed_phase_ = PaperPhase::WaterVelocity;
        compute_erosion_deposition();
        last_completed_phase_ = PaperPhase::ErosionDeposition;
        transport_sediment();
        last_completed_phase_ = PaperPhase::SedimentTransport;
        evaporate_water();
        last_completed_phase_ = PaperPhase::Evaporation;
        clear_tiny_water_and_flux();
    }

    [[nodiscard]] bool step_next_phase()
    {
        bool changed = false;

        switch (next_manual_phase_)
        {
        case PaperPhase::WaterIncrement:
            changed = advance_rainfall();
            last_completed_phase_ = PaperPhase::WaterIncrement;
            next_manual_phase_ = PaperPhase::Flux;
            break;

        case PaperPhase::Flux:
            previous_water_depths_feet_ = water_depths_feet_;
            compute_outflow_flux();
            last_completed_phase_ = PaperPhase::Flux;
            next_manual_phase_ = PaperPhase::WaterVelocity;
            changed = has_meaningful_flux();
            break;

        case PaperPhase::WaterVelocity:
            update_water_height_and_velocity();
            last_completed_phase_ = PaperPhase::WaterVelocity;
            next_manual_phase_ = PaperPhase::ErosionDeposition;
            changed = true;
            break;

        case PaperPhase::ErosionDeposition:
            compute_erosion_deposition();
            last_completed_phase_ = PaperPhase::ErosionDeposition;
            next_manual_phase_ = PaperPhase::SedimentTransport;
            changed = true;
            break;

        case PaperPhase::SedimentTransport:
            transport_sediment();
            last_completed_phase_ = PaperPhase::SedimentTransport;
            next_manual_phase_ = PaperPhase::Evaporation;
            changed = true;
            break;

        case PaperPhase::Evaporation:
            evaporate_water();
            clear_tiny_water_and_flux();
            update_sleep_state();
            last_completed_phase_ = PaperPhase::Evaporation;
            next_manual_phase_ = PaperPhase::WaterIncrement;
            ++cycle_count_;
            changed = true;
            break;
        }

        summary_dirty_ = true;
        return changed;
    }

    void compute_outflow_flux()
    {
        const float cell_area = cell_area_square_feet();
        for (int z = 0; z < depth_; ++z)
        {
            for (int x = 0; x < width_; ++x)
            {
                const std::size_t i = unchecked_index(x, z);

                auto update_flux =
                    [&](DirectionIndex direction)
                    {
                        float& flux = flux_for(
                            flux_left_cfs_, flux_right_cfs_,
                            flux_top_cfs_, flux_bottom_cfs_,
                            direction, i);
                        const float height_delta =
                            neighbor_surface_delta_feet(x, z, direction);
                        flux = std::max(
                            0.0f,
                            flux + k_time_step_seconds *
                                k_pipe_area_square_feet *
                                k_gravity_feet_per_second2 *
                                height_delta / cell_size_feet());
                    };

                if (x > 0) update_flux(DirectionIndex::Left);
                else flux_left_cfs_[i] = 0.0f;

                if (x + 1 < width_) update_flux(DirectionIndex::Right);
                else flux_right_cfs_[i] = 0.0f;

                if (z > 0) update_flux(DirectionIndex::Top);
                else flux_top_cfs_[i] = 0.0f;

                if (z + 1 < depth_) update_flux(DirectionIndex::Bottom);
                else flux_bottom_cfs_[i] = 0.0f;

                const float outflow =
                    flux_left_cfs_[i] + flux_right_cfs_[i] +
                    flux_top_cfs_[i] + flux_bottom_cfs_[i];
                const float available_volume =
                    water_depths_feet_[i] * cell_area;
                if (outflow * k_time_step_seconds > available_volume &&
                    outflow > 0.0f)
                {
                    const float scale =
                        available_volume / (outflow * k_time_step_seconds);
                    flux_left_cfs_[i] *= scale;
                    flux_right_cfs_[i] *= scale;
                    flux_top_cfs_[i] *= scale;
                    flux_bottom_cfs_[i] *= scale;
                }
            }
        }
    }

    void update_water_height_and_velocity()
    {
        const float cell_area = cell_area_square_feet();
        last_max_water_delta_feet_ = 0.0f;
        last_max_velocity_feet_per_second_ = 0.0f;

        for (int z = 0; z < depth_; ++z)
        {
            for (int x = 0; x < width_; ++x)
            {
                const std::size_t i = unchecked_index(x, z);
                const float inflow =
                    (x > 0 ? flux_right_cfs_[unchecked_index(x - 1, z)] : 0.0f) +
                    (x + 1 < width_ ? flux_left_cfs_[unchecked_index(x + 1, z)] : 0.0f) +
                    (z > 0 ? flux_bottom_cfs_[unchecked_index(x, z - 1)] : 0.0f) +
                    (z + 1 < depth_ ? flux_top_cfs_[unchecked_index(x, z + 1)] : 0.0f);
                const float outflow =
                    flux_left_cfs_[i] + flux_right_cfs_[i] +
                    flux_top_cfs_[i] + flux_bottom_cfs_[i];
                const float delta_volume =
                    k_time_step_seconds * (inflow - outflow);
                const float new_water_depth = std::max(
                    0.0f, water_depths_feet_[i] + delta_volume / cell_area);

                last_max_water_delta_feet_ =
                    std::max(last_max_water_delta_feet_,
                             std::abs(new_water_depth - water_depths_feet_[i]));
                water_depths_feet_[i] = new_water_depth;
            }
        }

        for (int z = 0; z < depth_; ++z)
        {
            for (int x = 0; x < width_; ++x)
            {
                const std::size_t i = unchecked_index(x, z);
                const float avg_water_depth =
                    0.5f * (previous_water_depths_feet_[i] + water_depths_feet_[i]);
                if (avg_water_depth <= k_velocity_water_depth_epsilon_feet)
                {
                    velocity_x_feet_per_second_[i] = 0.0f;
                    velocity_z_feet_per_second_[i] = 0.0f;
                    continue;
                }

                const float delta_wx =
                    0.5f * (
                        (x > 0 ? flux_right_cfs_[unchecked_index(x - 1, z)] : 0.0f) -
                        flux_left_cfs_[i] +
                        flux_right_cfs_[i] -
                        (x + 1 < width_ ? flux_left_cfs_[unchecked_index(x + 1, z)] : 0.0f));
                const float delta_wz =
                    0.5f * (
                        (z > 0 ? flux_bottom_cfs_[unchecked_index(x, z - 1)] : 0.0f) -
                        flux_top_cfs_[i] +
                        flux_bottom_cfs_[i] -
                        (z + 1 < depth_ ? flux_top_cfs_[unchecked_index(x, z + 1)] : 0.0f));

                velocity_x_feet_per_second_[i] = std::clamp(
                    delta_wx / (cell_size_feet() * avg_water_depth),
                    -k_velocity_cap_feet_per_second,
                    k_velocity_cap_feet_per_second);
                velocity_z_feet_per_second_[i] = std::clamp(
                    delta_wz / (cell_size_feet() * avg_water_depth),
                    -k_velocity_cap_feet_per_second,
                    k_velocity_cap_feet_per_second);

                const float speed = std::sqrt(
                    velocity_x_feet_per_second_[i] * velocity_x_feet_per_second_[i] +
                    velocity_z_feet_per_second_[i] * velocity_z_feet_per_second_[i]);
                last_max_velocity_feet_per_second_ =
                    std::max(last_max_velocity_feet_per_second_, speed);
            }
        }
    }

    [[nodiscard]] float terrain_height_sample_clamped(int x, int z) const noexcept
    {
        x = std::clamp(x, 0, width_ - 1);
        z = std::clamp(z, 0, depth_ - 1);
        return terrain_heights_feet_[unchecked_index(x, z)];
    }

    [[nodiscard]] float local_tilt_sine_at(int x, int z) const noexcept
    {
        const float left = terrain_height_sample_clamped(x - 1, z);
        const float right = terrain_height_sample_clamped(x + 1, z);
        const float top = terrain_height_sample_clamped(x, z - 1);
        const float bottom = terrain_height_sample_clamped(x, z + 1);
        const float dx = (right - left) / (2.0f * cell_size_feet());
        const float dz = (bottom - top) / (2.0f * cell_size_feet());
        const float slope = std::sqrt(dx * dx + dz * dz);
        return slope / std::sqrt(1.0f + slope * slope);
    }

    void compute_erosion_deposition()
    {
        sediment_after_erosion_feet_ = sediment_depths_feet_;
        std::fill(last_terrain_delta_feet_.begin(),
                  last_terrain_delta_feet_.end(), 0.0f);
        last_max_terrain_delta_feet_ = 0.0f;

        for (int z = 0; z < depth_; ++z)
        {
            for (int x = 0; x < width_; ++x)
            {
                const std::size_t i = unchecked_index(x, z);
                const float speed = std::sqrt(
                    velocity_x_feet_per_second_[i] * velocity_x_feet_per_second_[i] +
                    velocity_z_feet_per_second_[i] * velocity_z_feet_per_second_[i]);
                if (speed <= 0.0001f && sediment_after_erosion_feet_[i] <= 0.0f)
                    continue;

                const float tilt_sine =
                    speed > 0.0001f
                        ? std::max(local_tilt_sine_at(x, z), k_min_tilt_sine)
                        : 0.0f;
                const float capacity =
                    sediment_capacity_constant_ * tilt_sine * speed;
                float& sediment_depth = sediment_after_erosion_feet_[i];

                if (capacity > sediment_depth)
                {
                    const float desired_erosion =
                        (capacity - sediment_depth) * dissolving_constant_;
                    const float floor_limited_erosion = std::max(
                        0.0f,
                        terrain_heights_feet_[i] - k_min_terrain_height_feet);
                    const float eroded_height = std::min(
                        desired_erosion,
                        std::min(max_terrain_change_per_step_feet_,
                                 floor_limited_erosion));
                    if (eroded_height > 0.0f)
                    {
                        terrain_heights_feet_[i] -= eroded_height;
                        sediment_depth += eroded_height;
                        last_terrain_delta_feet_[i] -= eroded_height;
                        total_eroded_volume_cubic_feet_ +=
                            eroded_height * cell_area_square_feet();
                        last_max_terrain_delta_feet_ =
                            std::max(last_max_terrain_delta_feet_, eroded_height);
                    }
                }
                else
                {
                    const float deposited_height = std::min(
                        sediment_depth,
                        std::min(max_terrain_change_per_step_feet_,
                                 (sediment_depth - capacity) *
                                     deposition_constant_));
                    if (deposited_height > 0.0f)
                    {
                        terrain_heights_feet_[i] += deposited_height;
                        sediment_depth -= deposited_height;
                        last_terrain_delta_feet_[i] += deposited_height;
                        total_deposited_volume_cubic_feet_ +=
                            deposited_height * cell_area_square_feet();
                        last_max_terrain_delta_feet_ =
                            std::max(last_max_terrain_delta_feet_, deposited_height);
                    }
                }
            }
        }
    }

    [[nodiscard]] float sample_sediment_bilinear(float x, float z) const noexcept
    {
        x = std::clamp(x, 0.0f, static_cast<float>(std::max(0, width_ - 1)));
        z = std::clamp(z, 0.0f, static_cast<float>(std::max(0, depth_ - 1)));

        const int x0 = static_cast<int>(std::floor(x));
        const int z0 = static_cast<int>(std::floor(z));
        const int x1 = std::min(width_ - 1, x0 + 1);
        const int z1 = std::min(depth_ - 1, z0 + 1);
        const float tx = x - static_cast<float>(x0);
        const float tz = z - static_cast<float>(z0);

        const float s00 = sediment_after_erosion_feet_[unchecked_index(x0, z0)];
        const float s10 = sediment_after_erosion_feet_[unchecked_index(x1, z0)];
        const float s01 = sediment_after_erosion_feet_[unchecked_index(x0, z1)];
        const float s11 = sediment_after_erosion_feet_[unchecked_index(x1, z1)];
        const float sx0 = s00 + (s10 - s00) * tx;
        const float sx1 = s01 + (s11 - s01) * tx;
        return std::max(0.0f, sx0 + (sx1 - sx0) * tz);
    }

    void transport_sediment()
    {
        for (int z = 0; z < depth_; ++z)
        {
            for (int x = 0; x < width_; ++x)
            {
                const std::size_t i = unchecked_index(x, z);
                const float back_x =
                    static_cast<float>(x) -
                    velocity_x_feet_per_second_[i] *
                        k_time_step_seconds / cell_size_feet();
                const float back_z =
                    static_cast<float>(z) -
                    velocity_z_feet_per_second_[i] *
                        k_time_step_seconds / cell_size_feet();
                sediment_transport_buffer_feet_[i] =
                    sample_sediment_bilinear(back_x, back_z);
            }
        }

        sediment_depths_feet_.swap(sediment_transport_buffer_feet_);
    }

    void evaporate_water()
    {
        const float evaporation_factor =
            std::clamp(1.0f - evaporation_per_second_ * k_time_step_seconds,
                       0.0f, 1.0f);
        for (float& water_depth : water_depths_feet_)
            water_depth *= evaporation_factor;
    }

    void clear_tiny_water_and_flux()
    {
        for (std::size_t i = 0; i < water_depths_feet_.size(); ++i)
        {
            if (water_depths_feet_[i] > k_min_water_depth_feet)
                continue;

            water_depths_feet_[i] = 0.0f;
            flux_left_cfs_[i] = 0.0f;
            flux_right_cfs_[i] = 0.0f;
            flux_top_cfs_[i] = 0.0f;
            flux_bottom_cfs_[i] = 0.0f;
            velocity_x_feet_per_second_[i] = 0.0f;
            velocity_z_feet_per_second_[i] = 0.0f;
        }
    }

    void update_sleep_state()
    {
        const bool quiet =
            last_max_water_delta_feet_ * k_inches_per_foot <=
                k_sleep_max_water_delta_inches &&
            last_max_terrain_delta_feet_ * k_inches_per_foot <=
                k_sleep_max_terrain_delta_inches &&
            last_max_velocity_feet_per_second_ <=
                k_sleep_max_velocity_feet_per_second &&
            !rain_is_active();

        if (quiet)
            quiet_step_count_ = std::min(quiet_step_count_ + 1,
                                         k_sleep_quiet_step_count);
        else
            quiet_step_count_ = 0;

        if (quiet_step_count_ >= k_sleep_quiet_step_count)
        {
            std::fill(flux_left_cfs_.begin(), flux_left_cfs_.end(), 0.0f);
            std::fill(flux_right_cfs_.begin(), flux_right_cfs_.end(), 0.0f);
            std::fill(flux_top_cfs_.begin(), flux_top_cfs_.end(), 0.0f);
            std::fill(flux_bottom_cfs_.begin(), flux_bottom_cfs_.end(), 0.0f);
            std::fill(velocity_x_feet_per_second_.begin(),
                      velocity_x_feet_per_second_.end(), 0.0f);
            std::fill(velocity_z_feet_per_second_.begin(),
                      velocity_z_feet_per_second_.end(), 0.0f);
            last_max_velocity_feet_per_second_ = 0.0f;
            sleeping_ = true;
        }
        else
        {
            sleeping_ = false;
        }
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

    void spawn_rain_burst(int drop_count)
    {
        for (int i = 0;
             i < drop_count &&
                static_cast<int>(rain_drops_.size()) < k_max_active_rain_drops;
             ++i)
        {
            spawn_one_rain_drop();
        }
    }

    void spawn_one_rain_drop()
    {
        if (width_ <= 0 || depth_ <= 0)
            return;

        std::uniform_int_distribution<int> x_dist(0, width_ - 1);
        std::uniform_int_distribution<int> z_dist(0, depth_ - 1);
        std::uniform_real_distribution<float> jitter_dist(-0.35f, 0.35f);
        std::uniform_real_distribution<float> height_dist(0.0f, 4.0f);

        RainDrop drop;
        drop.x = x_dist(rng_);
        drop.z = z_dist(rng_);
        drop.x_feet =
            (static_cast<float>(drop.x) + 0.5f + jitter_dist(rng_)) *
            cell_size_feet();
        drop.z_feet =
            (static_cast<float>(drop.z) + 0.5f + jitter_dist(rng_)) *
            cell_size_feet();

        const std::size_t i = unchecked_index(drop.x, drop.z);
        drop.y_feet =
            surface_elevation_feet_at(i) +
            k_rain_spawn_height_feet + height_dist(rng_);
        drop.volume_cubic_feet =
            (k_rain_drop_depth_inches / k_inches_per_foot) *
            cell_area_square_feet();
        rain_drops_.push_back(drop);
    }

    [[nodiscard]] bool advance_rainfall()
    {
        if (rain_enabled_ && rain_intensity_inches_per_hour_ > 0.0f)
        {
            const float field_area_square_feet =
                static_cast<float>(width_ * depth_) * cell_area_square_feet();
            const float rain_depth_feet_per_second =
                (rain_intensity_inches_per_hour_ / k_inches_per_foot) / 3600.0f;
            const float rain_volume_this_step =
                rain_depth_feet_per_second * field_area_square_feet *
                k_time_step_seconds;
            const float drop_volume =
                (k_rain_drop_depth_inches / k_inches_per_foot) *
                cell_area_square_feet();
            rain_drop_accumulator_ += rain_volume_this_step / drop_volume;

            int spawned_this_step = 0;
            while (rain_drop_accumulator_ >= 1.0f &&
                   spawned_this_step < k_max_spawned_drops_per_step &&
                   static_cast<int>(rain_drops_.size()) < k_max_active_rain_drops)
            {
                spawn_one_rain_drop();
                rain_drop_accumulator_ -= 1.0f;
                ++spawned_this_step;
            }

            if (static_cast<int>(rain_drops_.size()) >= k_max_active_rain_drops)
                rain_drop_accumulator_ = std::min(rain_drop_accumulator_, 1.0f);
        }

        bool landed = false;
        for (std::size_t i = 0; i < rain_drops_.size();)
        {
            RainDrop& drop = rain_drops_[i];
            drop.y_feet -= k_rain_fall_speed_feet_per_second *
                k_time_step_seconds;
            const std::size_t cell_i = unchecked_index(drop.x, drop.z);
            const float target_y = surface_elevation_feet_at(cell_i);

            if (drop.y_feet <= target_y + 0.05f)
            {
                water_depths_feet_[cell_i] +=
                    drop.volume_cubic_feet / cell_area_square_feet();
                rain_drops_[i] = rain_drops_.back();
                rain_drops_.pop_back();
                ++landed_rain_drop_count_;
                wake_for_new_water();
                summary_dirty_ = true;
                landed = true;
                continue;
            }

            ++i;
        }

        return landed;
    }

    void ensure_summary_current()
    {
        if (!summary_dirty_)
            return;

        summary_ = {};
        const float cell_area = cell_area_square_feet();
        for (std::size_t i = 0; i < water_depths_feet_.size(); ++i)
        {
            const float water_depth_feet = water_depths_feet_[i];
            if (water_depth_feet > 0.0f)
                ++summary_.wet_cell_count;

            summary_.total_water_volume_cubic_feet += water_depth_feet * cell_area;
            summary_.max_water_depth_feet =
                std::max(summary_.max_water_depth_feet, water_depth_feet);
            summary_.suspended_sediment_cubic_feet +=
                sediment_depths_feet_[i] * cell_area;

            const float speed = std::sqrt(
                velocity_x_feet_per_second_[i] * velocity_x_feet_per_second_[i] +
                velocity_z_feet_per_second_[i] * velocity_z_feet_per_second_[i]);
            summary_.max_velocity_feet_per_second =
                std::max(summary_.max_velocity_feet_per_second, speed);
        }

        summary_dirty_ = false;
    }

    int width_ = 0;
    int depth_ = 0;
    int cycle_count_ = 0;
    int selected_x_ = 0;
    int selected_z_ = 0;
    int quiet_step_count_ = k_sleep_quiet_step_count;
    int landed_rain_drop_count_ = 0;
    bool selected_cell_valid_ = false;
    bool summary_dirty_ = true;
    bool sleeping_ = true;
    bool rain_enabled_ = false;
    float rain_intensity_inches_per_hour_ = 1.5f;
    float rain_drop_accumulator_ = 0.0f;
    float source_depth_inches_ = 18.0f;
    float evaporation_per_second_ = 0.012f;
    float sediment_capacity_constant_ = 0.035f;
    float dissolving_constant_ = 0.055f;
    float deposition_constant_ = 0.070f;
    float max_terrain_change_per_step_feet_ = 0.0015f;
    float last_max_water_delta_feet_ = 0.0f;
    float last_max_terrain_delta_feet_ = 0.0f;
    float last_max_velocity_feet_per_second_ = 0.0f;
    float total_eroded_volume_cubic_feet_ = 0.0f;
    float total_deposited_volume_cubic_feet_ = 0.0f;
    double last_step_ms_ = 0.0;
    double total_step_ms_ = 0.0;
    PaperPhase next_manual_phase_ = PaperPhase::WaterIncrement;
    PaperPhase last_completed_phase_ = PaperPhase::Evaporation;
    Summary summary_{};
    std::mt19937 rng_{ k_rng_seed };

    std::vector<float> terrain_heights_feet_;
    std::vector<float> water_depths_feet_;
    std::vector<float> previous_water_depths_feet_;
    std::vector<float> sediment_depths_feet_;
    std::vector<float> sediment_after_erosion_feet_;
    std::vector<float> sediment_transport_buffer_feet_;
    std::vector<float> last_terrain_delta_feet_;
    std::vector<float> flux_left_cfs_;
    std::vector<float> flux_right_cfs_;
    std::vector<float> flux_top_cfs_;
    std::vector<float> flux_bottom_cfs_;
    std::vector<float> velocity_x_feet_per_second_;
    std::vector<float> velocity_z_feet_per_second_;
    std::vector<RainDrop> rain_drops_;
};

} // namespace grannys_house_trials::sim
