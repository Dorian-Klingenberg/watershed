#pragma once

#include "simulator.h"

namespace tide_logic_regulator_002
{
enum class OverlayMode
{
    HydraulicState,
    PressureHead,
    StructuralStress,
    ControlState
};

struct UiState
{
    OverlayMode overlay = OverlayMode::HydraulicState;
    bool running = false;
    bool quit_requested = false;
    bool interactive = true;
    int tick_ms = 120;
    int sample_x = 3;
    int sample_y = 3;
    bool needs_redraw = true;
};

struct DashboardOptions
{
    int ticks = 60;
    int sleep_ms = 120;
    bool clear_screen = true;
    bool interactive = true;
};

int run_dashboard(const DashboardOptions &options);
} // namespace tide_logic_regulator_002
