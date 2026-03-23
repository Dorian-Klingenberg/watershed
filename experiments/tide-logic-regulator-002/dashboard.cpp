#include "dashboard.h"

#define NOMINMAX
#include <Windows.h>
#include <conio.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace tide_logic_regulator_002
{
namespace
{
struct RgbColor
{
    int red = 0;
    int green = 0;
    int blue = 0;
};

class AlternateScreenSession
{
  public:
    AlternateScreenSession()
    {
        stdout_handle_ = GetStdHandle(STD_OUTPUT_HANDLE);
        stdin_handle_ = GetStdHandle(STD_INPUT_HANDLE);

        GetConsoleMode(stdout_handle_, &original_out_mode_);
        GetConsoleMode(stdin_handle_, &original_in_mode_);

        DWORD out_mode = original_out_mode_ | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        SetConsoleMode(stdout_handle_, out_mode);

        DWORD in_mode = original_in_mode_;
        in_mode &= ~ENABLE_ECHO_INPUT;
        in_mode &= ~ENABLE_LINE_INPUT;
        SetConsoleMode(stdin_handle_, in_mode);

        std::cout << "\x1b[?1049h\x1b[?25l\x1b[2J\x1b[H";
        std::cout.flush();
    }

    ~AlternateScreenSession()
    {
        std::cout << "\x1b[?25h\x1b[?1049l";
        std::cout.flush();
        SetConsoleMode(stdout_handle_, original_out_mode_);
        SetConsoleMode(stdin_handle_, original_in_mode_);
    }

  private:
    HANDLE stdout_handle_ = INVALID_HANDLE_VALUE;
    HANDLE stdin_handle_ = INVALID_HANDLE_VALUE;
    DWORD original_out_mode_ = 0;
    DWORD original_in_mode_ = 0;
};

double clamp01(double value)
{
    return std::clamp(value, 0.0, 1.0);
}

std::string reset_ansi()
{
    return "\x1b[0m";
}

std::string pad_right(const std::string &text, int width)
{
    if (static_cast<int>(text.size()) >= width)
    {
        return text.substr(0, static_cast<std::size_t>(width));
    }

    return text + std::string(static_cast<std::size_t>(width - static_cast<int>(text.size())), ' ');
}

int visible_length(const std::string &text)
{
    int length = 0;
    bool in_escape = false;
    for (char ch : text)
    {
        if (!in_escape)
        {
            if (ch == '\x1b')
            {
                in_escape = true;
            }
            else
            {
                ++length;
            }
        }
        else if (ch == 'm')
        {
            in_escape = false;
        }
    }
    return length;
}

std::string pad_ansi(const std::string &text, int width)
{
    const int length = visible_length(text);
    if (length >= width)
    {
        return text;
    }

    return text + std::string(static_cast<std::size_t>(width - length), ' ');
}

RgbColor blend(RgbColor a, RgbColor b, double t)
{
    const double clamped = clamp01(t);
    const auto channel = [clamped](int start, int end) {
        return static_cast<int>(std::round(static_cast<double>(start) + (static_cast<double>(end - start) * clamped)));
    };

    return {channel(a.red, b.red), channel(a.green, b.green), channel(a.blue, b.blue)};
}

std::string ansi_bg(RgbColor color)
{
    std::ostringstream out;
    out << "\x1b[48;2;" << color.red << ';' << color.green << ';' << color.blue << 'm';
    return out.str();
}

std::string ansi_fg(RgbColor color)
{
    std::ostringstream out;
    out << "\x1b[38;2;" << color.red << ';' << color.green << ';' << color.blue << 'm';
    return out.str();
}

std::string color_text(const std::string &text, RgbColor color)
{
    return ansi_fg(color) + text + reset_ansi();
}

std::string badge(const std::string &text, RgbColor fg, RgbColor bg)
{
    return ansi_bg(bg) + ansi_fg(fg) + ' ' + text + ' ' + reset_ansi();
}

std::string section_bar(const std::string &text, int width, RgbColor fg, RgbColor bg)
{
    return ansi_bg(bg) + ansi_fg(fg) + pad_right(" " + text + " ", width) + reset_ansi();
}

std::string key_value(const std::string &key, const std::string &value, int key_width)
{
    return " " + color_text(pad_right(key, key_width), {132, 178, 236}) + " " + value;
}

std::string key_value_inline(
    const std::string &left_key,
    const std::string &left_value,
    const std::string &right_key,
    const std::string &right_value,
    int key_width)
{
    return key_value(left_key, left_value, key_width) + "    " + key_value(right_key, right_value, key_width);
}

std::string format_fixed(double value, int precision = 2)
{
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(precision) << value;
    return stream.str();
}

std::string signal_text(bool value)
{
    return value ? badge("HIGH", {10, 24, 10}, {94, 184, 94}) : badge("LOW", {34, 24, 10}, {189, 140, 70});
}

std::string state_badge(bool value, const std::string &on_text, const std::string &off_text)
{
    return value ? badge(on_text, {10, 24, 10}, {94, 184, 94}) : badge(off_text, {28, 28, 28}, {120, 120, 120});
}

std::string overlay_name(OverlayMode overlay)
{
    switch (overlay)
    {
    case OverlayMode::HydraulicState:
        return "Hydraulic State";
    case OverlayMode::PressureHead:
        return "Pressure Head";
    case OverlayMode::StructuralStress:
        return "Structural Stress";
    case OverlayMode::ControlState:
        return "Control State";
    }

    return "Unknown";
}

std::string siphon_state_name(TileSiphonState state)
{
    switch (state)
    {
    case TileSiphonState::Unprimed:
        return "Unprimed";
    case TileSiphonState::Priming:
        return "Priming";
    case TileSiphonState::Active:
        return "Active";
    case TileSiphonState::Broken:
        return "Broken";
    }

    return "Unknown";
}

char tile_glyph(const TileState &tile, OverlayMode overlay)
{
    if (overlay == OverlayMode::PressureHead)
    {
        if (tile.pressure_head.value < 1.0)
        {
            return '.';
        }
        if (tile.pressure_head.value < 2.5)
        {
            return ':';
        }
        if (tile.pressure_head.value < 4.0)
        {
            return 'o';
        }
        if (tile.pressure_head.value < 6.0)
        {
            return 'O';
        }
        return '@';
    }

    if (overlay == OverlayMode::StructuralStress)
    {
        const double stress = clamp01(
            (tile.structural_damage * 0.45) + (tile.leak_rate * 2.0) + (tile.sediment * 0.32) + (tile.geometry_drift * 0.65));
        if (stress < 0.18)
        {
            return '.';
        }
        if (stress < 0.36)
        {
            return ':';
        }
        if (stress < 0.54)
        {
            return 'x';
        }
        if (stress < 0.75)
        {
            return 'X';
        }
        return '#';
    }

    if (overlay == OverlayMode::ControlState)
    {
        switch (tile.kind)
        {
        case TileKind::PressureComparator:
            return 'C';
        case TileKind::DelayBasin:
            return 'D';
        case TileKind::AirTrapChamber:
            return 'A';
        case TileKind::ThresholdLip:
            return 'L';
        case TileKind::SiphonChannel:
            return 'S';
        case TileKind::OverflowSpillway:
            return 'O';
        case TileKind::Reservoir:
            return 'R';
        case TileKind::TideChannel:
            return 'T';
        default:
            return '.';
        }
    }

    switch (tile.kind)
    {
    case TileKind::Reservoir:
        return 'R';
    case TileKind::FlowBiasChannel:
        return 'f';
    case TileKind::TideChannel:
        return 't';
    case TileKind::PressureComparator:
        return 'C';
    case TileKind::DelayBasin:
        return 'D';
    case TileKind::AirTrapChamber:
        return 'A';
    case TileKind::ThresholdLip:
        return 'L';
    case TileKind::SiphonChannel:
        return 'S';
    case TileKind::OverflowSpillway:
        return 'w';
    case TileKind::Settlement:
        return 'S';
    case TileKind::Sea:
        return '~';
    case TileKind::Void:
    default:
        return '.';
    }
}

RgbColor hydraulic_background(const TileState &tile)
{
    const RgbColor dry{34, 38, 44};
    const RgbColor wet{42, 116, 190};
    RgbColor color = blend(dry, wet, clamp01(tile.water_level.value / 8.5));

    switch (tile.kind)
    {
    case TileKind::Reservoir:
        color = blend(color, {84, 128, 182}, 0.45);
        break;
    case TileKind::PressureComparator:
        color = blend(color, {108, 92, 156}, 0.35);
        break;
    case TileKind::DelayBasin:
        color = blend(color, {88, 128, 166}, 0.40);
        break;
    case TileKind::AirTrapChamber:
        color = blend(color, {120, 124, 166}, 0.42);
        break;
    case TileKind::ThresholdLip:
        color = blend(color, {124, 114, 92}, 0.42);
        break;
    case TileKind::SiphonChannel:
        color = blend(color, {90, 132, 164}, 0.42);
        break;
    case TileKind::OverflowSpillway:
        color = blend(color, {72, 128, 88}, 0.52);
        break;
    case TileKind::Settlement:
        color = blend(color, {132, 102, 84}, 0.50);
        break;
    case TileKind::Sea:
        color = blend(color, {46, 86, 160}, 0.70);
        break;
    default:
        break;
    }

    return color;
}

RgbColor pressure_background(const TileState &tile)
{
    return blend({44, 48, 56}, {216, 112, 78}, clamp01(tile.pressure_head.value / 8.0));
}

RgbColor stress_background(const TileState &tile)
{
    const double stress = clamp01(
        (tile.structural_damage * 0.45) + (tile.leak_rate * 2.0) + (tile.sediment * 0.32) + (tile.geometry_drift * 0.65));
    return blend({44, 56, 48}, {156, 58, 54}, stress);
}

RgbColor control_background(const SimulationSnapshot &snapshot, const TileState &tile)
{
    const ControlState &control = snapshot.control;

    switch (tile.kind)
    {
    case TileKind::Reservoir:
        return control.reservoir_pressure_ready ? RgbColor{84, 150, 108} : RgbColor{96, 74, 62};
    case TileKind::TideChannel:
        return control.tide_backpressure_safe ? RgbColor{80, 138, 96} : RgbColor{154, 92, 78};
    case TileKind::PressureComparator:
        return control.differential_ready ? RgbColor{110, 168, 112} : RgbColor{82, 74, 112};
    case TileKind::DelayBasin:
        return control.basin_memory_charged ? RgbColor{120, 176, 126} : RgbColor{70, 84, 112};
    case TileKind::AirTrapChamber:
        return snapshot.metrics.air_intrusion < 0.45 ? RgbColor{98, 152, 132} : RgbColor{158, 96, 86};
    case TileKind::SiphonChannel:
    case TileKind::ThresholdLip:
        return control.release_siphon_active ? RgbColor{116, 182, 118} : RgbColor{98, 86, 72};
    case TileKind::OverflowSpillway:
        return control.overflow_active ? RgbColor{76, 158, 110} : RgbColor{62, 94, 74};
    default:
        return {38, 42, 50};
    }
}

RgbColor overlay_background(const SimulationSnapshot &snapshot, OverlayMode overlay, const TileState &tile)
{
    switch (overlay)
    {
    case OverlayMode::HydraulicState:
        return hydraulic_background(tile);
    case OverlayMode::PressureHead:
        return pressure_background(tile);
    case OverlayMode::StructuralStress:
        return stress_background(tile);
    case OverlayMode::ControlState:
        return control_background(snapshot, tile);
    }

    return {36, 40, 46};
}

RgbColor glyph_color(const TileState &tile, OverlayMode overlay)
{
    if (overlay == OverlayMode::StructuralStress)
    {
        return {244, 238, 230};
    }

    if (overlay == OverlayMode::PressureHead)
    {
        return tile.pressure_head.value > 4.0 ? RgbColor{252, 244, 236} : RgbColor{220, 228, 236};
    }

    if (overlay == OverlayMode::ControlState)
    {
        return {248, 248, 248};
    }

    switch (tile.kind)
    {
    case TileKind::Settlement:
        return {255, 236, 216};
    case TileKind::Sea:
        return {236, 244, 255};
    default:
        return {240, 244, 248};
    }
}

std::string render_tile(const SimulationSnapshot &snapshot, const UiState &ui, int x, int y)
{
    const TileState &tile = snapshot.tiles[static_cast<std::size_t>(y * snapshot.width + x)];
    RgbColor bg = overlay_background(snapshot, ui.overlay, tile);
    RgbColor fg = glyph_color(tile, ui.overlay);
    const char glyph = tile_glyph(tile, ui.overlay);
    const bool selected = x == ui.sample_x && y == ui.sample_y;

    if (selected)
    {
        bg = blend(bg, {245, 245, 245}, 0.35);
        fg = {14, 16, 20};
    }

    std::ostringstream out;
    out << ansi_bg(bg) << ansi_fg(fg);
    if (selected)
    {
        out << "\x1b[1m\x1b[4m";
    }
    out << glyph << ' ' << reset_ansi();
    return out.str();
}

void append_boxed_lines(std::ostringstream &out, const std::string &title, const std::vector<std::string> &lines, int inner_width)
{
    out << '+' << std::string(static_cast<std::size_t>(inner_width), '-') << "+\n";
    out << '|' << pad_ansi(title, inner_width) << "|\n";
    out << '+' << std::string(static_cast<std::size_t>(inner_width), '-') << "+\n";
    for (const std::string &line : lines)
    {
        out << '|' << pad_ansi(line, inner_width) << "|\n";
    }
    out << '+' << std::string(static_cast<std::size_t>(inner_width), '-') << "+\n";
}

std::string tile_kind_name(TileKind kind)
{
    switch (kind)
    {
    case TileKind::Reservoir:
        return "Reservoir";
    case TileKind::FlowBiasChannel:
        return "Flow Bias Channel";
    case TileKind::TideChannel:
        return "Tide Channel";
    case TileKind::PressureComparator:
        return "Pressure Comparator";
    case TileKind::DelayBasin:
        return "Delay Basin";
    case TileKind::AirTrapChamber:
        return "Air Trap Chamber";
    case TileKind::ThresholdLip:
        return "Threshold Lip";
    case TileKind::SiphonChannel:
        return "Siphon Channel";
    case TileKind::OverflowSpillway:
        return "Overflow Spillway";
    case TileKind::Settlement:
        return "Settlement";
    case TileKind::Sea:
        return "Sea";
    case TileKind::Void:
    default:
        return "Void";
    }
}

void append_overlay_legend_lines(std::vector<std::string> &lines, OverlayMode overlay, int panel_width)
{
    lines.push_back(section_bar("ACTIVE FIELD LEGEND", panel_width, {242, 238, 224}, {56, 56, 56}));
    lines.push_back(" overlay    " + color_text(overlay_name(overlay), {232, 238, 246}));

    switch (overlay)
    {
    case OverlayMode::HydraulicState:
        lines.push_back(" symbols    R reservoir  f flow bias  t tide  C comparator  D basin  A air trap  L lip  S siphon  w spillway");
        lines.push_back(" meaning    background follows tile water level; brighter blues mean more stored or moving water");
        break;
    case OverlayMode::PressureHead:
        lines.push_back(" symbols    . : o O @  (increasing pressure head)");
        lines.push_back(" meaning    hotter cells are carrying more hydraulic load and are more likely to propagate remote effects");
        break;
    case OverlayMode::StructuralStress:
        lines.push_back(" symbols    . : x X #  (increasing combined damage, leakage, and sediment stress)");
        lines.push_back(" meaning    redder cells are where decay or patchwork is warping the regulator's computation");
        break;
    case OverlayMode::ControlState:
        lines.push_back(" symbols    R reservoir head  T tide safety  C differential  D memory  A air health  S release siphon  O spillway");
        lines.push_back(" meaning    green means permissive or active; amber/red means blocked, unsafe, air-broken, or inactive");
        break;
    }

    lines.push_back(" overlays   1 hydraulic  2 pressure  3 stress  4 control");
    lines.push_back(std::string());
}

void draw_frame(const SimulationSnapshot &snapshot, const UiState &ui)
{
    const int field_inner_width = snapshot.width * 2;
    const int panel_width = 112;
    const int sample_x = std::clamp(ui.sample_x, 0, snapshot.width - 1);
    const int sample_y = std::clamp(ui.sample_y, 0, snapshot.height - 1);
    const TileState &sample = snapshot.tiles[static_cast<std::size_t>(sample_y * snapshot.width + sample_x)];
    const HydraulicMetrics &metrics = snapshot.metrics;
    const ControlState &control = snapshot.control;

    const auto paused_badge = badge("PAUSED", {32, 24, 10}, {214, 178, 76});
    const auto running_badge = badge("RUNNING", {10, 24, 10}, {94, 184, 94});
    const auto active_badge = badge("ACTIVE", {10, 24, 10}, {94, 184, 94});
    const auto idle_badge = badge("IDLE", {28, 28, 28}, {120, 120, 120});
    const auto risk_badge = metrics.settlement_risk > 0.65
        ? badge("SEVERE", {252, 236, 230}, {156, 58, 54})
        : metrics.settlement_risk > 0.25
            ? badge("RISING", {46, 32, 12}, {214, 178, 76})
            : badge("LOW", {10, 22, 10}, {108, 196, 108});

    std::ostringstream out;
    out << "\x1b[H";

    append_boxed_lines(
        out,
        ansi_bg({26, 40, 58}) + ansi_fg({232, 238, 246}) + pad_right(" EXPERIMENT 002 :: TIDE-LOGIC REGULATOR DASHBOARD ", panel_width) + reset_ansi(),
        {
            std::string(" ") + color_text("STATE", {132, 178, 236}) + "  " + (ui.running ? running_badge : paused_badge) +
                "    " + color_text("STEP", {132, 178, 236}) + " " + std::to_string(snapshot.tick) +
                "    " + color_text("OVERLAY", {132, 178, 236}) + " " + overlay_name(ui.overlay) +
                "    " + color_text("TICK", {132, 178, 236}) + " " + std::to_string(ui.tick_ms) + "ms",
            std::string(" ") + color_text("SCENARIO", {132, 178, 236}) +
                "  degraded coastal fluidic regulator with delayed tide sensing, unstable air trap, cracked delay basin, and exposed spillway",
        },
        panel_width);

    out << '+' << std::string(static_cast<std::size_t>(field_inner_width), '-') << "+\n";
    out << '|' << pad_ansi(
        ansi_bg({32, 42, 54}) + ansi_fg({224, 230, 236}) +
        pad_right(" FIELD :: " + overlay_name(ui.overlay) + " ", field_inner_width) +
        reset_ansi(),
        field_inner_width) << "|\n";
    out << '+' << std::string(static_cast<std::size_t>(field_inner_width), '-') << "+\n";
    for (int y = 0; y < snapshot.height; ++y)
    {
        out << '|';
        for (int x = 0; x < snapshot.width; ++x)
        {
            out << render_tile(snapshot, ui, x, y);
        }
        out << "|\n";
    }
    out << '+' << std::string(static_cast<std::size_t>(field_inner_width), '-') << "+\n";

    std::vector<std::string> info_lines;
    info_lines.push_back(section_bar("SYSTEM METRICS", panel_width, {245, 232, 194}, {74, 58, 26}));
    info_lines.push_back(key_value_inline("Reservoir Level", format_fixed(metrics.reservoir_level), "Reservoir Pressure", format_fixed(metrics.reservoir_pressure), 18));
    info_lines.push_back(key_value_inline("Tide Actual", format_fixed(metrics.tide_actual), "Tide Sensed", format_fixed(metrics.tide_sensed), 18));
    info_lines.push_back(key_value_inline("Siphon Release", format_fixed(metrics.siphon_release), "Overflow Release", format_fixed(metrics.overflow_release), 18));
    info_lines.push_back(key_value_inline("Downstream Delivery", format_fixed(metrics.downstream_delivery), "Marsh Level", format_fixed(metrics.marsh_level), 18));
    info_lines.push_back(key_value_inline("Basin Charge", format_fixed(metrics.basin_charge), "Air Intrusion", format_fixed(metrics.air_intrusion), 18));
    info_lines.push_back(key_value_inline("Settlement Risk", risk_badge, "Release State", siphon_state_name(control.release_siphon_state), 18));
    info_lines.push_back(std::string());

    info_lines.push_back(section_bar("CONTROL STATE", panel_width, {231, 238, 247}, {34, 62, 88}));
    info_lines.push_back(
        std::string(" ") + color_text("Reservoir Head", {132, 178, 236}) + " " + signal_text(control.reservoir_pressure_ready) +
        "    " + color_text("Tide Safe", {132, 178, 236}) + " " + signal_text(control.tide_backpressure_safe) +
        "    " + color_text("Differential", {132, 178, 236}) + " " + signal_text(control.differential_ready));
    info_lines.push_back(
        std::string(" ") + color_text("Delay Basin", {132, 178, 236}) + " " + state_badge(control.basin_memory_charged, "CHARGED", "DRAINING") +
        "    " + color_text("Release Siphon", {132, 178, 236}) + " " + (control.release_siphon_active ? active_badge : idle_badge) +
        "    " + color_text("Overflow", {132, 178, 236}) + " " + state_badge(control.overflow_active, "ACTIVE", "IDLE"));
    info_lines.push_back(std::string());

    info_lines.push_back(section_bar("SAMPLE TILE", panel_width, {229, 241, 232}, {34, 74, 46}));
    info_lines.push_back(key_value_inline("Position", "(" + std::to_string(sample_x) + "," + std::to_string(sample_y) + ")", "Type", tile_kind_name(sample.kind), 18));
    info_lines.push_back(key_value_inline("Water", format_fixed(sample.water_level.value), "Pressure", format_fixed(sample.pressure_head.value), 18));
    info_lines.push_back(key_value_inline("Air Volume", format_fixed(sample.air_volume.value), "Resistance", format_fixed(sample.flow_resistance.value), 18));
    info_lines.push_back(key_value_inline("Sediment", format_fixed(sample.sediment), "Damage", format_fixed(sample.structural_damage), 18));
    info_lines.push_back(key_value_inline("Leak Rate", format_fixed(sample.leak_rate), "Geometry Drift", format_fixed(sample.geometry_drift), 18));
    info_lines.push_back(key_value("Siphon State", siphon_state_name(sample.siphon_state), 18));
    info_lines.push_back(std::string());

    info_lines.push_back(section_bar("INTERVENTIONS", panel_width, {243, 232, 247}, {72, 42, 82}));
    info_lines.push_back(" c clear tide intake sediment: faster, truer backpressure signal and less false-safe siphon priming");
    info_lines.push_back(" s seal reservoir leak: raises inland head and delivery, but can intensify remote miscomputation");
    info_lines.push_back(" m repair delay basin: steadier memory retention, but wrong states persist longer");
    info_lines.push_back(" o reopen overflow path: restores sacrificial spill behavior and reduces comparator stress");
    info_lines.push_back(" z reset scenario");
    info_lines.push_back(std::string());

    append_overlay_legend_lines(info_lines, ui.overlay, panel_width);

    info_lines.push_back(section_bar("RECENT EVENTS", panel_width, {242, 238, 224}, {56, 56, 56}));
    if (snapshot.recent_events.empty())
    {
        info_lines.push_back(" no anomalies logged");
    }
    else
    {
        for (const std::string &event_text : snapshot.recent_events)
        {
            info_lines.push_back(" " + event_text);
        }
    }
    info_lines.push_back(std::string());

    info_lines.push_back(section_bar("CONTROLS", panel_width, {242, 238, 224}, {56, 56, 56}));
    info_lines.push_back(" sim        space play/pause   n single-step   -/+ speed");
    info_lines.push_back(" inspect    arrow keys move selected tile");
    info_lines.push_back(" overlays   1 hydraulic  2 pressure  3 stress  4 control");
    info_lines.push_back(" actions    c clear tide intake   s seal leak   m repair basin   o reopen spillway   z reset");
    info_lines.push_back(" shell      q quit");

    append_boxed_lines(
        out,
        ansi_bg({28, 34, 42}) + ansi_fg({220, 228, 236}) + pad_right(" STATUS / DIAGNOSTICS ", panel_width) + reset_ansi(),
        info_lines,
        panel_width);
    out << "\x1b[J";

    std::cout << out.str() << std::flush;
}

void handle_keypress(int key, Simulator &simulator, UiState &ui, const SimulationSnapshot &snapshot)
{
    ui.needs_redraw = true;

    if (key == 0 || key == 224)
    {
        const int extended = _getch();
        switch (extended)
        {
        case 72:
            ui.sample_y = std::max(0, ui.sample_y - 1);
            return;
        case 80:
            ui.sample_y = std::min(snapshot.height - 1, ui.sample_y + 1);
            return;
        case 75:
            ui.sample_x = std::max(0, ui.sample_x - 1);
            return;
        case 77:
            ui.sample_x = std::min(snapshot.width - 1, ui.sample_x + 1);
            return;
        default:
            return;
        }
    }

    switch (key)
    {
    case 'q':
    case 'Q':
        ui.quit_requested = true;
        return;
    case ' ':
        ui.running = !ui.running;
        return;
    case 'n':
    case 'N':
        simulator.step();
        return;
    case '1':
        ui.overlay = OverlayMode::HydraulicState;
        return;
    case '2':
        ui.overlay = OverlayMode::PressureHead;
        return;
    case '3':
        ui.overlay = OverlayMode::StructuralStress;
        return;
    case '4':
        ui.overlay = OverlayMode::ControlState;
        return;
    case '-':
        ui.tick_ms = std::min(1000, ui.tick_ms + 20);
        return;
    case '+':
    case '=':
        ui.tick_ms = std::max(20, ui.tick_ms - 20);
        return;
    case 'c':
    case 'C':
        simulator.apply_intervention(InterventionType::ClearTideIntake);
        return;
    case 's':
    case 'S':
        simulator.apply_intervention(InterventionType::SealReservoirLeak);
        return;
    case 'm':
    case 'M':
        simulator.apply_intervention(InterventionType::RepairDelayBasin);
        return;
    case 'o':
    case 'O':
        simulator.apply_intervention(InterventionType::ReopenOverflowPath);
        return;
    case 'z':
    case 'Z':
        simulator.reset();
        return;
    default:
        return;
    }
}

int run_non_interactive(const DashboardOptions &options)
{
    Simulator simulator;
    UiState ui;
    ui.interactive = false;

    for (int frame = 0; frame < options.ticks; ++frame)
    {
        if (options.clear_screen)
        {
            std::cout << "\x1B[2J\x1B[H";
        }

        draw_frame(simulator.snapshot(), ui);

        if (frame + 1 < options.ticks)
        {
            simulator.step();
        }

        if (options.sleep_ms > 0 && frame + 1 < options.ticks)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(options.sleep_ms));
        }
    }

    return 0;
}
} // namespace

int run_dashboard(const DashboardOptions &options)
{
    if (!options.interactive)
    {
        return run_non_interactive(options);
    }

    AlternateScreenSession terminal_session;
    Simulator simulator;
    UiState ui;

    const SimulationSnapshot initial = simulator.snapshot();
    ui.sample_x = initial.width / 2;
    ui.sample_y = initial.height / 2;

    auto next_tick = std::chrono::steady_clock::now();

    while (!ui.quit_requested)
    {
        SimulationSnapshot snapshot = simulator.snapshot();

        while (_kbhit())
        {
            handle_keypress(_getch(), simulator, ui, snapshot);
            snapshot = simulator.snapshot();
        }

        const auto now = std::chrono::steady_clock::now();
        if (ui.running && now >= next_tick)
        {
            simulator.step();
            ui.needs_redraw = true;
            next_tick = now + std::chrono::milliseconds(ui.tick_ms);
            snapshot = simulator.snapshot();
        }

        if (ui.needs_redraw)
        {
            draw_frame(snapshot, ui);
            ui.needs_redraw = false;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    return 0;
}
} // namespace tide_logic_regulator_002
