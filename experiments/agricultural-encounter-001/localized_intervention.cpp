#include "encounter.h"

#include <algorithm>
#include <cstdlib>

namespace agricultural_encounter_001
{
void apply_intervention(EncounterRuntime &runtime, InterventionType type, int center_x, int center_y)
{
    const auto falloff = [](int dx, int dy) -> float
    {
        const int distance = (std::max)(std::abs(dx), std::abs(dy));
        switch (distance)
        {
        case 0:
            return 1.0F;
        case 1:
            return 0.6F;
        case 2:
            return 0.3F;
        default:
            return 0.0F;
        }
    };

    for (int dy = -2; dy <= 2; ++dy)
    {
        for (int dx = -2; dx <= 2; ++dx)
        {
            const float weight = falloff(dx, dy);
            if (weight <= 0.0F)
            {
                continue;
            }

            const int x = center_x + dx;
            const int y = center_y + dy;
            if (!runtime.current.in_bounds(x, y))
            {
                continue;
            }

            TileState &tile = runtime.current.at(x, y);
            switch (type)
            {
            case InterventionType::PatchWesternSeep:
                tile.inflow_bias *= (std::max)(0.0F, 1.0F - (0.65F * weight));
                tile.saturation = (std::max)(0.0F, tile.saturation - (0.03F * weight));
                break;

            case InterventionType::OpenBuriedDrain:
                if (tile.buried_drain)
                {
                    tile.buried_drain_open = true;
                    tile.drain_bias += runtime.tuning.opened_drain_bonus * weight;
                    tile.saturation = (std::max)(0.0F, tile.saturation - (0.02F * weight));
                }
                break;

            case InterventionType::CutReliefChannel:
                tile.elevation = (std::max)(0.0F, tile.elevation - (runtime.tuning.relief_channel_cut_depth * weight));
                tile.drain_bias += runtime.tuning.relief_channel_drain_bonus * weight;
                tile.saturation = (std::max)(0.0F, tile.saturation - (0.06F * weight));
                break;

            case InterventionType::RaiseCentralSpine:
                tile.elevation += runtime.tuning.central_spine_raise * weight;
                tile.saturation = (std::max)(0.0F, tile.saturation - (0.02F * weight));
                break;
            }
        }
    }

    runtime.qualities = evaluate_land_qualities(runtime.current, &runtime.previous, runtime.tuning);
    runtime.metrics = analyze_site_metrics(runtime.current, runtime.qualities);
    runtime.goal_status.area_met = runtime.metrics.cultivable_tile_count >= runtime.goal.min_cultivable_tiles;
    runtime.goal_status.contiguity_met = runtime.metrics.largest_cultivable_patch >= runtime.goal.min_largest_patch;
    runtime.goal_status.persistence_met = false;
    runtime.goal_status.overall_success = false;
}
} // namespace agricultural_encounter_001
