#pragma once

#include <cstddef>
#include <utility>
#include <vector>

namespace scalar_field_flooding
{
    enum class TerrainType
    {
        Bedrock,
        Clay,
        Loam,
        Sand,
    };

    enum class WaterFeatureType
    {
        None,
        SpringSource,
    };

    struct TerrainProperties
    {
        float saturation_capacity = 0.0F;
        float lateral_spread = 0.0F;
    };

    struct GridSummary
    {
        int wet_cells = 0;
        int airable_cells = 0;
        float average_saturation = 0.0F;
        float average_airability = 0.0F;
    };

    inline constexpr int k_experiment_grid_width = 50;
    inline constexpr int k_experiment_grid_height = 50;

    struct CellState
    {
        TerrainType terrain = TerrainType::Loam;
        WaterFeatureType water_feature = WaterFeatureType::None;
        float saturation = 0.0F;
    };

    class GridState
    {
    public:
        GridState(int width, int height);

        [[nodiscard]] int width() const;
        [[nodiscard]] int height() const;
        [[nodiscard]] bool in_bounds(int x, int y) const;

        CellState &at(int x, int y);
        const CellState &at(int x, int y) const;

    private:
        [[nodiscard]] std::size_t index_for(int x, int y) const;

        int width_ = 0;
        int height_ = 0;
        std::vector<CellState> cells_;
    };

    [[nodiscard]] float clamp01(float value);
    [[nodiscard]] float current_airability(const CellState &cell);
    [[nodiscard]] bool is_airable(const CellState &cell);
    [[nodiscard]] GridSummary summarize_grid(const GridState &grid);
    [[nodiscard]] GridState simulate_step(const GridState &current);

    void run_tests();
} // namespace scalar_field_flooding
