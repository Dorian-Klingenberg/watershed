#pragma once

#include "i_field_sim.h"

#include "../third_party/imgui/imgui.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <stdexcept>
#include <utility>
#include <vector>

namespace grannys_house_trials::sim
{
class GrannyMapVisualSim final : public IFieldSim
{
public:
    enum Material : std::uint32_t
    {
        grass = 0,
        high_grass = 1,
        packed_earth = 2,
        path_stone = 3,
        wet_soil = 4,
        stone_canal = 5,
        canal_water = 6,
        garden_loam = 7,
        terrace_stone = 8,
        house_stone = 9,
        roof_wood = 10,
        loose_boulder = 11,
        hidden_drain = 12,
        safe_swale = 13,
        check_dam = 14,
        cistern_stone = 15,
        splitter_box = 16,
    };

    static constexpr int feet_wide = 108;
    static constexpr int feet_deep = 156;
    static constexpr int inches_per_foot = 12;
    static constexpr int fine_wide = feet_wide * inches_per_foot;
    static constexpr int fine_deep = feet_deep * inches_per_foot;

    GrannyMapVisualSim()
    {
        generate();
    }

    [[nodiscard]] const char* name() const noexcept override
    {
        return "Authored Granny Map";
    }

    [[nodiscard]] int width() const noexcept override { return fine_wide; }
    [[nodiscard]] int depth() const noexcept override { return fine_deep; }

    [[nodiscard]] int height_at(int x, int z) const override
    {
        return heights_.at(index_of(x, z));
    }

    [[nodiscard]] std::uint32_t material_id_at(int x, int z) const override
    {
        return materials_.at(index_of(x, z));
    }

    [[nodiscard]] const char* material_name_at(int x, int z) const override
    {
        return material_name(material_id_at(x, z));
    }

    void reset(int new_width, int new_depth, std::vector<int> heights_inches) override
    {
        (void)heights_inches;
        if (new_width != fine_wide || new_depth != fine_deep)
            throw std::invalid_argument("GrannyMapVisualSim: unexpected map dimensions.");

        generate();
    }

    [[nodiscard]] bool render_ui() override
    {
        ImGui::Text("Authored size: %d x %d ft", feet_wide, feet_deep);
        ImGui::Text("Render grid:   %d x %d one-inch cells", fine_wide, fine_deep);
        ImGui::TextDisabled("Broad ground stays foot-flat; structures carry inch detail.");
        ImGui::Separator();
        ImGui::Text("Visible systems");
        ImGui::BulletText("Cistern -> canal -> terraces -> splitter");
        ImGui::BulletText("Safe swale and check dams to lower garden");
        ImGui::BulletText("Hidden drains are purple/debug-visible");
        ImGui::BulletText("House, canal, terraces, boulders use inch-scale top detail");
        return false;
    }

    [[nodiscard]] static const char* material_name(std::uint32_t material) noexcept
    {
        switch (material)
        {
        case grass: return "Grass";
        case high_grass: return "High Grass";
        case packed_earth: return "Packed Earth";
        case path_stone: return "Path Stone";
        case wet_soil: return "Wet Soil";
        case stone_canal: return "Stone Canal";
        case canal_water: return "Canal Water";
        case garden_loam: return "Garden Loam";
        case terrace_stone: return "Terrace Stone";
        case house_stone: return "House Stone";
        case roof_wood: return "Roof Wood";
        case loose_boulder: return "Loose Boulder";
        case hidden_drain: return "Hidden Drain";
        case safe_swale: return "Safe Swale";
        case check_dam: return "Check Dam";
        case cistern_stone: return "Cistern Stone";
        case splitter_box: return "Splitter Box";
        default: return "Unknown";
        }
    }

private:
    struct Segment
    {
        float ax;
        float az;
        float bx;
        float bz;
    };

    [[nodiscard]] static float distance_to_segment(float x, float z, const Segment& segment) noexcept
    {
        const float vx = segment.bx - segment.ax;
        const float vz = segment.bz - segment.az;
        const float wx = x - segment.ax;
        const float wz = z - segment.az;
        const float len_sq = vx * vx + vz * vz;
        const float t = len_sq > 0.0001f
            ? std::clamp((wx * vx + wz * vz) / len_sq, 0.0f, 1.0f)
            : 0.0f;
        const float px = segment.ax + vx * t;
        const float pz = segment.az + vz * t;
        const float dx = x - px;
        const float dz = z - pz;
        return std::sqrt(dx * dx + dz * dz);
    }

    [[nodiscard]] static float distance_to_path(
        float x,
        float z,
        const std::initializer_list<Segment> segments) noexcept
    {
        float best = 100000.0f;
        for (const Segment& segment : segments)
            best = std::min(best, distance_to_segment(x, z, segment));
        return best;
    }

    [[nodiscard]] static bool in_rect(float x, float z, float min_x, float max_x, float min_z, float max_z) noexcept
    {
        return x >= min_x && x <= max_x && z >= min_z && z <= max_z;
    }

    [[nodiscard]] static bool in_ellipse(float x, float z, float cx, float cz, float rx, float rz) noexcept
    {
        const float nx = (x - cx) / rx;
        const float nz = (z - cz) / rz;
        return nx * nx + nz * nz <= 1.0f;
    }

    [[nodiscard]] static int base_ground_height_inches(int foot_x, int foot_z) noexcept
    {
        const float north_to_south = 148.0f - static_cast<float>(foot_z) * 0.72f;
        const float east_bank = 9.0f * std::sin(static_cast<float>(foot_x) * 0.12f);
        const float cross_roll = 5.0f * std::sin(static_cast<float>(foot_z) * 0.07f + 1.4f);
        const int raw = std::max(18, static_cast<int>(north_to_south + east_bank + cross_roll));
        return ((raw + 6) / inches_per_foot) * inches_per_foot;
    }

    [[nodiscard]] static int local_cobble_offset(int x, int z) noexcept
    {
        const int h = (x * 37 + z * 53 + (x / 3) * 11 + (z / 5) * 7) & 7;
        return h <= 1 ? -1 : (h >= 6 ? 1 : 0);
    }

    void generate()
    {
        heights_.assign(static_cast<std::size_t>(fine_wide * fine_deep), 0);
        materials_.assign(static_cast<std::size_t>(fine_wide * fine_deep), grass);

        for (int z = 0; z < fine_deep; ++z)
        {
            for (int x = 0; x < fine_wide; ++x)
            {
                const int foot_x = x / inches_per_foot;
                const int foot_z = z / inches_per_foot;
                const int local_x = x % inches_per_foot;
                const int local_z = z % inches_per_foot;
                const float fx = (static_cast<float>(x) + 0.5f) / static_cast<float>(inches_per_foot);
                const float fz = (static_cast<float>(z) + 0.5f) / static_cast<float>(inches_per_foot);

                int height = base_ground_height_inches(foot_x, foot_z);
                std::uint32_t material = ((foot_x + foot_z * 3) % 17 == 0) ? high_grass : grass;

                apply_cistern(fx, fz, local_x, local_z, height, material);
                apply_terraces(fx, fz, local_x, local_z, height, material);
                apply_canal(fx, fz, local_x, local_z, height, material);
                apply_splitter(fx, fz, local_x, local_z, height, material);
                apply_house(fx, fz, local_x, local_z, height, material);
                apply_boulder_wall(fx, fz, local_x, local_z, height, material);
                apply_paths_and_gardens(fx, fz, local_x, local_z, height, material);
                apply_safe_swale(fx, fz, local_x, local_z, height, material);
                apply_hidden_drains(fx, fz, height, material);

                const std::size_t i = index_of(x, z);
                heights_[i] = std::max(1, height);
                materials_[i] = material;
            }
        }
    }

    static void apply_cistern(float fx, float fz, int local_x, int local_z, int& height, std::uint32_t& material)
    {
        const float dx = fx - 16.0f;
        const float dz = fz - 13.0f;
        const float dist = std::sqrt(dx * dx + dz * dz);
        if (dist <= 8.0f)
        {
            material = cistern_stone;
            height = 166 + local_cobble_offset(local_x, local_z);
        }
        if (dist <= 5.2f)
        {
            material = canal_water;
            height = 154;
        }
        if (dist >= 5.2f && dist <= 6.4f)
        {
            material = cistern_stone;
            height = 176 + ((local_x == 0 || local_z == 0) ? 2 : 0);
        }
    }

    static void apply_canal(float fx, float fz, int local_x, int local_z, int& height, std::uint32_t& material)
    {
        const float d = distance_to_path(fx, fz, {
            {21.0f, 13.0f, 43.0f, 12.0f},
            {43.0f, 12.0f, 69.0f, 18.0f},
            {69.0f, 18.0f, 62.0f, 27.0f},
            {62.0f, 27.0f, 55.5f, 36.0f},
            {55.5f, 36.0f, 55.5f, 68.0f},
        });

        if (d <= 2.2f)
        {
            material = stone_canal;
            height = std::max(height, 128 + local_cobble_offset(local_x, local_z));
        }

        if (d <= 0.68f)
        {
            material = canal_water;
            height = 122;
        }

        if (d > 0.68f && d <= 1.05f)
        {
            material = stone_canal;
            height = 135 + (local_x == 0 || local_z == 11 ? 2 : 0);
        }
    }

    static void apply_terraces(float fx, float fz, int local_x, int local_z, int& height, std::uint32_t& material)
    {
        constexpr std::array<int, 4> shelf_heights{126, 114, 102, 90};
        for (int terrace = 0; terrace < 4; ++terrace)
        {
            const float min_z = 24.0f + static_cast<float>(terrace) * 11.0f;
            const float max_z = min_z + 8.0f;
            if (!in_rect(fx, fz, 34.0f, 70.0f, min_z, max_z))
                continue;

            const bool wall = fx < 35.5f || fx > 68.5f || fz < min_z + 1.0f || fz > max_z - 1.0f;
            material = wall ? terrace_stone : garden_loam;
            const int stone_detail = ((local_x + local_z) % 5 == 0) ? 1 : 0;
            height = shelf_heights[static_cast<std::size_t>(terrace)]
                + (wall ? 8 + stone_detail : ((local_z % 4 == 0) ? -2 : (local_z % 4 == 3 ? 1 : 0)));
        }
    }

    static void apply_splitter(float fx, float fz, int local_x, int local_z, int& height, std::uint32_t& material)
    {
        if (in_rect(fx, fz, 51.0f, 63.0f, 68.0f, 80.0f))
        {
            const bool rim = fx < 52.4f || fx > 61.6f || fz < 69.4f || fz > 78.6f;
            material = rim ? splitter_box : canal_water;
            height = rim ? 87 + local_cobble_offset(local_x, local_z) : 78;
        }
    }

    static void apply_house(float fx, float fz, int local_x, int local_z, int& height, std::uint32_t& material)
    {
        if (in_rect(fx, fz, 57.0f, 87.0f, 79.0f, 108.0f))
        {
            material = house_stone;
            height = 78;
        }

        if (in_rect(fx, fz, 60.0f, 84.0f, 82.0f, 104.0f))
        {
            const float ridge_dx = std::abs(fx - 72.0f);
            const int roof_height = 112 + static_cast<int>((12.0f - std::min(12.0f, ridge_dx)) * 2.2f);
            material = roof_wood;
            height = roof_height + (((local_x + local_z) % 6 == 0) ? 1 : 0);
        }

        if (in_rect(fx, fz, 54.0f, 90.0f, 76.0f, 111.0f)
            && !in_rect(fx, fz, 57.0f, 87.0f, 79.0f, 108.0f))
        {
            material = wet_soil;
            height = std::min(height, 64);
        }
    }

    static void apply_boulder_wall(float fx, float fz, int local_x, int local_z, int& height, std::uint32_t& material)
    {
        const float d = distance_to_path(fx, fz, {
            {21.0f, 35.0f, 17.0f, 60.0f},
            {17.0f, 60.0f, 20.0f, 89.0f},
            {20.0f, 89.0f, 16.0f, 116.0f},
        });

        if (d <= 3.4f)
        {
            material = loose_boulder;
            const int lump = ((local_x * 3 + local_z * 5 + static_cast<int>(fx * 7.0f)) % 11) - 4;
            height = std::max(height + 8, 84 + lump);
        }
    }

    static void apply_paths_and_gardens(float fx, float fz, int local_x, int local_z, int& height, std::uint32_t& material)
    {
        const float path_d = distance_to_path(fx, fz, {
            {17.0f, 122.0f, 38.0f, 112.0f},
            {38.0f, 112.0f, 66.0f, 112.0f},
            {66.0f, 112.0f, 88.0f, 122.0f},
        });
        if (path_d <= 2.2f)
        {
            material = path_stone;
            height = std::max(34, height - 3 + local_cobble_offset(local_x, local_z));
        }

        if (in_ellipse(fx, fz, 39.0f, 139.0f, 23.0f, 12.0f))
        {
            material = garden_loam;
            height = 30 + ((local_z % 5 == 0) ? -2 : 0);
        }
    }

    static void apply_safe_swale(float fx, float fz, int local_x, int local_z, int& height, std::uint32_t& material)
    {
        const float d = distance_to_path(fx, fz, {
            {62.0f, 74.0f, 88.0f, 87.0f},
            {88.0f, 87.0f, 91.0f, 116.0f},
            {91.0f, 116.0f, 61.0f, 135.0f},
            {61.0f, 135.0f, 47.0f, 139.0f},
        });

        if (d <= 2.1f)
        {
            material = safe_swale;
            height = std::min(height, 44 + ((local_x + local_z) % 3 == 0 ? -1 : 0));
        }

        const bool check_dam_cell =
            in_rect(fx, fz, 85.0f, 91.0f, 98.0f, 100.5f)
            || in_rect(fx, fz, 76.0f, 82.0f, 124.0f, 126.5f)
            || in_rect(fx, fz, 57.0f, 63.0f, 135.0f, 137.5f);
        if (check_dam_cell)
        {
            material = check_dam;
            height = std::max(height, 50 + local_cobble_offset(local_x, local_z));
        }
    }

    static void apply_hidden_drains(float fx, float fz, int& height, std::uint32_t& material)
    {
        const float d = distance_to_path(fx, fz, {
            {58.0f, 75.0f, 67.0f, 88.0f},
            {67.0f, 88.0f, 72.0f, 111.0f},
            {56.0f, 75.0f, 39.0f, 95.0f},
        });

        if (d <= 0.42f)
        {
            material = hidden_drain;
            height = std::max(height, 67);
        }
    }

    [[nodiscard]] static std::size_t index_of(int x, int z)
    {
        if (x < 0 || x >= fine_wide || z < 0 || z >= fine_deep)
            throw std::out_of_range("GrannyMapVisualSim: coordinates out of range.");

        return static_cast<std::size_t>(z * fine_wide + x);
    }

    std::vector<int> heights_;
    std::vector<std::uint32_t> materials_;
};
} // namespace grannys_house_trials::sim
