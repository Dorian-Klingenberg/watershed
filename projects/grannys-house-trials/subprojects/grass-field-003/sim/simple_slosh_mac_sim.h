#pragma once

// SimpleSloshMacSim — staggered MAC-grid shallow water solver.
//
// Stores velocity components on cell faces rather than deriving them fresh each
// step from a head gradient. This gives velocity genuine inertia: it accumulates
// from the pressure impulse and persists across steps, so a wave front carries
// momentum across the basin before linear drag dissipates it.
//
// Equations (depth-averaged linearised SWE, no advection):
//   ∂u/∂t = -g ∂η/∂x  -  α·u
//   ∂v/∂t = -g ∂η/∂z  -  α·v
//   ∂h/∂t = -∂(hu)/∂x  -  ∂(hv)/∂z   (first-order upwind fluxes)
//
// where η = terrain + h is the free surface and α is the linear drag coefficient.
//
// Wave speed: c = √(g·H) — physically correct, independent of any pipe-area knob.
//
// Face layout (W×D cell grid):
//   u_[ z*(W+1) + x ]  x∈[0,W]  — east-face velocity between cell (x-1,z) and (x,z)
//   v_[ z*W    + x ]   z∈[0,D]  — north-face velocity between cell (x,z-1) and (x,z)
//
// Solid cells: terrain >= k_solid_in are treated as rigid walls.
// Active-face masks are precomputed at reset() so the hot loop skips solid tests.

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

class SimpleSloshMacSim final : public IFieldSim
{
public:
    [[nodiscard]] const char* name() const noexcept override { return "Slosh MAC Grid"; }

    [[nodiscard]] SeedMapProfile seed_map_profile() const noexcept override
    {
        return SeedMapProfile::SloshBasin;
    }

    [[nodiscard]] int   width()          const noexcept override { return width_;  }
    [[nodiscard]] int   depth()          const noexcept override { return depth_;  }
    [[nodiscard]] float cell_size_feet() const noexcept override { return 1.0f;    }

    void set_selected_cell(int x, int z, bool valid) noexcept override
    {
        sel_x_ = x;
        sel_z_ = z;
        sel_valid_ = valid && x >= 0 && x < width_ && z >= 0 && z < depth_;
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
        const std::size_t i = ci(x, z);
        return static_cast<float>(terrain_[i]) + h_[i] * k_in_per_ft;
    }

    [[nodiscard]] float water_depth_inches_at(int x, int z) const override
    {
        return h_[ci(x, z)] * k_in_per_ft;
    }

    void reset(int w, int d, std::vector<int> terrain_inches) override
    {
        if (w <= 0 || d <= 0)
            throw std::invalid_argument("SimpleSloshMacSim: dimensions must be positive.");
        if (static_cast<int>(terrain_inches.size()) != w * d)
            throw std::invalid_argument("SimpleSloshMacSim: terrain size mismatch.");

        width_  = w;
        depth_  = d;
        terrain_ = std::move(terrain_inches);

        const auto nc = static_cast<std::size_t>(w * d);
        const auto nu = static_cast<std::size_t>((w + 1) * d);
        const auto nv = static_cast<std::size_t>(w * (d + 1));

        h_.assign(nc, 0.0f);
        u_.assign(nu, 0.0f);
        v_.assign(nv, 0.0f);
        h_new_.assign(nc, 0.0f);
        u_active_.assign(nu, false);
        v_active_.assign(nv, false);

        // Precompute which faces connect two non-solid cells.
        // Inactive faces are permanently zero — no need to check solid status
        // inside the hot simulation loop.
        for (int z = 0; z < d; ++z)
        {
            for (int x = 0; x <= w; ++x)
            {
                const bool left_ok  = x > 0 && !is_solid(x - 1, z);
                const bool right_ok = x < w && !is_solid(x,     z);
                u_active_[ui(x, z)] = left_ok && right_ok;
            }
        }
        for (int z = 0; z <= d; ++z)
        {
            for (int x = 0; x < w; ++x)
            {
                const bool bot_ok = z > 0 && !is_solid(x, z - 1);
                const bool top_ok = z < d && !is_solid(x, z    );
                v_active_[vi(x, z)] = bot_ok && top_ok;
            }
        }

        cycle_count_    = 0;
        last_step_ms_   = 0.0;
        total_step_ms_  = 0.0;
        sleeping_       = true;
        quiet_steps_    = k_sleep_steps;
        last_max_speed_ = 0.0f;
    }

    [[nodiscard]] bool step_once() override
    {
        if (h_.empty() || sleeping_) return false;

        const auto t0 = std::chrono::steady_clock::now();

        apply_gravity();
        apply_drag();
        update_depth();
        tick_sleep();

        ++cycle_count_;

        const auto t1 = std::chrono::steady_clock::now();
        last_step_ms_  = std::chrono::duration<double, std::milli>(t1 - t0).count();
        total_step_ms_ += last_step_ms_;
        return true;
    }

    [[nodiscard]] bool render_ui() override
    {
        ImGui::Text("Cycles: %d",         cycle_count_);
        ImGui::Text("Last step: %.3f ms", last_step_ms_);
        if (cycle_count_ > 0)
            ImGui::Text("Mean step:  %.3f ms",
                total_step_ms_ / static_cast<double>(cycle_count_));
        ImGui::Text("Max speed: %.3f ft/s", last_max_speed_);
        ImGui::Text("State: %s", sleeping_ ? "Sleeping (steady)" : "Active");

        if (sel_valid_)
            ImGui::TextDisabled("Brush: [%d, %d]", sel_x_, sel_z_);
        else
            ImGui::TextDisabled("Brush: field center.");

        bool changed = false;
        if (ImGui::Button("Add Water"))
        {
            const auto [cx, cz] = brush_center();
            add_water_disc(cx, cz, k_add_radius, k_add_depth_in);
            changed = true;
        }

        if (ImGui::Button("Step (x1)"))
        {
            wake();
            changed = step_once() || changed;
        }
        ImGui::SameLine();
        if (ImGui::Button("Step (x100)"))
        {
            wake();
            for (int i = 0; i < 100; ++i)
            {
                changed = step_once() || changed;
                if (sleeping_) break;
            }
        }

        ImGui::Separator();
        ImGui::SliderFloat("Damping (1/s)", &damping_, 0.0f, 2.0f, "%.3f");
        ImGui::TextDisabled("MAC staggered grid: u on east faces, v on north faces.");
        ImGui::TextDisabled("Wave speed = sqrt(g*h)  |  dt = %.4f s", k_dt);
        return changed;
    }

private:
    static constexpr float k_gravity    = 32.174f; // ft/s^2
    static constexpr float k_dt         = 0.016f;  // s per step
    static constexpr float k_dx         = 1.0f;    // cell width, ft
    static constexpr float k_in_per_ft  = 12.0f;
    static constexpr int   k_solid_in   = 60;      // terrain >= this = rigid wall
    static constexpr int   k_add_radius = 4;
    static constexpr float k_add_depth_in = 8.0f;
    static constexpr int   k_sleep_steps  = 120;
    static constexpr float k_sleep_speed  = 0.005f; // ft/s

    float damping_ = 0.02f; // linear drag coefficient, 1/s

    // ── Indexing ──────────────────────────────────────────────────────────────

    [[nodiscard]] std::size_t ci(int x, int z) const noexcept
    {
        return static_cast<std::size_t>(z * width_ + x);
    }

    // u-face between cell (x-1,z) and (x,z). x ∈ [0, width_].
    [[nodiscard]] std::size_t ui(int x, int z) const noexcept
    {
        return static_cast<std::size_t>(z * (width_ + 1) + x);
    }

    // v-face between cell (x,z-1) and (x,z). z ∈ [0, depth_].
    [[nodiscard]] std::size_t vi(int x, int z) const noexcept
    {
        return static_cast<std::size_t>(z * width_ + x);
    }

    [[nodiscard]] bool is_solid(int x, int z) const noexcept
    {
        if (x < 0 || x >= width_ || z < 0 || z >= depth_) return true;
        return terrain_[ci(x, z)] >= k_solid_in;
    }

    // Free surface height in feet at a non-solid cell.
    [[nodiscard]] float eta(int x, int z) const noexcept
    {
        const std::size_t i = ci(x, z);
        return static_cast<float>(terrain_[i]) / k_in_per_ft + h_[i];
    }

    [[nodiscard]] std::pair<int, int> brush_center() const noexcept
    {
        if (sel_valid_) return { sel_x_, sel_z_ };
        return { width_ / 2, depth_ / 2 };
    }

    void wake() noexcept
    {
        sleeping_    = false;
        quiet_steps_ = 0;
    }

    void add_water_disc(int cx, int cz, int radius, float depth_inches)
    {
        wake();
        const int   r2         = radius * radius;
        const float depth_feet = depth_inches / k_in_per_ft;

        for (int z = std::max(0, cz - radius); z <= std::min(depth_ - 1, cz + radius); ++z)
        {
            for (int x = std::max(0, cx - radius); x <= std::min(width_ - 1, cx + radius); ++x)
            {
                if (is_solid(x, z)) continue;
                const int ddx = x - cx, ddz = z - cz;
                if (ddx * ddx + ddz * ddz <= r2)
                    h_[ci(x, z)] += depth_feet;
            }
        }
    }

    // ── Simulation steps ──────────────────────────────────────────────────────

    // Pressure step: accelerate each active face by the free-surface head gradient.
    void apply_gravity()
    {
        const float coeff = k_gravity * k_dt / k_dx;

        for (int z = 0; z < depth_; ++z)
        {
            for (int x = 1; x < width_; ++x)   // skip boundary faces (always inactive)
            {
                const std::size_t fi = ui(x, z);
                if (!u_active_[fi]) continue;
                u_[fi] -= coeff * (eta(x, z) - eta(x - 1, z));
            }
        }

        for (int z = 1; z < depth_; ++z)        // skip boundary faces
        {
            for (int x = 0; x < width_; ++x)
            {
                const std::size_t fi = vi(x, z);
                if (!v_active_[fi]) continue;
                v_[fi] -= coeff * (eta(x, z) - eta(x, z - 1));
            }
        }
    }

    void apply_drag()
    {
        const float retain = std::max(0.0f, 1.0f - damping_ * k_dt);
        for (float& u : u_) u *= retain;
        for (float& v : v_) v *= retain;
        // Inactive faces are zero and stay zero.
    }

    // Continuity step: update h using first-order upwind fluxes.
    // Upwind rule: when u > 0 at a face, use the upstream (left/bottom) cell's depth.
    void update_depth()
    {
        const float coeff = k_dt / k_dx;
        last_max_speed_ = 0.0f;

        for (int z = 0; z < depth_; ++z)
        {
            for (int x = 0; x < width_; ++x)
            {
                if (is_solid(x, z)) { h_new_[ci(x, z)] = 0.0f; continue; }

                const float hc = h_[ci(x, z)];

                // East face: positive u = flow leaving to the east.
                const float ue = u_[ui(x + 1, z)];
                const float fe = ue >= 0.0f
                    ? ue * hc
                    : ue * (x + 1 < width_ ? h_[ci(x + 1, z)] : 0.0f);

                // West face: positive u = flow arriving from the west.
                const float uw = u_[ui(x, z)];
                const float fw = uw >= 0.0f
                    ? uw * (x > 0 ? h_[ci(x - 1, z)] : 0.0f)
                    : uw * hc;

                // North face: positive v = flow leaving to the north.
                const float vn = v_[vi(x, z + 1)];
                const float fn = vn >= 0.0f
                    ? vn * hc
                    : vn * (z + 1 < depth_ ? h_[ci(x, z + 1)] : 0.0f);

                // South face: positive v = flow arriving from the south.
                const float vs = v_[vi(x, z)];
                const float fs = vs >= 0.0f
                    ? vs * (z > 0 ? h_[ci(x, z - 1)] : 0.0f)
                    : vs * hc;

                h_new_[ci(x, z)] = std::max(0.0f,
                    hc - coeff * ((fe - fw) + (fn - fs)));

                last_max_speed_ = std::max(last_max_speed_,
                    std::max(std::abs(ue), std::abs(vn)));
            }
        }

        std::swap(h_, h_new_);
    }

    void tick_sleep()
    {
        if (last_max_speed_ <= k_sleep_speed)
            quiet_steps_ = std::min(quiet_steps_ + 1, k_sleep_steps);
        else
            quiet_steps_ = 0;

        if (quiet_steps_ >= k_sleep_steps)
        {
            std::fill(u_.begin(), u_.end(), 0.0f);
            std::fill(v_.begin(), v_.end(), 0.0f);
            sleeping_ = true;
        }
    }

    // ── State ─────────────────────────────────────────────────────────────────

    int  width_  = 0;
    int  depth_  = 0;
    int  cycle_count_    = 0;
    int  quiet_steps_    = k_sleep_steps;
    int  sel_x_          = 0;
    int  sel_z_          = 0;
    bool sel_valid_      = false;
    bool sleeping_       = true;
    float  last_max_speed_ = 0.0f;
    double last_step_ms_   = 0.0;
    double total_step_ms_  = 0.0;

    std::vector<int>   terrain_;
    std::vector<float> h_;
    std::vector<float> u_;       // (W+1)*D east-face velocities, ft/s
    std::vector<float> v_;       // W*(D+1) north-face velocities, ft/s
    std::vector<float> h_new_;
    std::vector<bool>  u_active_; // precomputed: face connects two non-solid cells
    std::vector<bool>  v_active_;
};

} // namespace grannys_house_trials::sim
