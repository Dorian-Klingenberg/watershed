#pragma once

// SimpleSloshPipeFlowSim — terrain-aware 4-neighbor pipe flow with linear drag.
//
// Replaces Manning rough-bed friction (velocity-dependent, kills wave momentum)
// with a simple linear drag coefficient so water retains inertia across the
// basin and can overshoot equilibrium before damping back.
//
// Key differences from SimpleTerrainHeadPipeFlowSim (CPU 10/12 base):
//   - 4 cardinal neighbors only (no diagonals): cleaner wave fronts, 2x cheaper
//   - Linear drag: flux *= (1 - damping * dt) instead of Manning pow(h, 4/3)
//   - Larger dt (0.016 s): more accumulated momentum per rendered frame
//   - Damping exposed as a tunable slider in the UI

#include "i_field_sim.h"

#include "../third_party/imgui/imgui.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <vector>

namespace grannys_house_trials::sim
{

class SimpleSloshPipeFlowSim final : public IFieldSim
{
public:
    [[nodiscard]] const char* name() const noexcept override
    {
        return "Slosh Pipe Flow";
    }

    [[nodiscard]] int width()  const noexcept override { return width_; }
    [[nodiscard]] int depth()  const noexcept override { return depth_; }
    [[nodiscard]] float cell_size_feet() const noexcept override { return 1.0f; }

    void set_selected_cell(int x, int z, bool valid) noexcept override
    {
        selected_x_ = x;
        selected_z_ = z;
        selected_cell_valid_ = valid && x >= 0 && x < width_ && z >= 0 && z < depth_;
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
            throw std::invalid_argument(
                "SimpleSloshPipeFlowSim: width and depth must be positive.");
        if (static_cast<int>(heights_inches.size()) != new_width * new_depth)
            throw std::invalid_argument(
                "SimpleSloshPipeFlowSim: seed size does not match dimensions.");

        width_ = new_width;
        depth_ = new_depth;
        terrain_heights_ = std::move(heights_inches);

        const std::size_t n = static_cast<std::size_t>(width_ * depth_);
        water_depths_feet_.assign(n, 0.0f);
        // 4 pipes per cell: E(0), W(1), S(2), N(3). Opposite of d = d ^ 1.
        pipe_flow_rates_.assign(n * k_dir_count, 0.0f);
        proposed_out_.assign(n * k_dir_count, 0.0f);
        volume_deltas_.assign(n, 0.0f);

        cycle_count_    = 0;
        last_step_ms_   = 0.0;
        total_step_ms_  = 0.0;
        sleeping_       = true;
        quiet_step_count_ = k_sleep_quiet_step_count;
        last_max_flow_cfs_ = 0.0f;
    }

    [[nodiscard]] bool step_once() override
    {
        if (water_depths_feet_.empty() || sleeping_)
            return false;

        const auto t0 = std::chrono::steady_clock::now();

        update_flows();
        apply_transfers();
        tick_sleep();

        ++cycle_count_;

        const auto t1 = std::chrono::steady_clock::now();
        last_step_ms_   = std::chrono::duration<double, std::milli>(t1 - t0).count();
        total_step_ms_ += last_step_ms_;
        return true;
    }

    [[nodiscard]] bool render_ui() override
    {
        ImGui::Text("Cycles: %d",        cycle_count_);
        ImGui::Text("Last step: %.3f ms", last_step_ms_);
        if (cycle_count_ > 0)
            ImGui::Text("Mean step: %.3f ms",
                total_step_ms_ / static_cast<double>(cycle_count_));
        ImGui::Text("Max pipe flow: %.4f ft^3/s", last_max_flow_cfs_);
        ImGui::Text("State: %s",
            sleeping_ ? "Sleeping (steady)" : "Active");

        if (selected_cell_valid_)
            ImGui::TextDisabled("Water brush: [%d, %d]", selected_x_, selected_z_);
        else
            ImGui::TextDisabled("Water brush: field center.");

        bool changed = false;
        if (ImGui::Button("Add Water"))
        {
            const auto [cx, cz] = brush_center();
            add_water_disc(cx, cz, k_add_radius, k_add_depth_inches);
            changed = true;
        }

        if (ImGui::Button("Step (x1)"))
        {
            wake();
            changed = step_once() || changed;
        }
        ImGui::SameLine();
        if (ImGui::Button("Step (x25)"))
        {
            wake();
            for (int i = 0; i < 25; ++i)
            {
                changed = step_once() || changed;
                if (sleeping_) break;
            }
        }

        ImGui::Separator();
        ImGui::SliderFloat("Damping (1/s)", &damping_, 0.0f, 2.0f, "%.3f");
        ImGui::TextDisabled("4-neighbor cardinal pipe flow with linear drag.");
        ImGui::TextDisabled("dt = %.4f s  |  pipe area = %.3f ft^2", k_dt, k_pipe_area);
        return changed;
    }

private:
    // Cardinal pipe directions: E(0), W(1), S(2), N(3). Opposite of d = d ^ 1.
    static constexpr int k_dir_count = 4;
    static constexpr int k_dx[k_dir_count] = {  1, -1,  0, 0 };
    static constexpr int k_dz[k_dir_count] = {  0,  0,  1, -1 };

    static constexpr float k_inches_per_foot = 12.0f;
    static constexpr float k_gravity         = 32.174f;  // ft/s^2
    static constexpr float k_dt              = 0.016f;   // s per step
    // Stability limit: k_pipe_area * k_gravity * k_dt < min_depth_feet.
    // At 0.20 ft^2 this is stable down to ~1.5 in of water — thin enough to see wave structure.
    static constexpr float k_pipe_area       = 0.20f;    // ft^2
    static constexpr float k_cell_length     = 1.0f;     // ft (cell width)
    static constexpr int   k_add_radius      = 4;
    static constexpr float k_add_depth_inches = 8.0f;    // thin pulse, not a deep pool
    static constexpr int   k_sleep_quiet_step_count = 120;
    static constexpr float k_sleep_max_flow_cfs     = 0.002f;

    float damping_ = 0.06f;  // linear drag coefficient, 1/s — low so waves carry across basin

    [[nodiscard]] std::size_t index_of(int x, int z) const
    {
        if (x < 0 || x >= width_ || z < 0 || z >= depth_)
            throw std::out_of_range("SimpleSloshPipeFlowSim: coordinates out of range.");
        return static_cast<std::size_t>(z * width_ + x);
    }

    [[nodiscard]] std::size_t unchecked_index(int x, int z) const noexcept
    {
        return static_cast<std::size_t>(z * width_ + x);
    }

    [[nodiscard]] float terrain_feet_at(std::size_t i) const noexcept
    {
        return static_cast<float>(terrain_heights_[i]) / k_inches_per_foot;
    }

    [[nodiscard]] float free_surface_feet_at(std::size_t i) const noexcept
    {
        return terrain_feet_at(i) + water_depths_feet_[i];
    }

    // Water is able to leave source toward target if source has water AND
    // source free surface is above the target terrain bed.
    [[nodiscard]] bool can_flow(std::size_t source, std::size_t target) const noexcept
    {
        return water_depths_feet_[source] > 0.0f &&
            free_surface_feet_at(source) > terrain_feet_at(target);
    }

    [[nodiscard]] std::pair<int, int> brush_center() const noexcept
    {
        if (selected_cell_valid_)
            return { selected_x_, selected_z_ };
        return { width_ / 2, depth_ / 2 };
    }

    void wake() noexcept
    {
        sleeping_ = false;
        quiet_step_count_ = 0;
    }

    void update_flows()
    {
        std::fill(proposed_out_.begin(), proposed_out_.end(), 0.0f);
        last_max_flow_cfs_ = 0.0f;

        const float drag_retain = 1.0f - damping_ * k_dt;

        // Iterate over unique pairs: E(0) covers E-W pairs; S(2) covers S-N pairs.
        for (int z = 0; z < depth_; ++z)
        {
            for (int x = 0; x < width_; ++x)
            {
                const std::size_t src = unchecked_index(x, z);

                for (int d : { 0, 2 })  // E, S — unique half of each axis
                {
                    const int nx = x + k_dx[d];
                    const int nz = z + k_dz[d];
                    if (nx < 0 || nx >= width_ || nz < 0 || nz >= depth_)
                        continue;

                    const std::size_t nbr = unchecked_index(nx, nz);
                    const std::size_t fwd_i = src * k_dir_count + d;
                    const std::size_t rev_i = nbr * k_dir_count + (d ^ 1);

                    const float old_flow = pipe_flow_rates_[fwd_i];
                    const float head_delta =
                        free_surface_feet_at(src) - free_surface_feet_at(nbr);
                    const float gravity_impulse =
                        k_dt * k_pipe_area * k_gravity * head_delta / k_cell_length;

                    float new_flow = (old_flow + gravity_impulse) * drag_retain;

                    // Terrain wall gate: drain momentum when water can't cross.
                    if (new_flow > 0.0f && !can_flow(src, nbr))
                        new_flow = 0.0f;
                    else if (new_flow < 0.0f && !can_flow(nbr, src))
                        new_flow = 0.0f;

                    pipe_flow_rates_[fwd_i] =  new_flow;
                    pipe_flow_rates_[rev_i] = -new_flow;

                    last_max_flow_cfs_ =
                        std::max(last_max_flow_cfs_, std::abs(new_flow));

                    // Average flow over the step for transfer volume.
                    const float avg_flow  = 0.5f * (old_flow + new_flow);
                    const float transfer  = k_dt * avg_flow;
                    if (transfer > 0.0f && can_flow(src, nbr))
                        proposed_out_[fwd_i] = transfer;
                    else if (transfer < 0.0f && can_flow(nbr, src))
                        proposed_out_[rev_i] = -transfer;
                }
            }
        }
    }

    void apply_transfers()
    {
        std::fill(volume_deltas_.begin(), volume_deltas_.end(), 0.0f);

        const float cell_area = k_cell_length * k_cell_length;

        for (int z = 0; z < depth_; ++z)
        {
            for (int x = 0; x < width_; ++x)
            {
                const std::size_t src = unchecked_index(x, z);
                float total_out = 0.0f;

                for (int d = 0; d < k_dir_count; ++d)
                    total_out += proposed_out_[src * k_dir_count + d];

                if (total_out <= 0.0f)
                    continue;

                const float available = water_depths_feet_[src] * cell_area;
                const float scale = (total_out > available && available > 0.0f)
                    ? available / total_out
                    : 1.0f;

                for (int d = 0; d < k_dir_count; ++d)
                {
                    const float proposed = proposed_out_[src * k_dir_count + d];
                    if (proposed <= 0.0f)
                        continue;

                    const int nx = x + k_dx[d];
                    const int nz = z + k_dz[d];
                    if (nx < 0 || nx >= width_ || nz < 0 || nz >= depth_)
                        continue;

                    const float actual = proposed * scale;
                    if (scale < 1.0f)
                    {
                        proposed_out_[src * k_dir_count + d] = actual;
                        // Also scale the stored pipe flow to stay consistent.
                        const std::size_t fwd_i = src * k_dir_count + d;
                        const std::size_t nbr   = unchecked_index(nx, nz);
                        const std::size_t rev_i = nbr * k_dir_count + (d ^ 1);
                        pipe_flow_rates_[fwd_i] *= scale;
                        pipe_flow_rates_[rev_i] *= scale;
                    }

                    const std::size_t nbr = unchecked_index(nx, nz);
                    volume_deltas_[src] -= actual;
                    volume_deltas_[nbr] += actual;
                }
            }
        }

        const float inv_area = 1.0f / (k_cell_length * k_cell_length);
        for (int z = 0; z < depth_; ++z)
        {
            for (int x = 0; x < width_; ++x)
            {
                const std::size_t i = unchecked_index(x, z);
                const float new_vol =
                    std::max(0.0f, water_depths_feet_[i] * cell_area + volume_deltas_[i]);
                water_depths_feet_[i] = new_vol * inv_area;
            }
        }
    }

    void tick_sleep()
    {
        if (last_max_flow_cfs_ <= k_sleep_max_flow_cfs)
            quiet_step_count_ = std::min(quiet_step_count_ + 1, k_sleep_quiet_step_count);
        else
            quiet_step_count_ = 0;

        if (quiet_step_count_ >= k_sleep_quiet_step_count)
        {
            std::fill(pipe_flow_rates_.begin(), pipe_flow_rates_.end(), 0.0f);
            sleeping_ = true;
        }
    }

    void add_water_disc(int cx, int cz, int radius, float depth_inches)
    {
        wake();
        const int r2 = radius * radius;
        const float depth_feet = depth_inches / k_inches_per_foot;
        for (int z = std::max(0, cz - radius); z <= std::min(depth_ - 1, cz + radius); ++z)
        {
            for (int x = std::max(0, cx - radius); x <= std::min(width_ - 1, cx + radius); ++x)
            {
                const int dx = x - cx;
                const int dz = z - cz;
                if (dx * dx + dz * dz <= r2)
                    water_depths_feet_[unchecked_index(x, z)] += depth_feet;
            }
        }
    }

    int    width_  = 0;
    int    depth_  = 0;
    int    cycle_count_       = 0;
    int    quiet_step_count_  = k_sleep_quiet_step_count;
    int    selected_x_        = 0;
    int    selected_z_        = 0;
    bool   selected_cell_valid_ = false;
    bool   sleeping_          = true;
    float  last_max_flow_cfs_ = 0.0f;
    double last_step_ms_      = 0.0;
    double total_step_ms_     = 0.0;

    std::vector<int>   terrain_heights_;
    std::vector<float> water_depths_feet_;
    std::vector<float> pipe_flow_rates_;   // N * 4 floats
    std::vector<float> proposed_out_;      // N * 4 floats, reset each step
    std::vector<float> volume_deltas_;     // N floats
};

} // namespace grannys_house_trials::sim
