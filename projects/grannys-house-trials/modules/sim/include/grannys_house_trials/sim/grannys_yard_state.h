#pragma once

#include "grannys_house_trials/sim/anchor_id.h"

namespace grannys_house_trials::sim
{
struct GrannysYardState
{
    AnchorId current_anchor = AnchorId::Porch;
    bool terrace_cut_blocked = true;
    bool hidden_cross_link_revealed = false;
    bool garden_bed_north_watered = false;
    bool cellar_edge_saturated = false;
    bool path_edge_softened = false;
    unsigned int turn_count = 0;
};
} // namespace grannys_house_trials::sim
