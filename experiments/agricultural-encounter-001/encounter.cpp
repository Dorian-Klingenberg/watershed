#include "encounter.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <utility>
#include <vector>

namespace agricultural_encounter_001
{
namespace
{
constexpr std::array<std::array<int, 2>, 4> k_neighbor_offsets{{
    {1, 0},
    {-1, 0},
    {0, 1},
    {0, -1},
}};

constexpr int k_map_width = 50;
constexpr int k_map_height = 50;

[[nodiscard]] float clamp01(float value)
{
    return std::clamp(value, 0.0F, 1.0F);
}

[[nodiscard]] float soil_base_drain(SoilType soil, const EncounterTuning &tuning)
{
    switch (soil)
    {
    case SoilType::Clay:
        return tuning.clay_base_drain;
    case SoilType::Loam:
        return tuning.loam_base_drain;
    }

    throw std::invalid_argument("Unknown soil type.");
}

[[nodiscard]] float local_pooling_score(const TileState &tile)
{
    return (1.0F - tile.elevation) * 0.65F + tile.saturation;
}

[[nodiscard]] EncounterState simulate_step(
    const EncounterState &current,
    const EncounterTuning &tuning)
{
    EncounterState next = current;
    std::vector<float> saturation_delta(
        static_cast<std::size_t>(current.width() * current.height()), 0.0F);

    const auto index_for = [&current](int x, int y) {
        return static_cast<std::size_t>(y * current.width() + x);
    };

    for (int y = 0; y < current.height(); ++y)
    {
        for (int x = 0; x < current.width(); ++x)
        {
            const TileState &tile = current.at(x, y);
            saturation_delta[index_for(x, y)] += tile.inflow_bias;
            saturation_delta[index_for(x, y)] -= soil_base_drain(tile.soil, tuning);
            saturation_delta[index_for(x, y)] -= tile.drain_bias;
        }
    }

    for (int y = 0; y < current.height(); ++y)
    {
        for (int x = 0; x < current.width(); ++x)
        {
            const TileState &tile = current.at(x, y);
            for (const auto &offset : k_neighbor_offsets)
            {
                const int nx = x + offset[0];
                const int ny = y + offset[1];
                if (!current.in_bounds(nx, ny))
                {
                    continue;
                }

                if (nx < x || (nx == x && ny < y))
                {
                    continue;
                }

                const TileState &neighbor = current.at(nx, ny);
                const float head_delta = local_pooling_score(tile) - local_pooling_score(neighbor);
                if (std::fabs(head_delta) < 0.01F)
                {
                    continue;
                }

                const float transfer = std::min(0.05F, std::fabs(head_delta) * 0.10F);
                if (head_delta > 0.0F)
                {
                    saturation_delta[index_for(x, y)] -= transfer;
                    saturation_delta[index_for(nx, ny)] += transfer;
                }
                else
                {
                    saturation_delta[index_for(x, y)] += transfer;
                    saturation_delta[index_for(nx, ny)] -= transfer;
                }
            }
        }
    }

    for (int y = 0; y < current.height(); ++y)
    {
        for (int x = 0; x < current.width(); ++x)
        {
            TileState &tile = next.at(x, y);
            tile.saturation = clamp01(tile.saturation + saturation_delta[index_for(x, y)]);
        }
    }

    return next;
}
} // namespace

EncounterState::EncounterState(int width, int height)
    : width_(width), height_(height), tiles_(static_cast<std::size_t>(width * height))
{
    if (width <= 0 || height <= 0)
    {
        throw std::invalid_argument("Encounter dimensions must be positive.");
    }
}

int EncounterState::width() const
{
    return width_;
}

int EncounterState::height() const
{
    return height_;
}

bool EncounterState::in_bounds(int x, int y) const
{
    return x >= 0 && x < width_ && y >= 0 && y < height_;
}

TileState &EncounterState::at(int x, int y)
{
    return tiles_.at(index_for(x, y));
}

const TileState &EncounterState::at(int x, int y) const
{
    return tiles_.at(index_for(x, y));
}

std::size_t EncounterState::index_for(int x, int y) const
{
    if (!in_bounds(x, y))
    {
        throw std::out_of_range("Tile index out of bounds.");
    }

    return static_cast<std::size_t>(y * width_ + x);
}

EncounterRuntime::EncounterRuntime()
    : current(make_waterlogged_terrace(tuning)),
      previous(current)
{
    refresh_runtime(*this);
}

EncounterState make_waterlogged_terrace(const EncounterTuning &tuning)
{
    EncounterState state(k_map_width, k_map_height);

    for (int y = 0; y < state.height(); ++y)
    {
        for (int x = 0; x < state.width(); ++x)
        {
            TileState &tile = state.at(x, y);
            tile.soil = (y >= 10 && y <= 40) ? SoilType::Loam : SoilType::Clay;
            tile.elevation = 0.64F;
            tile.saturation = 0.18F;
        }
    }

    for (int y = 12; y <= 37; ++y)
    {
        for (int x = 10; x <= 39; ++x)
        {
            TileState &tile = state.at(x, y);
            tile.target_zone = true;
            tile.soil = SoilType::Loam;
            tile.elevation = 0.46F;
            tile.saturation = 0.50F;
        }
    }

    for (int y = 18; y <= 31; ++y)
    {
        for (int x = 18; x <= 31; ++x)
        {
            TileState &tile = state.at(x, y);
            tile.elevation = 0.26F;
            tile.saturation = 0.80F;
        }
    }

    for (int y = 20; y <= 29; ++y)
    {
        for (int x = 0; x <= 4; ++x)
        {
            TileState &tile = state.at(x, y);
            tile.inflow_bias = tuning.seep_inflow_strength;
            tile.saturation = 0.74F;
            tile.elevation = 0.34F;
        }
    }

    {
        TileState &spring = state.at(16, 2);
        spring.inflow_bias = tuning.seep_inflow_strength * 1.15F;
        spring.saturation = 0.92F;
        spring.elevation = 0.30F;
        spring.soil = SoilType::Clay;
    }

    for (int x = 10; x <= 39; ++x)
    {
        TileState &tile = state.at(x, 38);
        tile.modern_berm = true;
        tile.elevation += 0.20F;
    }

    for (int x = 14; x <= 36; ++x)
    {
        // TileState &tile = state.at(x, 34);
        // tile.buried_drain = true;
        // tile.drain_bias = 0.02F;
    }

    return state;
}

void reset_runtime(EncounterRuntime &runtime)
{
    runtime.current = make_waterlogged_terrace(runtime.tuning);
    runtime.previous = runtime.current;
    runtime.step = 0;
    runtime.goal_status = {};
    refresh_runtime(runtime);
}

void refresh_runtime(EncounterRuntime &runtime)
{
    runtime.qualities = evaluate_land_qualities(
        runtime.current,
        runtime.step > 0 ? &runtime.previous : nullptr,
        runtime.tuning);
    runtime.metrics = analyze_site_metrics(runtime.current, runtime.qualities);
    runtime.goal_status = evaluate_goal(runtime.metrics, 0, runtime.goal);
    if (runtime.step == 0)
    {
        runtime.goal_status.stable_turns = 0;
        runtime.goal_status.persistence_met = false;
        runtime.goal_status.overall_success = false;
    }
}

void simulate_runtime_step(EncounterRuntime &runtime)
{
    runtime.previous = runtime.current;
    runtime.current = simulate_step(runtime.current, runtime.tuning);
    ++runtime.step;
    runtime.qualities = evaluate_land_qualities(runtime.current, &runtime.previous, runtime.tuning);
    runtime.metrics = analyze_site_metrics(runtime.current, runtime.qualities);
    runtime.goal_status = evaluate_goal(runtime.metrics, runtime.goal_status.stable_turns, runtime.goal);
}

void apply_intervention(EncounterRuntime &runtime, InterventionType type)
{
    switch (type)
    {
    case InterventionType::PatchWesternSeep:
        for (int y = 20; y <= 29; ++y)
        {
            for (int x = 0; x <= 4; ++x)
            {
                TileState &tile = runtime.current.at(x, y);
                tile.inflow_bias *= 0.35F;
                tile.saturation = std::max(0.0F, tile.saturation - 0.03F);
            }
        }
        break;

    case InterventionType::OpenBuriedDrain:
        for (int x = 14; x <= 36; ++x)
        {
            TileState &tile = runtime.current.at(x, 34);
            if (tile.buried_drain)
            {
                tile.buried_drain_open = true;
                 tile.drain_bias += runtime.tuning.opened_drain_bonus;
            }
        }
        break;

    case InterventionType::CutReliefChannel:
        for (int offset = 0; offset < 18; ++offset)
        {
            const int x = 22 + offset;
            const int y = 18 + offset;
            if (!runtime.current.in_bounds(x, y))
            {
                continue;
            }

            TileState &tile = runtime.current.at(x, y);
            tile.elevation = std::max(0.0F, tile.elevation - runtime.tuning.relief_channel_cut_depth);
            tile.drain_bias += runtime.tuning.relief_channel_drain_bonus;
            tile.saturation = std::max(0.0F, tile.saturation - 0.06F);
        }
        break;

    case InterventionType::RaiseCentralSpine:
        for (int x = 24; x <= 26; ++x)
        {
            for (int y = 14; y <= 34; ++y)
            {
                TileState &tile = runtime.current.at(x, y);
                tile.elevation += runtime.tuning.central_spine_raise;
                tile.saturation = std::max(0.0F, tile.saturation - 0.02F);
            }
        }
        break;
    }

    runtime.qualities = evaluate_land_qualities(runtime.current, &runtime.previous, runtime.tuning);
    runtime.metrics = analyze_site_metrics(runtime.current, runtime.qualities);
    runtime.goal_status.area_met = runtime.metrics.cultivable_tile_count >= runtime.goal.min_cultivable_tiles;
    runtime.goal_status.contiguity_met = runtime.metrics.largest_cultivable_patch >= runtime.goal.min_largest_patch;
    runtime.goal_status.persistence_met = false;
    runtime.goal_status.overall_success = false;
}

std::vector<ParameterInfo> editable_parameters(EncounterRuntime &runtime)
{
    return {
        {"Clay drain", &runtime.tuning.clay_base_drain, 0.002F, 0.0F, 0.20F},
        {"Loam drain", &runtime.tuning.loam_base_drain, 0.002F, 0.0F, 0.20F},
        {"Seep inflow", &runtime.tuning.seep_inflow_strength, 0.01F, 0.0F, 0.50F},
        {"Open drain bonus", &runtime.tuning.opened_drain_bonus, 0.01F, 0.0F, 0.40F},
        {"Channel drain", &runtime.tuning.relief_channel_drain_bonus, 0.01F, 0.0F, 0.40F},
        {"Channel cut", &runtime.tuning.relief_channel_cut_depth, 0.01F, 0.0F, 0.40F},
        {"Spine raise", &runtime.tuning.central_spine_raise, 0.01F, 0.0F, 0.30F},
        {"Cultivable min", &runtime.tuning.cultivable_min_saturation, 0.01F, 0.0F, 1.0F},
        {"Cultivable max", &runtime.tuning.cultivable_max_saturation, 0.01F, 0.0F, 1.0F},
        {"Trend threshold", &runtime.tuning.trend_delta_threshold, 0.005F, 0.0F, 0.30F},
    };
}

std::vector<TileQuality> evaluate_land_qualities(
    const EncounterState &current,
    const EncounterState *previous,
    const EncounterTuning &tuning)
{
    std::vector<TileQuality> qualities(
        static_cast<std::size_t>(current.width() * current.height()));

    for (int y = 0; y < current.height(); ++y)
    {
        for (int x = 0; x < current.width(); ++x)
        {
            const TileState &tile = current.at(x, y);
            TileQuality &quality = qualities.at(static_cast<std::size_t>(y * current.width() + x));

            if (tile.saturation < tuning.cultivable_min_saturation)
            {
                quality.condition = LandCondition::TooDry;
            }
            else if (tile.saturation > tuning.cultivable_max_saturation)
            {
                quality.condition = LandCondition::Waterlogged;
            }
            else
            {
                quality.condition = LandCondition::Cultivable;
            }

            if (previous != nullptr)
            {
                const float delta = tile.saturation - previous->at(x, y).saturation;
                if (delta > tuning.trend_delta_threshold)
                {
                    quality.trend = MoistureTrend::Wetting;
                }
                else if (delta < -tuning.trend_delta_threshold)
                {
                    quality.trend = MoistureTrend::Drying;
                }
            }

            quality.cultivable =
                quality.condition == LandCondition::Cultivable &&
                quality.trend == MoistureTrend::Stable;
        }
    }

    return qualities;
}

SiteMetrics analyze_site_metrics(
    const EncounterState &state,
    const std::vector<TileQuality> &qualities)
{
    SiteMetrics metrics;
    std::vector<bool> visited(
        static_cast<std::size_t>(state.width() * state.height()), false);

    const auto is_target_cultivable = [&state, &qualities](int x, int y) {
        if (!state.in_bounds(x, y))
        {
            return false;
        }

        const std::size_t index = static_cast<std::size_t>(y * state.width() + x);
        return state.at(x, y).target_zone && qualities.at(index).cultivable;
    };

    for (int y = 0; y < state.height(); ++y)
    {
        for (int x = 0; x < state.width(); ++x)
        {
            const std::size_t index = static_cast<std::size_t>(y * state.width() + x);
            if (!state.at(x, y).target_zone || !qualities.at(index).cultivable)
            {
                continue;
            }

            ++metrics.cultivable_tile_count;

            if (visited.at(index))
            {
                continue;
            }

            int patch_size = 0;
            std::vector<std::pair<int, int>> frontier{{x, y}};
            visited.at(index) = true;

            while (!frontier.empty())
            {
                const auto current = frontier.back();
                frontier.pop_back();
                ++patch_size;

                for (const auto &offset : k_neighbor_offsets)
                {
                    const int nx = current.first + offset[0];
                    const int ny = current.second + offset[1];
                    if (!is_target_cultivable(nx, ny))
                    {
                        continue;
                    }

                    const std::size_t neighbor_index =
                        static_cast<std::size_t>(ny * state.width() + nx);
                    if (visited.at(neighbor_index))
                    {
                        continue;
                    }

                    visited.at(neighbor_index) = true;
                    frontier.push_back({nx, ny});
                }
            }

            ++metrics.cultivable_patch_count;
            metrics.largest_cultivable_patch = std::max(metrics.largest_cultivable_patch, patch_size);
        }
    }

    return metrics;
}

GoalStatus evaluate_goal(
    const SiteMetrics &metrics,
    int prior_stable_turns,
    const GoalDefinition &goal)
{
    GoalStatus status;
    status.area_met = metrics.cultivable_tile_count >= goal.min_cultivable_tiles;
    status.contiguity_met = metrics.largest_cultivable_patch >= goal.min_largest_patch;
    status.stable_turns = (status.area_met && status.contiguity_met)
        ? prior_stable_turns + 1
        : 0;
    status.persistence_met = status.stable_turns >= goal.min_stable_turns;
    status.overall_success = status.area_met && status.contiguity_met && status.persistence_met;
    return status;
}

const char *soil_name(SoilType soil)
{
    switch (soil)
    {
    case SoilType::Clay:
        return "Clay";
    case SoilType::Loam:
        return "Loam";
    }

    return "Unknown";
}

const char *condition_name(LandCondition condition)
{
    switch (condition)
    {
    case LandCondition::TooDry:
        return "too_dry";
    case LandCondition::Cultivable:
        return "cultivable";
    case LandCondition::Waterlogged:
        return "waterlogged";
    }

    return "unknown";
}

const char *trend_name(MoistureTrend trend)
{
    switch (trend)
    {
    case MoistureTrend::Stable:
        return "stable";
    case MoistureTrend::Wetting:
        return "wetting";
    case MoistureTrend::Drying:
        return "drying";
    }

    return "unknown";
}

const char *intervention_name(InterventionType type)
{
    switch (type)
    {
    case InterventionType::PatchWesternSeep:
        return "Patch western seep";
    case InterventionType::OpenBuriedDrain:
        return "Open buried drain";
    case InterventionType::CutReliefChannel:
        return "Cut relief channel";
    case InterventionType::RaiseCentralSpine:
        return "Raise central spine";
    }

    return "Unknown";
}

const char *overlay_name(OverlayMode overlay)
{
    switch (overlay)
    {
    case OverlayMode::LandQuality:
        return "Land quality";
    case OverlayMode::Saturation:
        return "Saturation";
    case OverlayMode::Elevation:
        return "Elevation";
    case OverlayMode::SoilType:
        return "Soil type";
    case OverlayMode::TargetZone:
        return "Target zone";
    case OverlayMode::Infrastructure:
        return "Infrastructure";
    }

    return "Unknown";
}
} // namespace agricultural_encounter_001



