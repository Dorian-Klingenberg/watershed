#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace agricultural_encounter_001
{
enum class SoilType
{
    Clay,
    Loam,
};

enum class LandCondition
{
    TooDry,
    Cultivable,
    Waterlogged,
};

enum class MoistureTrend
{
    Stable,
    Wetting,
    Drying,
};

enum class InterventionType
{
    PatchWesternSeep,
    OpenBuriedDrain,
    CutReliefChannel,
    RaiseCentralSpine,
};

enum class OverlayMode
{
    LandQuality,
    Saturation,
    Elevation,
    SoilType,
    TargetZone,
    Infrastructure,
};

struct EncounterTuning
{
    float clay_base_drain = 0.015F;
    float loam_base_drain = 0.025F;
    float seep_inflow_strength = 0.14F;
    float opened_drain_bonus = 0.08F;
    float relief_channel_drain_bonus = 0.20F;
    float relief_channel_cut_depth = 0.15F;
    float central_spine_raise = 0.08F;
    float cultivable_min_saturation = 0.30F;
    float cultivable_max_saturation = 0.58F;
    float trend_delta_threshold = 0.035F;
};

struct GoalDefinition
{
    int min_cultivable_tiles = 300;
    int min_largest_patch = 220;
    int min_stable_turns = 3;
};

struct TileState
{
    SoilType soil = SoilType::Loam;
    float elevation = 0.0F;
    float saturation = 0.0F;
    float inflow_bias = 0.0F;
    float drain_bias = 0.0F;
    bool target_zone = false;
    bool buried_drain = false;
    bool buried_drain_open = false;
    bool modern_berm = false;
};

struct TileQuality
{
    LandCondition condition = LandCondition::TooDry;
    MoistureTrend trend = MoistureTrend::Stable;
    bool cultivable = false;
};

struct SiteMetrics
{
    int cultivable_tile_count = 0;
    int largest_cultivable_patch = 0;
    int cultivable_patch_count = 0;
};

struct GoalStatus
{
    bool area_met = false;
    bool contiguity_met = false;
    bool persistence_met = false;
    bool overall_success = false;
    int stable_turns = 0;
};

struct ParameterInfo
{
    const char *label = "";
    float *value = nullptr;
    float step = 0.01F;
    float min_value = 0.0F;
    float max_value = 1.0F;
};

class EncounterState
{
public:
    EncounterState(int width, int height);

    [[nodiscard]] int width() const;
    [[nodiscard]] int height() const;
    [[nodiscard]] bool in_bounds(int x, int y) const;

    TileState &at(int x, int y);
    const TileState &at(int x, int y) const;

private:
    [[nodiscard]] std::size_t index_for(int x, int y) const;

    int width_ = 0;
    int height_ = 0;
    std::vector<TileState> tiles_;
};

struct EncounterRuntime
{
    EncounterTuning tuning;
    GoalDefinition goal;
    EncounterState current;
    EncounterState previous;
    std::vector<TileQuality> qualities;
    SiteMetrics metrics;
    GoalStatus goal_status;
    int step = 0;

    EncounterRuntime();
};

[[nodiscard]] EncounterState make_waterlogged_terrace(const EncounterTuning &tuning);
void reset_runtime(EncounterRuntime &runtime);
void refresh_runtime(EncounterRuntime &runtime);
void simulate_runtime_step(EncounterRuntime &runtime);
void apply_intervention(EncounterRuntime &runtime, InterventionType type, int center_x, int center_y);
[[nodiscard]] std::vector<ParameterInfo> editable_parameters(EncounterRuntime &runtime);

[[nodiscard]] std::vector<TileQuality> evaluate_land_qualities(
    const EncounterState &current,
    const EncounterState *previous,
    const EncounterTuning &tuning);
[[nodiscard]] SiteMetrics analyze_site_metrics(
    const EncounterState &state,
    const std::vector<TileQuality> &qualities);
[[nodiscard]] GoalStatus evaluate_goal(
    const SiteMetrics &metrics,
    int prior_stable_turns,
    const GoalDefinition &goal);

[[nodiscard]] const char *soil_name(SoilType soil);
[[nodiscard]] const char *condition_name(LandCondition condition);
[[nodiscard]] const char *trend_name(MoistureTrend trend);
[[nodiscard]] const char *intervention_name(InterventionType type);
[[nodiscard]] const char *overlay_name(OverlayMode overlay);
} // namespace agricultural_encounter_001

