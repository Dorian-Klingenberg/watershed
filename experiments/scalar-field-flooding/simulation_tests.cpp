#include "simulation.h"
#include "map_generation.h"
#include "terrain.h"

#include <cmath>
#include <stdexcept>

namespace scalar_field_flooding
{
namespace
{
// The tests in this file are meant to read like a compact specification for the
// prototype. When the simulation changes, this is the first place to check what
// behavior we still consider intentional.

void require(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

void test_grid_access()
{
    // The grid is the most basic invariant in the experiment. Before trusting
    // any hydrology result, we want to know that reads, writes, and bounds
    // checks behave predictably.
    GridState grid(3, 2);
    grid.at(1, 1).terrain = TerrainType::Sand;
    require(grid.at(1, 1).terrain == TerrainType::Sand, "Grid access should preserve stored terrain.");
    require(grid.in_bounds(2, 1), "Expected valid coordinates to be in bounds.");
    require(!grid.in_bounds(3, 1), "Expected invalid coordinates to be out of bounds.");
}

void test_terrain_properties_match_intended_drainage_order()
{
    // These relative terrain relationships are the core truth we want to
    // preserve:
    // - bedrock fills quickly and sheds early
    // - clay holds more than bedrock but spreads slowly
    // - loam holds more than clay
    // - sand stores the most and also spreads the most laterally
    //
    // We intentionally test ordering rather than every exact number so we can
    // keep tuning without rewriting the whole spec each time.
    const TerrainProperties bedrock = terrain_properties(TerrainType::Bedrock);
    const TerrainProperties clay = terrain_properties(TerrainType::Clay);
    const TerrainProperties loam = terrain_properties(TerrainType::Loam);
    const TerrainProperties sand = terrain_properties(TerrainType::Sand);

    require(bedrock.saturation_capacity < clay.saturation_capacity &&
                clay.saturation_capacity < loam.saturation_capacity &&
                loam.saturation_capacity < sand.saturation_capacity,
            "Terrain storage capacity should rise from bedrock to sand.");
    require(sand.lateral_spread > bedrock.lateral_spread &&
                bedrock.lateral_spread > clay.lateral_spread &&
                loam.lateral_spread > clay.lateral_spread,
            "Sand should spread water the most, while bedrock sheds more readily than clay.");
}

void test_spring_increases_local_saturation()
{
    // A spring must behave like a reliable local source. If this stops being
    // true, many later tests become hard to interpret because the world is no
    // longer receiving water at the origin point we expect.
    GridState grid(3, 3);
    grid.at(1, 1).terrain = TerrainType::Clay;
    grid.at(1, 1).water_feature = WaterFeatureType::SpringSource;

    const GridState next = simulate_step(grid);
    require(next.at(1, 1).saturation > grid.at(1, 1).saturation,
            "A spring source should increase local saturation.");
}

void test_spring_saturates_neighbors()
{
    // Springs are not meant to be isolated point values forever. Even before a
    // cell fully overflows downhill, nearby tiles should feel the local wetting
    // effect of being adjacent to a source.
    GridState grid(3, 3);
    grid.at(1, 1).terrain = TerrainType::Clay;
    grid.at(1, 1).water_feature = WaterFeatureType::SpringSource;
    grid.at(1, 0).terrain = TerrainType::Clay;

    const GridState next = simulate_step(grid);

    require(next.at(1, 0).saturation > 0.0F,
            "A cell next to a spring should gain saturation from the spring's local spill.");
}

void test_sand_spreads_faster_than_clay()
{
    // This test isolates lateral behavior. Both setups start with the same
    // water amount and differ only by terrain, so the neighbor gain tells us
    // whether sand is really acting as the faster-spreading material.
    GridState sand_grid(3, 1);
    sand_grid.at(0, 0).terrain = TerrainType::Sand;
    sand_grid.at(1, 0).terrain = TerrainType::Sand;
    sand_grid.at(0, 0).saturation = 1.0F;

    GridState clay_grid(3, 1);
    clay_grid.at(0, 0).terrain = TerrainType::Clay;
    clay_grid.at(1, 0).terrain = TerrainType::Clay;
    clay_grid.at(0, 0).saturation = 1.0F;

    const GridState sand_next = simulate_step(sand_grid);
    const GridState clay_next = simulate_step(clay_grid);

    require(sand_next.at(1, 0).saturation > clay_next.at(1, 0).saturation,
            "Sand should pass more water to neighboring cells than clay.");
}

void test_overflow_moves_downhill_once_capacity_is_exceeded()
{
    // The new model treats terrain capacity as the line between "stored here"
    // and "must go somewhere else." Once a cell is above capacity, the excess
    // should leave the tile and move downhill during the step.
    GridState grid(1, 2);
    grid.at(0, 0).terrain = TerrainType::Clay;
    grid.at(0, 0).saturation = 0.70F;
    grid.at(0, 1).terrain = TerrainType::Clay;

    const GridState next = simulate_step(grid);

    require(next.at(0, 1).saturation > 0.0F,
            "Cells above their terrain capacity should send overflow downhill.");
}

void test_water_flows_downhill()
{
    // This is the simplest downhill transfer case: one wet tile above one dry
    // tile. We keep it small on purpose so the test documents the intended
    // direction of overflow without any noise from lateral neighbors.
    GridState grid(1, 2);
    grid.at(0, 0).terrain = TerrainType::Sand;
    grid.at(0, 0).saturation = 1.0F;
    grid.at(0, 1).terrain = TerrainType::Sand;
    grid.at(0, 1).saturation = 0.0F;

    const GridState next = simulate_step(grid);

    require(next.at(0, 1).saturation > 0.0F,
            "Water drained from an upper tile should move into the tile below.");
}

void test_airability_changes_with_saturation()
{
    // Airability is derived from how full a terrain is relative to its own
    // storage capacity. A wetter cell should always be less airable than a
    // drier cell of the same terrain.
    CellState dry_sand;
    dry_sand.terrain = TerrainType::Sand;
    dry_sand.saturation = 0.10F;

    CellState wet_sand;
    wet_sand.terrain = TerrainType::Sand;
    wet_sand.saturation = 0.80F;

    require(current_airability(dry_sand) > current_airability(wet_sand),
            "Airability should drop as saturation rises.");
}

void test_bottom_edge_keeps_drainage_inside_boundary()
{
    // The bottom row has no downhill recipient. We currently treat that as a
    // hard boundary for downhill flow, so the tile should end the step in a
    // valid state rather than retaining impossible "extra" water above
    // capacity.
    GridState grid(3, 1);
    grid.at(0, 0).terrain = TerrainType::Sand;
    grid.at(0, 0).saturation = 1.10F;
    grid.at(1, 0).terrain = TerrainType::Sand;
    grid.at(1, 0).saturation = 1.10F;
    grid.at(2, 0).terrain = TerrainType::Sand;
    grid.at(2, 0).saturation = 1.10F;

    const GridState next = simulate_step(grid);
    const float capacity = terrain_properties(TerrainType::Sand).saturation_capacity;

    require(next.at(1, 0).saturation >= 0.0F,
            "Bottom-edge cells should remain in a valid non-negative state.");
    require(next.at(1, 0).saturation <= capacity,
            "Without a lower cell, a tile should not remain above its terrain capacity.");
}

void test_bedrock_sheds_overflow_sooner_than_clay()
{
    // Bedrock should start shedding downhill sooner than clay because its
    // capacity is lower. Giving both terrains the same starting wetness lets
    // us compare that difference directly.
    GridState bedrock_grid(1, 2);
    bedrock_grid.at(0, 0).terrain = TerrainType::Bedrock;
    bedrock_grid.at(0, 0).saturation = 0.60F;
    bedrock_grid.at(0, 1).terrain = TerrainType::Bedrock;

    GridState clay_grid(1, 2);
    clay_grid.at(0, 0).terrain = TerrainType::Clay;
    clay_grid.at(0, 0).saturation = 0.60F;
    clay_grid.at(0, 1).terrain = TerrainType::Clay;

    const GridState bedrock_next = simulate_step(bedrock_grid);
    const GridState clay_next = simulate_step(clay_grid);

    require(bedrock_next.at(0, 1).saturation > clay_next.at(0, 1).saturation,
            "Bedrock should send downhill overflow sooner than clay at the same wetness.");
}

void test_sand_has_more_airability_than_clay_at_same_saturation()
{
    // Equal absolute water should not imply equal usability. Because sand can
    // tolerate more stored water before feeling "full," it should remain more
    // airable than clay at the same saturation amount.
    CellState sand;
    sand.terrain = TerrainType::Sand;
    sand.saturation = 0.40F;

    CellState clay;
    clay.terrain = TerrainType::Clay;
    clay.saturation = 0.40F;

    require(current_airability(sand) > current_airability(clay),
            "At the same saturation, sand should show more airability than clay.");
}

void test_manual_map_generation_succeeds_at_configured_size()
{
    // The authored test map should obey the single grid-size configuration
    // point. This protects us from slipping back toward hard-coded map sizes.
    const GridState grid = make_manual_test_map();
    require(grid.width() == k_experiment_grid_width, "Manual test map should use the configured width.");
    require(grid.height() == k_experiment_grid_height, "Manual test map should use the configured height.");
}

void test_generated_springs_are_in_bounds()
{
    // Spring generation must be safe on the configured map. This test is less
    // about exact placement and more about guaranteeing that normalized anchor
    // conversion never produces out-of-bounds coordinates.
    const auto spring_positions = generated_spring_positions(10, 10);
    require(!spring_positions.empty(), "Generated spring positions should not be empty.");

    for (const auto& spring : spring_positions)
    {
        require(spring.first >= 0 && spring.first < 10, "Generated spring x should stay in bounds.");
        require(spring.second >= 0 && spring.second < 10, "Generated spring y should stay in bounds.");
    }
}

void test_small_manual_map_has_springs_and_multiple_terrains()
{
    // Even a tiny configured map should still be useful as a demo world. We
    // want at least one spring to drive the simulation and enough terrain
    // variety to show meaningful differences in behavior.
    const GridState grid = make_manual_test_map();

    int spring_count = 0;
    bool has_bedrock = false;
    bool has_clay = false;
    bool has_loam = false;
    bool has_sand = false;

    for (int y = 0; y < grid.height(); ++y)
    {
        for (int x = 0; x < grid.width(); ++x)
        {
            const CellState& cell = grid.at(x, y);
            if (cell.water_feature == WaterFeatureType::SpringSource)
            {
                ++spring_count;
            }

            switch (cell.terrain)
            {
            case TerrainType::Bedrock:
                has_bedrock = true;
                break;
            case TerrainType::Clay:
                has_clay = true;
                break;
            case TerrainType::Loam:
                has_loam = true;
                break;
            case TerrainType::Sand:
                has_sand = true;
                break;
            }
        }
    }

    const int terrain_type_count = static_cast<int>(has_bedrock) + static_cast<int>(has_clay) +
                                   static_cast<int>(has_loam) + static_cast<int>(has_sand);

    require(spring_count > 0, "Small manual maps should include at least one spring.");
    require(terrain_type_count >= 2, "Small manual maps should include multiple terrain types.");
}
} // namespace

void run_tests()
{
    // Keep the run order grouped from foundational invariants to model rules
    // to generated-content checks. When a failure appears, this makes it easier
    // to tell whether the breakage is structural, hydrological, or authored-map
    // related.
    test_grid_access();
    test_terrain_properties_match_intended_drainage_order();
    test_spring_increases_local_saturation();
    test_spring_saturates_neighbors();
    test_sand_spreads_faster_than_clay();
    test_overflow_moves_downhill_once_capacity_is_exceeded();
    test_water_flows_downhill();
    test_bottom_edge_keeps_drainage_inside_boundary();
    test_bedrock_sheds_overflow_sooner_than_clay();
    test_airability_changes_with_saturation();
    test_sand_has_more_airability_than_clay_at_same_saturation();
    test_manual_map_generation_succeeds_at_configured_size();
    test_generated_springs_are_in_bounds();
    test_small_manual_map_has_springs_and_multiple_terrains();
}
} // namespace scalar_field_flooding
