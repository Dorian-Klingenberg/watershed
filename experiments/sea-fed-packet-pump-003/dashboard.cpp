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

namespace sea_fed_packet_pump_003
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

std::string ansi_fg(RgbColor color)
{
    std::ostringstream out;
    out << "\x1b[38;2;" << color.red << ';' << color.green << ';' << color.blue << 'm';
    return out.str();
}

std::string ansi_bg(RgbColor color)
{
    std::ostringstream out;
    out << "\x1b[48;2;" << color.red << ';' << color.green << ';' << color.blue << 'm';
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

std::string section_bar(const std::string &text, int width, RgbColor fg, RgbColor bg)
{
    return ansi_bg(bg) + ansi_fg(fg) + pad_right(" " + text + " ", width) + reset_ansi();
}

std::string format_fixed(double value, int precision = 2)
{
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(precision) << value;
    return stream.str();
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

std::string overlay_name(OverlayMode overlay)
{
    switch (overlay)
    {
    case OverlayMode::HydraulicState:
        return "Hydraulic State";
    case OverlayMode::PulseState:
        return "Pulse State";
    case OverlayMode::PacketIsolation:
        return "Packet Isolation";
    case OverlayMode::SalinityRisk:
        return "Salinity Risk";
    }

    return "Unknown";
}

std::string siphon_state_name(SiphonState state)
{
    switch (state)
    {
    case SiphonState::Idle:
        return "Idle";
    case SiphonState::Charging:
        return "Charging";
    case SiphonState::Triggered:
        return "Triggered";
    case SiphonState::Recovering:
        return "Recovering";
    case SiphonState::Misfiring:
        return "Misfiring";
    }

    return "Unknown";
}

std::string packet_state_name(PacketState state)
{
    switch (state)
    {
    case PacketState::Empty:
        return "Empty";
    case PacketState::Filling:
        return "Filling";
    case PacketState::Lifting:
        return "Lifting";
    case PacketState::Captured:
        return "Captured";
    case PacketState::Lost:
        return "Lost";
    }

    return "Unknown";
}

std::string state_badge(bool value, const std::string &on_text, const std::string &off_text)
{
    return value ? badge(on_text, {10, 24, 10}, {94, 184, 94}) : badge(off_text, {28, 28, 28}, {120, 120, 120});
}

std::string packet_badge(PacketState state)
{
    switch (state)
    {
    case PacketState::Captured:
        return badge("CAPTURED", {10, 24, 10}, {94, 184, 94});
    case PacketState::Filling:
        return badge("FILLING", {16, 22, 34}, {118, 164, 230});
    case PacketState::Lifting:
        return badge("LIFTING", {20, 22, 28}, {224, 195, 104});
    case PacketState::Lost:
        return badge("LOST", {28, 18, 16}, {204, 112, 96});
    case PacketState::Empty:
    default:
        return badge("EMPTY", {28, 28, 28}, {120, 120, 120});
    }
}

bool is_sea_side(TileKind kind)
{
    switch (kind)
    {
    case TileKind::SeaReservoir:
    case TileKind::RestrictedInlet:
    case TileKind::ChargeChamber:
    case TileKind::TriggerSiphon:
    case TileKind::PulseChamber:
        return true;
    default:
        return false;
    }
}

bool is_freshwater_side(TileKind kind)
{
    switch (kind)
    {
    case TileKind::Spring:
    case TileKind::IntakePocket:
    case TileKind::PacketRiser:
    case TileKind::CaptureBasin:
    case TileKind::VentStack:
    case TileKind::UpperCistern:
    case TileKind::SpillLip:
        return true;
    default:
        return false;
    }
}

RgbColor blend(RgbColor a, RgbColor b, double t)
{
    const double clamped = clamp01(t);
    const auto channel = [clamped](int start, int end) {
        return static_cast<int>(std::lround(static_cast<double>(start) + (static_cast<double>(end - start) * clamped)));
    };

    return {channel(a.red, b.red), channel(a.green, b.green), channel(a.blue, b.blue)};
}

char tile_glyph(const TileState &tile, OverlayMode overlay)
{
    switch (overlay)
    {
    case OverlayMode::HydraulicState:
        switch (tile.kind)
        {
        case TileKind::SeaReservoir:
            return '~';
        case TileKind::RestrictedInlet:
            return '=';
        case TileKind::ChargeChamber:
            return 'C';
        case TileKind::TriggerSiphon:
            return 'S';
        case TileKind::PulseChamber:
            return 'P';
        case TileKind::Spring:
            return 'f';
        case TileKind::IntakePocket:
            return 'i';
        case TileKind::PacketRiser:
            return '|';
        case TileKind::CaptureBasin:
            return 'B';
        case TileKind::VentStack:
            return 'V';
        case TileKind::UpperCistern:
            return 'U';
        case TileKind::SpillLip:
            return 'L';
        case TileKind::Void:
        default:
            return ' ';
        }
    case OverlayMode::PulseState:
        if (tile.pressure < 1.0)
        {
            return '.';
        }
        if (tile.pressure < 2.0)
        {
            return ':';
        }
        if (tile.pressure < 3.5)
        {
            return 'o';
        }
        if (tile.pressure < 5.0)
        {
            return 'O';
        }
        return '@';
    case OverlayMode::PacketIsolation:
        if (tile.kind == TileKind::VentStack)
        {
            return '^';
        }
        if (tile.kind == TileKind::SpillLip)
        {
            return '_';
        }
        if (tile.kind == TileKind::CaptureBasin || tile.kind == TileKind::IntakePocket || tile.kind == TileKind::PacketRiser)
        {
            return '#';
        }
        return tile.kind == TileKind::Void ? ' ' : '.';
    case OverlayMode::SalinityRisk:
        if (tile.salinity < 0.08)
        {
            return '.';
        }
        if (tile.salinity < 0.2)
        {
            return ':';
        }
        if (tile.salinity < 0.45)
        {
            return 'o';
        }
        if (tile.salinity < 0.75)
        {
            return 'O';
        }
        return '@';
    }

    return '?';
}

RgbColor tile_color(const TileState &tile, OverlayMode overlay, bool selected)
{
    if (selected)
    {
        return {250, 236, 116};
    }

    switch (overlay)
    {
    case OverlayMode::HydraulicState:
        switch (tile.kind)
        {
        case TileKind::SeaReservoir:
            return {78, 144, 220};
        case TileKind::RestrictedInlet:
            return {88, 114, 176};
        case TileKind::ChargeChamber:
            return {94, 174, 202};
        case TileKind::TriggerSiphon:
            return {150, 206, 218};
        case TileKind::PulseChamber:
            return {226, 174, 98};
        case TileKind::Spring:
            return {120, 204, 148};
        case TileKind::IntakePocket:
            return {130, 216, 180};
        case TileKind::PacketRiser:
            return {216, 214, 164};
        case TileKind::CaptureBasin:
            return {188, 226, 150};
        case TileKind::VentStack:
            return {212, 212, 212};
        case TileKind::UpperCistern:
            return {242, 218, 120};
        case TileKind::SpillLip:
            return {204, 126, 92};
        case TileKind::Void:
        default:
            return {52, 58, 66};
        }
    case OverlayMode::PulseState:
        if (tile.pressure < 1.0)
        {
            return {82, 94, 114};
        }
        if (tile.pressure < 2.0)
        {
            return {112, 126, 164};
        }
        if (tile.pressure < 3.5)
        {
            return {152, 166, 214};
        }
        if (tile.pressure < 5.0)
        {
            return {224, 186, 118};
        }
        return {244, 126, 88};
    case OverlayMode::PacketIsolation:
        if (tile.kind == TileKind::VentStack)
        {
            return tile.air_clearance > 0.55 ? RgbColor{174, 226, 176} : RgbColor{212, 118, 96};
        }
        if (tile.kind == TileKind::SpillLip)
        {
            return tile.structural_wear < 0.35 ? RgbColor{206, 204, 132} : RgbColor{196, 106, 90};
        }
        return tile.water_level > 0.1 ? RgbColor{114, 176, 220} : RgbColor{88, 92, 104};
    case OverlayMode::SalinityRisk:
        if (tile.salinity < 0.08)
        {
            return {116, 204, 152};
        }
        if (tile.salinity < 0.2)
        {
            return {186, 214, 136};
        }
        if (tile.salinity < 0.45)
        {
            return {226, 192, 104};
        }
        if (tile.salinity < 0.75)
        {
            return {224, 144, 96};
        }
        return {216, 92, 92};
    }

    return {255, 255, 255};
}

RgbColor tile_background(const TileState &tile, OverlayMode overlay, bool selected)
{
    if (selected)
    {
        return {120, 52, 18};
    }

    switch (overlay)
    {
    case OverlayMode::HydraulicState:
    {
        if (is_sea_side(tile.kind))
        {
            const RgbColor dry = {20, 42, 38};
            const RgbColor wet = {34, 148, 118};
            return blend(dry, wet, std::max(tile.water_level, 0.18));
        }

        if (is_freshwater_side(tile.kind))
        {
            const RgbColor dry = {18, 30, 58};
            const RgbColor wet = {56, 122, 238};
            return blend(dry, wet, std::max(tile.water_level, 0.14));
        }

        return {14, 18, 28};
    }
    case OverlayMode::PulseState:
        return blend({20, 20, 44}, {220, 96, 36}, clamp01(tile.pressure / 5.5));
    case OverlayMode::PacketIsolation:
        if (tile.kind == TileKind::VentStack)
        {
            return blend({68, 26, 24}, {58, 170, 84}, tile.air_clearance);
        }
        if (tile.kind == TileKind::SpillLip)
        {
            return blend({128, 42, 34}, {206, 170, 72}, 1.0 - tile.structural_wear);
        }
        if (is_freshwater_side(tile.kind))
        {
            return blend({34, 32, 58}, {70, 120, 214}, tile.water_level);
        }
        if (is_sea_side(tile.kind))
        {
            return blend({24, 42, 36}, {42, 146, 122}, tile.water_level);
        }
        return {18, 20, 26};
    case OverlayMode::SalinityRisk:
        return blend({26, 68, 82}, {224, 88, 84}, tile.salinity);
    }

    return {14, 18, 28};
}

std::string tile_kind_name(TileKind kind)
{
    switch (kind)
    {
    case TileKind::Void:
        return "Void";
    case TileKind::SeaReservoir:
        return "Sea Reservoir";
    case TileKind::RestrictedInlet:
        return "Restricted Inlet";
    case TileKind::ChargeChamber:
        return "Charge Chamber";
    case TileKind::TriggerSiphon:
        return "Trigger Siphon";
    case TileKind::PulseChamber:
        return "Pulse Chamber";
    case TileKind::Spring:
        return "Fresh Spring";
    case TileKind::IntakePocket:
        return "Intake Pocket";
    case TileKind::PacketRiser:
        return "Packet Riser";
    case TileKind::CaptureBasin:
        return "Capture Basin";
    case TileKind::VentStack:
        return "Vent Stack";
    case TileKind::UpperCistern:
        return "Upper Cistern";
    case TileKind::SpillLip:
        return "Spill Lip";
    }

    return "Unknown";
}

void append_boxed_lines(std::ostringstream &out, const std::string &title, const std::vector<std::string> &lines, int width)
{
    out << title << "\n";
    for (const std::string &line : lines)
    {
        out << pad_ansi(line, width) << "\n";
    }
}

void append_overlay_legend_lines(std::vector<std::string> &info_lines, OverlayMode overlay, int width)
{
    info_lines.push_back(section_bar("OVERLAY", width, {238, 238, 224}, {44, 56, 82}));
    info_lines.push_back(" current " + badge(overlay_name(overlay), {18, 22, 32}, {162, 182, 236}));

    switch (overlay)
    {
    case OverlayMode::HydraulicState:
        info_lines.push_back(" green backgrounds show sea-fed power water, blue backgrounds show freshwater payload");
        info_lines.push_back(" ~ sea   C charge   S siphon   P pulse   i intake   B basin   V vent   U cistern");
        break;
    case OverlayMode::PulseState:
        info_lines.push_back(" . low pressure   : charging   o useful swing   O strong pulse   @ peak discharge");
        break;
    case OverlayMode::PacketIsolation:
        info_lines.push_back(" ^ vent health   _ spill lip health   # packet path   warm colors mean re-coupling risk");
        break;
    case OverlayMode::SalinityRisk:
        info_lines.push_back(" . clean   : trace   o creeping mix   O bad intrusion   @ severe salt contamination");
        break;
    }
}

void draw_map(std::ostringstream &out, const SimulationSnapshot &snapshot, const UiState &ui)
{
    out << section_bar(" TILE VIEW ", 52, {235, 240, 244}, {38, 56, 72}) << "\n";

    for (int y = 0; y < snapshot.height; ++y)
    {
        out << " ";
        for (int x = 0; x < snapshot.width; ++x)
        {
            const TileState &tile = snapshot.tiles[static_cast<std::size_t>(y * snapshot.width + x)];
            const bool selected = x == ui.sample_x && y == ui.sample_y;
            const RgbColor fg = tile_color(tile, ui.overlay, selected);
            const RgbColor bg = tile_background(tile, ui.overlay, selected);
            const char glyph = tile_glyph(tile, ui.overlay);
            out << ansi_bg(bg) << ansi_fg(fg) << ' ' << glyph << reset_ansi();
        }
        out << "\n";
    }
}

void draw_frame(const SimulationSnapshot &snapshot, const UiState &ui)
{
    const int panel_width = 88;
    const TileState &sample = snapshot.tiles[static_cast<std::size_t>(ui.sample_y * snapshot.width + ui.sample_x)];

    std::ostringstream out;
    out << "\x1b[H";
    out << section_bar(" EXPERIMENT 003 :: SEA-FED TWO-STROKE PACKET PUMP ", panel_width + 52, {250, 246, 230}, {24, 52, 74}) << "\n";
    out << color_text(" scenario ", {118, 200, 255})
        << "sea-fed pulse generator driving a two-stage freshwater packet lift through vented isolation and perched capture basins\n\n";

    draw_map(out, snapshot, ui);

    std::vector<std::string> info_lines;
    info_lines.push_back(section_bar("STATUS", panel_width, {238, 238, 224}, {52, 70, 46}));
    info_lines.push_back(
        std::string(" ") + color_text("Tick", {132, 178, 236}) + " " + std::to_string(snapshot.tick) +
        "    " + color_text("Overlay", {132, 178, 236}) + " " + overlay_name(ui.overlay) +
        "    " + color_text("Sim", {132, 178, 236}) + " " + state_badge(ui.running, "RUNNING", "PAUSED"));
    info_lines.push_back(
        std::string(" ") + color_text("Trigger Siphon", {132, 178, 236}) + " " + badge(siphon_state_name(snapshot.siphon_state), {18, 22, 32}, {162, 182, 236}) +
        "    " + color_text("Tick Delay", {132, 178, 236}) + " " + std::to_string(ui.tick_ms) + " ms");
    info_lines.push_back(std::string());

    info_lines.push_back(section_bar("PULSE METRICS", panel_width, {232, 248, 246}, {20, 88, 92}));
    info_lines.push_back(key_value_inline("Sea Head", format_fixed(snapshot.metrics.sea_head), "Charge", format_fixed(snapshot.metrics.charge_level), 16));
    info_lines.push_back(key_value_inline("Pulse", format_fixed(snapshot.metrics.pulse_energy), "Cycle", format_fixed(snapshot.metrics.cycle_progress), 16));
    info_lines.push_back(key_value_inline("Delivered", format_fixed(snapshot.metrics.delivered_this_cycle), "Backflow", format_fixed(snapshot.metrics.lost_to_backflow), 16));
    info_lines.push_back(key_value_inline("Upper Cistern", format_fixed(snapshot.metrics.upper_cistern), "Salt Intrusion", format_fixed(snapshot.metrics.salt_intrusion), 16));
    info_lines.push_back(std::string());

    info_lines.push_back(section_bar("STAGES", panel_width, {250, 246, 232}, {110, 72, 30}));
    for (std::size_t stage_index = 0; stage_index < snapshot.stages.size(); ++stage_index)
    {
        const StageState &stage = snapshot.stages[stage_index];
        info_lines.push_back(
            std::string(" Stage ") + std::to_string(stage_index + 1) + " " + packet_badge(stage.packet_state));
        info_lines.push_back(key_value_inline("Source", format_fixed(stage.source_volume), "Intake", format_fixed(stage.intake_volume), 16));
        info_lines.push_back(key_value_inline("Capture", format_fixed(stage.capture_volume), "Target", format_fixed(stage.packet_target), 16));
        info_lines.push_back(key_value_inline("Vent", format_fixed(stage.vent_clearance), "Spill Lip", format_fixed(stage.spill_lip_integrity), 16));
        info_lines.push_back(key_value_inline("Riser", format_fixed(stage.riser_height), "Salinity", format_fixed(stage.salt_contamination), 16));
    }
    info_lines.push_back(std::string());

    info_lines.push_back(section_bar("SAMPLE TILE", panel_width, {229, 248, 246}, {24, 102, 70}));
    info_lines.push_back(key_value_inline("Position", "(" + std::to_string(ui.sample_x) + "," + std::to_string(ui.sample_y) + ")", "Type", tile_kind_name(sample.kind), 16));
    info_lines.push_back(key_value_inline("Water", format_fixed(sample.water_level), "Pressure", format_fixed(sample.pressure), 16));
    info_lines.push_back(key_value_inline("Salinity", format_fixed(sample.salinity), "Resistance", format_fixed(sample.resistance), 16));
    info_lines.push_back(key_value_inline("Air Clearance", format_fixed(sample.air_clearance), "Wear", format_fixed(sample.structural_wear), 16));
    info_lines.push_back(std::string());

    info_lines.push_back(section_bar("INTERVENTIONS", panel_width, {250, 236, 248}, {112, 44, 96}));
    info_lines.push_back(" c clear sea inlet: faster charge and less damped pulse timing");
    info_lines.push_back(" r repair trigger siphon: better catch and cleaner break");
    info_lines.push_back(" v clear vent stacks: stronger packet isolation between stages");
    info_lines.push_back(" l repair spill lips: better perched retention after lift");
    info_lines.push_back(" z reset scenario");
    info_lines.push_back(std::string());

    append_overlay_legend_lines(info_lines, ui.overlay, panel_width);
    info_lines.push_back(std::string());

    info_lines.push_back(section_bar("RECENT EVENTS", panel_width, {248, 242, 224}, {74, 74, 74}));
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

    info_lines.push_back(section_bar("CONTROLS", panel_width, {248, 242, 224}, {74, 74, 74}));
    info_lines.push_back(" sim        space play/pause   n single-step   -/+ speed");
    info_lines.push_back(" inspect    arrow keys move selected tile");
    info_lines.push_back(" overlays   1 hydraulic  2 pulse  3 isolation  4 salinity");
    info_lines.push_back(" actions    c clear inlet   r repair siphon   v clear vents   l repair spill lips   z reset");
    info_lines.push_back(" shell      q quit");

    append_boxed_lines(
        out,
        ansi_bg({18, 24, 36}) + ansi_fg({232, 240, 248}) + pad_right(" STATUS / DIAGNOSTICS ", panel_width) + reset_ansi(),
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
        ui.overlay = OverlayMode::PulseState;
        return;
    case '3':
        ui.overlay = OverlayMode::PacketIsolation;
        return;
    case '4':
        ui.overlay = OverlayMode::SalinityRisk;
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
        simulator.apply_intervention(InterventionType::ClearSeaInlet);
        return;
    case 'r':
    case 'R':
        simulator.apply_intervention(InterventionType::RepairTriggerSiphon);
        return;
    case 'v':
    case 'V':
        simulator.apply_intervention(InterventionType::ClearVentStack);
        return;
    case 'l':
    case 'L':
        simulator.apply_intervention(InterventionType::RepairSpillLip);
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
    ui.tick_ms = options.sleep_ms;

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
    ui.tick_ms = options.sleep_ms;

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
} // namespace sea_fed_packet_pump_003
