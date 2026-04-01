#pragma once

#include "grannys_house_trials/sim/anchor_id.h"

namespace grannys_house_trials::sim
{
struct GrannysYardState
{
    AnchorId current_anchor = AnchorId::Porch;
    bool drain_source_routed = false;
    bool terrace_channel_dug = false;
    bool hidden_cross_link_revealed = false;
    bool hidden_cross_link_active = true;
    bool flat_stone_packed = false;
    bool cellar_edge_packed = false;
    bool garden_bed_north_watered = false;
    bool cellar_edge_saturated = false;
    bool path_edge_softened = false;
    bool objective_completed = false;
    bool objective_failed = false;
    unsigned int turn_count = 0;
    unsigned int simulation_step_count = 0;

    [[nodiscard]] bool operator==(const GrannysYardState &) const = default;
};
} // namespace grannys_house_trials::sim
