#pragma once

#include "encounter.h"

namespace agricultural_encounter_001
{
struct UiState
{
    OverlayMode overlay = OverlayMode::LandQuality;
    bool running = false;
    bool quit_requested = false;
    int selected_parameter = 0;
    int tick_ms = 180;
    int sample_x = 25;
    int sample_y = 25;
    bool needs_redraw = true;
};

void run_console_ui();
} // namespace agricultural_encounter_001
