#pragma once

#include "simulator.h"

namespace voxel_infrastructure_004
{
enum class ViewMode
{
    XYSliceAtZ,
    YZSliceAtX,
    XZSliceAtY,
    Isometric
};

enum class OverlayMode
{
    Material,
    Saturation,
    Pressure,
    StructuralRisk
};

struct UiState
{
    OverlayMode overlay = OverlayMode::Material;
    bool running = false;
    bool quit_requested = false;
    int tick_ms = 140;
    int sample_x = 6;
    int sample_y = 4;
    int sample_z = 2;
    int plane_x = 6;
    int plane_y = 4;
    int plane_z = 2;
    ViewMode view = ViewMode::XYSliceAtZ;
    int hidden_depth = 0;
    bool needs_redraw = true;
};

struct DashboardOptions
{
    int ticks = 60;
    int sleep_ms = 140;
    bool clear_screen = true;
    bool interactive = true;
};

int run_dashboard(const DashboardOptions &options);
} // namespace voxel_infrastructure_004
