#include "console_ui.h"

#define NOMINMAX
#include <Windows.h>
#include <conio.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace agricultural_encounter_001
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

[[nodiscard]] float clamp01(float value)
{
    return std::clamp(value, 0.0F, 1.0F);
}

[[nodiscard]] std::string format_float(float value)
{
    std::ostringstream out;
    out << std::fixed << std::setprecision(3) << value;
    return out.str();
}

[[nodiscard]] std::string reset_ansi()
{
    return "\x1b[0m";
}

[[nodiscard]] std::string pad_right(const std::string &text, int width)
{
    if (static_cast<int>(text.size()) >= width)
    {
        return text.substr(0, static_cast<std::size_t>(width));
    }

    return text + std::string(static_cast<std::size_t>(width - static_cast<int>(text.size())), ' ');
}

[[nodiscard]] int visible_length(const std::string &text)
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

[[nodiscard]] std::string pad_ansi(const std::string &text, int width)
{
    const int length = visible_length(text);
    if (length >= width)
    {
        return text;
    }

    return text + std::string(static_cast<std::size_t>(width - length), ' ');
}

[[nodiscard]] RgbColor blend(RgbColor a, RgbColor b, float t)
{
    const float clamped = clamp01(t);
    const auto channel = [clamped](int start, int end) {
        return static_cast<int>(std::round(static_cast<float>(start) + (static_cast<float>(end - start) * clamped)));
    };

    return {
        channel(a.red, b.red),
        channel(a.green, b.green),
        channel(a.blue, b.blue),
    };
}

[[nodiscard]] std::string ansi_bg(RgbColor color)
{
    std::ostringstream out;
    out << "\x1b[48;2;" << color.red << ';' << color.green << ';' << color.blue << 'm';
    return out.str();
}

[[nodiscard]] std::string ansi_fg(RgbColor color)
{
    std::ostringstream out;
    out << "\x1b[38;2;" << color.red << ';' << color.green << ';' << color.blue << 'm';
    return out.str();
}

[[nodiscard]] std::string color_text(const std::string &text, RgbColor color)
{
    return ansi_fg(color) + text + reset_ansi();
}

[[nodiscard]] std::string badge(const std::string &text, RgbColor fg, RgbColor bg)
{
    return ansi_bg(bg) + ansi_fg(fg) + ' ' + text + ' ' + reset_ansi();
}

[[nodiscard]] std::string section_bar(const std::string &text, int width, RgbColor fg, RgbColor bg)
{
    const std::string padded = pad_right(" " + text + " ", width);
    return ansi_bg(bg) + ansi_fg(fg) + padded + reset_ansi();
}

[[nodiscard]] std::string key_value(const std::string &key, const std::string &value, int key_width)
{
    return " " + color_text(pad_right(key, key_width), {132, 178, 236}) + " " + value;
}

[[nodiscard]] std::string key_value_inline(
    const std::string &left_key,
    const std::string &left_value,
    const std::string &right_key,
    const std::string &right_value,
    int key_width)
{
    return key_value(left_key, left_value, key_width) + "    " + key_value(right_key, right_value, key_width);
}

[[nodiscard]] char saturation_glyph(float saturation)
{
    if (saturation < 0.15F)
    {
        return '.';
    }
    if (saturation < 0.30F)
    {
        return ':';
    }
    if (saturation < 0.45F)
    {
        return 'o';
    }
    if (saturation < 0.60F)
    {
        return 'O';
    }
    if (saturation < 0.80F)
    {
        return '0';
    }
    return '@';
}

[[nodiscard]] char elevation_glyph(float elevation)
{
    if (elevation < 0.20F)
    {
        return '0';
    }
    if (elevation < 0.35F)
    {
        return '1';
    }
    if (elevation < 0.50F)
    {
        return '2';
    }
    if (elevation < 0.65F)
    {
        return '3';
    }
    return '4';
}

[[nodiscard]] RgbColor saturation_background(float saturation)
{
    const RgbColor dry{92, 66, 42};
    const RgbColor mid{124, 158, 86};
    const RgbColor wet{44, 110, 190};

    if (saturation < 0.5F)
    {
        return blend(dry, mid, saturation / 0.5F);
    }

    return blend(mid, wet, (saturation - 0.5F) / 0.5F);
}

[[nodiscard]] RgbColor elevation_background(float elevation)
{
    const RgbColor low{34, 58, 84};
    const RgbColor high{194, 182, 138};
    return blend(low, high, elevation);
}
[[nodiscard]] RgbColor soil_background(SoilType soil)
{
    switch (soil)
    {
    case SoilType::Clay:
        return {126, 92, 74};
    case SoilType::Loam:
        return {92, 124, 78};
    }
    return {80, 80, 80};
}

[[nodiscard]] RgbColor land_quality_background(const EncounterRuntime &runtime, const TileState &tile)
{
    const float normalized = clamp01(
        (tile.saturation - runtime.tuning.cultivable_min_saturation) /
        std::max(0.01F, runtime.tuning.cultivable_max_saturation - runtime.tuning.cultivable_min_saturation));

    const RgbColor too_dry{140, 105, 70};
    const RgbColor cultivable{103, 154, 86};
    const RgbColor waterlogged{50, 112, 188};
    RgbColor color = normalized < 0.5F
        ? blend(too_dry, cultivable, normalized / 0.5F)
        : blend(cultivable, waterlogged, (normalized - 0.5F) / 0.5F);

    if (!tile.target_zone)
    {
        color = blend(color, {38, 38, 38}, 0.60F);
    }

    return color;
}

[[nodiscard]] RgbColor overlay_background(const EncounterRuntime &runtime, const UiState &ui, const TileState &tile)
{
    switch (ui.overlay)
    {
    case OverlayMode::LandQuality:
    case OverlayMode::Infrastructure:
    case OverlayMode::TargetZone:
        return land_quality_background(runtime, tile);
    case OverlayMode::Saturation:
        return saturation_background(tile.saturation);
    case OverlayMode::Elevation:
        return elevation_background(tile.elevation);
    case OverlayMode::SoilType:
        return soil_background(tile.soil);
    }

    return {30, 30, 30};
}

[[nodiscard]] RgbColor glyph_color(const TileState &tile, const TileQuality &quality, OverlayMode overlay)
{
    switch (overlay)
    {
    case OverlayMode::LandQuality:
        if (quality.condition == LandCondition::Waterlogged)
        {
            return {240, 248, 255};
        }
        if (quality.condition == LandCondition::TooDry)
        {
            return tile.target_zone ? RgbColor{44, 28, 12} : RgbColor{188, 188, 188};
        }
        if (quality.trend == MoistureTrend::Wetting)
        {
            return {244, 252, 255};
        }
        if (quality.trend == MoistureTrend::Drying)
        {
            return {52, 38, 20};
        }
        return {16, 42, 16};
    case OverlayMode::Saturation:
        return {242, 247, 255};
    case OverlayMode::Elevation:
        return {18, 20, 24};
    case OverlayMode::SoilType:
        return {244, 238, 228};
    case OverlayMode::TargetZone:
        return tile.target_zone ? RgbColor{232, 255, 232} : RgbColor{214, 214, 214};
    case OverlayMode::Infrastructure:
        if (tile.buried_drain_open)
        {
            return {190, 234, 255};
        }
        if (tile.buried_drain)
        {
            return {144, 196, 255};
        }
        if (tile.modern_berm)
        {
            return {255, 222, 148};
        }
        if (tile.inflow_bias > 0.01F)
        {
            return {255, 188, 168};
        }
        return {40, 40, 40};
    }

    return {245, 245, 245};
}

[[nodiscard]] char overlay_glyph(const EncounterRuntime &, const UiState &ui, const TileState &tile, const TileQuality &quality)
{
    switch (ui.overlay)
    {
    case OverlayMode::LandQuality:
        if (!tile.target_zone)
        {
            return '.';
        }
        if (quality.condition == LandCondition::Waterlogged)
        {
            return '~';
        }
        if (quality.condition == LandCondition::TooDry)
        {
            return '.';
        }
        if (quality.trend == MoistureTrend::Wetting)
        {
            return '+';
        }
        if (quality.trend == MoistureTrend::Drying)
        {
            return '-';
        }
        return ' ';
    case OverlayMode::Saturation:
        return saturation_glyph(tile.saturation);
    case OverlayMode::Elevation:
        return elevation_glyph(tile.elevation);
    case OverlayMode::SoilType:
        return tile.soil == SoilType::Clay ? 'C' : 'L';
    case OverlayMode::TargetZone:
        return tile.target_zone ? 'T' : '.';
    case OverlayMode::Infrastructure:
        if (tile.buried_drain_open)
        {
            return 'D';
        }
        if (tile.buried_drain)
        {
            return 'd';
        }
        if (tile.modern_berm)
        {
            return 'B';
        }
        if (tile.inflow_bias > 0.01F)
        {
            return 'S';
        }
        if (!tile.target_zone)
        {
            return '.';
        }
        if (quality.condition == LandCondition::Waterlogged)
        {
            return '~';
        }
        if (quality.condition == LandCondition::TooDry)
        {
            return '.';
        }
        return ' ';
    }

    return '?';
}

[[nodiscard]] std::string render_tile(const EncounterRuntime &runtime, const UiState &ui, int x, int y)
{
    const TileState &tile = runtime.current.at(x, y);
    const TileQuality &quality = runtime.qualities.at(static_cast<std::size_t>(y * runtime.current.width() + x));

    RgbColor bg = overlay_background(runtime, ui, tile);
    RgbColor fg = glyph_color(tile, quality, ui.overlay);
    const char glyph = overlay_glyph(runtime, ui, tile, quality);
    const bool is_selected = (x == ui.sample_x && y == ui.sample_y);

    if (is_selected)
    {
        bg = blend(bg, {245, 245, 245}, 0.35F);
        fg = {12, 12, 12};
    }

    std::ostringstream out;
    out << ansi_bg(bg) << ansi_fg(fg);
    if (is_selected)
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

[[nodiscard]] std::string color_swatch(const std::string &label, RgbColor bg, RgbColor fg = {240, 244, 248})
{
    return ansi_bg(bg) + ansi_fg(fg) + " " + label + " " + reset_ansi();
}

[[nodiscard]] TileState preview_tile(float saturation, bool target_zone)
{
    TileState tile;
    tile.saturation = saturation;
    tile.target_zone = target_zone;
    return tile;
}

void append_overlay_legend_lines(std::vector<std::string> &lines, const EncounterRuntime &runtime, OverlayMode overlay)
{
    lines.push_back(section_bar("ACTIVE FIELD LEGEND", 112, {242, 238, 224}, {56, 56, 56}));
    lines.push_back(" overlay    " + color_text(overlay_name(overlay), {232, 238, 246}));

    const float midpoint = (runtime.tuning.cultivable_min_saturation + runtime.tuning.cultivable_max_saturation) * 0.5F;

    switch (overlay)
    {
    case OverlayMode::LandQuality:
        lines.push_back(" symbols    dry='.'  stable='blank'  wetting='+'  drying='-'  waterlogged='~'");
        lines.push_back(
            " colors     " +
            color_swatch("TOO DRY", land_quality_background(runtime, preview_tile(runtime.tuning.cultivable_min_saturation - 0.10F, true)), {32, 20, 12}) +
            "  " + color_swatch("CULTIVABLE", land_quality_background(runtime, preview_tile(midpoint, true)), {16, 42, 16}) +
            "  " + color_swatch("WET", land_quality_background(runtime, preview_tile(runtime.tuning.cultivable_max_saturation + 0.12F, true)), {240, 248, 255}));
        lines.push_back(" meaning    background shows moisture band relative to cultivable range");
        break;
    case OverlayMode::Saturation:
        lines.push_back(" symbols    . : o O 0 @  (increasing saturation)");
        lines.push_back(
            " colors     " +
            color_swatch("DRY", saturation_background(0.05F), {244, 236, 224}) +
            "  " + color_swatch("MID", saturation_background(0.50F), {18, 28, 18}) +
            "  " + color_swatch("WET", saturation_background(0.95F), {242, 247, 255}));
        lines.push_back(" meaning    background is raw saturation value");
        break;
    case OverlayMode::Elevation:
        lines.push_back(" symbols    0 1 2 3 4  (low to high)");
        lines.push_back(
            " colors     " +
            color_swatch("LOW", elevation_background(0.05F), {230, 238, 246}) +
            "  " + color_swatch("MID", elevation_background(0.50F), {28, 30, 34}) +
            "  " + color_swatch("HIGH", elevation_background(0.95F), {28, 24, 18}));
        lines.push_back(" meaning    background is local terrain elevation");
        break;
    case OverlayMode::SoilType:
        lines.push_back(" symbols    C = clay   L = loam");
        lines.push_back(
            " colors     " +
            color_swatch("CLAY", soil_background(SoilType::Clay), {248, 240, 232}) +
            "  " + color_swatch("LOAM", soil_background(SoilType::Loam), {232, 244, 228}));
        lines.push_back(" meaning    background shows the substrate that controls baseline drainage");
        break;
    case OverlayMode::TargetZone:
        lines.push_back(" symbols    T = target tile   . = outside target zone");
        lines.push_back(
            " colors     " +
            color_swatch("OUTSIDE", land_quality_background(runtime, preview_tile(midpoint, false)), {214, 214, 214}) +
            "  " + color_swatch("TARGET", land_quality_background(runtime, preview_tile(midpoint, true)), {16, 42, 16}));
        lines.push_back(" meaning    background still shows land quality while symbols mark the work area");
        break;
    case OverlayMode::Infrastructure:
        lines.push_back(" symbols    S seep   d buried drain   D open drain   B berm");
        lines.push_back(
            " colors     " +
            color_swatch("SEEP", land_quality_background(runtime, preview_tile(runtime.tuning.cultivable_max_saturation + 0.08F, true)), {255, 188, 168}) +
            "  " + color_swatch("DRAIN", land_quality_background(runtime, preview_tile(runtime.tuning.cultivable_min_saturation + 0.05F, true)), {190, 234, 255}) +
            "  " + color_swatch("BERM", land_quality_background(runtime, preview_tile(runtime.tuning.cultivable_min_saturation + 0.02F, true)), {255, 222, 148}));
        lines.push_back(" meaning    background stays on land quality; symbols reveal structures and hazards");
        break;
    }

    lines.push_back(" overlays   1 quality  2 saturation  3 elevation  4 soil  5 target  6 infrastructure");
    lines.push_back(std::string());
}

void draw_frame(const EncounterRuntime &runtime, const UiState &ui)
{
    const int field_inner_width = runtime.current.width() * 2;
    const int panel_width = 112;
    const int sample_x = std::clamp(ui.sample_x, 0, runtime.current.width() - 1);
    const int sample_y = std::clamp(ui.sample_y, 0, runtime.current.height() - 1);
    const TileState &sample = runtime.current.at(sample_x, sample_y);
    const TileQuality &sample_quality = runtime.qualities.at(static_cast<std::size_t>(sample_y * runtime.current.width() + sample_x));
    auto parameters = editable_parameters(const_cast<EncounterRuntime &>(runtime));

    const auto paused_badge = badge("PAUSED", {32, 24, 10}, {214, 178, 76});
    const auto running_badge = badge("RUNNING", {10, 24, 10}, {94, 184, 94});
    const auto ok_badge = badge("OK", {10, 22, 10}, {108, 196, 108});
    const auto no_badge = badge("--", {28, 28, 28}, {120, 120, 120});
    const auto success_badge = badge("SUCCESS", {10, 22, 10}, {98, 188, 110});
    const auto notyet_badge = badge("NOT YET", {34, 24, 10}, {189, 140, 70});

    std::ostringstream out;
    out << "\x1b[H";

    append_boxed_lines(
        out,
        ansi_bg({26, 40, 58}) + ansi_fg({232, 238, 246}) + pad_right(" AGRICULTURAL ENCOUNTER 001 :: WATERLOGGED TERRACE ", panel_width) + reset_ansi(),
        {
            std::string(" ") + color_text("STATE", {132, 178, 236}) + "  " + (ui.running ? running_badge : paused_badge) +
                "    " + color_text("STEP", {132, 178, 236}) + " " + std::to_string(runtime.step) +
                "    " + color_text("OVERLAY", {132, 178, 236}) + " " + overlay_name(ui.overlay) +
                "    " + color_text("TICK", {132, 178, 236}) + " " + std::to_string(ui.tick_ms) + "ms",
            std::string(" ") + color_text("GOAL", {132, 178, 236}) +
                "  cultivable>=" + std::to_string(runtime.goal.min_cultivable_tiles) +
                "  patch>=" + std::to_string(runtime.goal.min_largest_patch) +
                "  stable>=" + std::to_string(runtime.goal.min_stable_turns),
        },
        panel_width);

    out << '+' << std::string(static_cast<std::size_t>(field_inner_width), '-') << "+\n";
    out << '|' << pad_ansi(
        ansi_bg({32, 42, 54}) + ansi_fg({224, 230, 236}) +
        pad_right(" FIELD :: " + std::string(overlay_name(ui.overlay)) + " :: BACKGROUND=CONTINUOUS / SYMBOLS=ITEMS ", field_inner_width) +
        reset_ansi(),
        field_inner_width) << "|\n";
    out << '+' << std::string(static_cast<std::size_t>(field_inner_width), '-') << "+\n";
    for (int y = 0; y < runtime.current.height(); ++y)
    {
        out << '|';
        for (int x = 0; x < runtime.current.width(); ++x)
        {
            out << render_tile(runtime, ui, x, y);
        }
        out << "|\n";
    }
    out << '+' << std::string(static_cast<std::size_t>(field_inner_width), '-') << "+\n";

    std::vector<std::string> info_lines;
    info_lines.push_back(section_bar("SITE METRICS", panel_width, {245, 232, 194}, {74, 58, 26}));
    info_lines.push_back(key_value_inline("Cultivable Tiles", std::to_string(runtime.metrics.cultivable_tile_count), "Largest Patch", std::to_string(runtime.metrics.largest_cultivable_patch), 17));
    info_lines.push_back(key_value_inline("Patch Count", std::to_string(runtime.metrics.cultivable_patch_count), "Stable Turns", std::to_string(runtime.goal_status.stable_turns), 17));
    info_lines.push_back(std::string());

    info_lines.push_back(section_bar("GOAL STATUS", panel_width, {231, 238, 247}, {34, 62, 88}));
    info_lines.push_back(
        std::string(" ") + color_text("Area", {132, 178, 236}) + " " + (runtime.goal_status.area_met ? ok_badge : no_badge) +
        "    " + color_text("Contiguity", {132, 178, 236}) + " " + (runtime.goal_status.contiguity_met ? ok_badge : no_badge) +
        "    " + color_text("Persistence", {132, 178, 236}) + " " + (runtime.goal_status.persistence_met ? ok_badge : no_badge) +
        "    " + color_text("Overall", {132, 178, 236}) + " " + (runtime.goal_status.overall_success ? success_badge : notyet_badge));
    info_lines.push_back(std::string());

    info_lines.push_back(section_bar("SAMPLE TILE", panel_width, {229, 241, 232}, {34, 74, 46}));
    info_lines.push_back(key_value_inline("Position", "(" + std::to_string(sample_x) + "," + std::to_string(sample_y) + ")", "Soil", soil_name(sample.soil), 17));
    info_lines.push_back(key_value_inline("Saturation", format_float(sample.saturation), "Elevation", format_float(sample.elevation), 17));
    info_lines.push_back(key_value_inline("Inflow", format_float(sample.inflow_bias), "Drain", format_float(sample.drain_bias), 17));
    info_lines.push_back(key_value_inline("Condition", condition_name(sample_quality.condition), "Trend", trend_name(sample_quality.trend), 17));
    info_lines.push_back(key_value("Cultivable", sample_quality.cultivable ? badge("YES", {8, 20, 8}, {112, 198, 112}) : badge("NO", {32, 20, 20}, {182, 110, 110}), 17));
    info_lines.push_back(std::string());

    info_lines.push_back(section_bar("TUNING", panel_width, {243, 232, 247}, {72, 42, 82}));
    for (std::size_t i = 0; i < parameters.size(); ++i)
    {
        const std::string selector = static_cast<int>(i) == ui.selected_parameter
            ? badge("ACTIVE", {14, 20, 30}, {144, 196, 255})
            : badge("      ", {32, 32, 32}, {58, 58, 58});
        info_lines.push_back(
            std::string(" ") + selector + "  " +
            color_text(pad_right(parameters[i].label, 20), {132, 178, 236}) +
            "  " + badge(format_float(*parameters[i].value), {240, 244, 248}, {56, 64, 74}));
    }
    info_lines.push_back(std::string());

    append_overlay_legend_lines(info_lines, runtime, ui.overlay);

    info_lines.push_back(section_bar("CONTROLS", panel_width, {242, 238, 224}, {56, 56, 56}));
    info_lines.push_back(" sim        space play/pause   n single-step   -/+ speed");
    info_lines.push_back(" tuning     tab next parameter   shift+tab previous   [ ] adjust selected");
    info_lines.push_back(" inspect    arrow keys move active sample tile");
    info_lines.push_back(" overlays   1 quality  2 saturation  3 elevation  4 soil  5 target  6 infrastructure");
    info_lines.push_back(" actions    p/o/c/r apply around selected tile (radius 2 falloff)   z reset");
    info_lines.push_back(" shell      q quit");

    append_boxed_lines(
        out,
        ansi_bg({28, 34, 42}) + ansi_fg({220, 228, 236}) + pad_right(" STATUS / DIAGNOSTICS ", panel_width) + reset_ansi(),
        info_lines,
        panel_width);
    out << "\x1b[J";

    std::cout << out.str() << std::flush;
}

void adjust_selected_parameter(EncounterRuntime &runtime, UiState &ui, float direction)
{
    auto parameters = editable_parameters(runtime);
    if (parameters.empty())
    {
        return;
    }

    ParameterInfo selected = parameters.at(static_cast<std::size_t>(ui.selected_parameter));
    *selected.value = std::clamp(
        *selected.value + (selected.step * direction),
        selected.min_value,
        selected.max_value);
}

void handle_keypress(int key, EncounterRuntime &runtime, UiState &ui)
{
    ui.needs_redraw = true;

    if (key == 0 || key == 224)
    {
        const int extended = _getch();
        switch (extended)
        {
        case 15:
        {
            auto parameters = editable_parameters(runtime);
            if (!parameters.empty())
            {
                ui.selected_parameter =
                    (ui.selected_parameter + static_cast<int>(parameters.size()) - 1) %
                    static_cast<int>(parameters.size());
            }
            return;
        }
        case 72:
            ui.sample_y = (std::max)(0, ui.sample_y - 1);
            return;
        case 80:
            ui.sample_y = (std::min)(runtime.current.height() - 1, ui.sample_y + 1);
            return;
        case 75:
            ui.sample_x = (std::max)(0, ui.sample_x - 1);
            return;
        case 77:
            ui.sample_x = (std::min)(runtime.current.width() - 1, ui.sample_x + 1);
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
        simulate_runtime_step(runtime);
        return;
    case '1':
        ui.overlay = OverlayMode::LandQuality;
        return;
    case '2':
        ui.overlay = OverlayMode::Saturation;
        return;
    case '3':
        ui.overlay = OverlayMode::Elevation;
        return;
    case '4':
        ui.overlay = OverlayMode::SoilType;
        return;
    case '5':
        ui.overlay = OverlayMode::TargetZone;
        return;
    case '6':
        ui.overlay = OverlayMode::Infrastructure;
        return;
    case '\t':
    {
        auto parameters = editable_parameters(runtime);
        if (!parameters.empty())
        {
            ui.selected_parameter =
                (ui.selected_parameter + 1) % static_cast<int>(parameters.size());
        }
        return;
    }
    case '[':
        adjust_selected_parameter(runtime, ui, -1.0F);
        return;
    case ']':
        adjust_selected_parameter(runtime, ui, 1.0F);
        return;
    case '-':
        ui.tick_ms = (std::min)(1000, ui.tick_ms + 20);
        return;
    case '+':
    case '=':
        ui.tick_ms = (std::max)(20, ui.tick_ms - 20);
        return;
    case 'z':
    case 'Z':
        reset_runtime(runtime);
        return;
    case 'p':
    case 'P':
        apply_intervention(runtime, InterventionType::PatchWesternSeep, ui.sample_x, ui.sample_y);
        return;
    case 'o':
    case 'O':
        apply_intervention(runtime, InterventionType::OpenBuriedDrain, ui.sample_x, ui.sample_y);
        return;
    case 'c':
    case 'C':
        apply_intervention(runtime, InterventionType::CutReliefChannel, ui.sample_x, ui.sample_y);
        return;
    case 'r':
    case 'R':
        apply_intervention(runtime, InterventionType::RaiseCentralSpine, ui.sample_x, ui.sample_y);
        return;
    default:
        return;
    }
}
} // namespace

void run_console_ui()
{
    AlternateScreenSession terminal_session;
    EncounterRuntime runtime;
    UiState ui;
    ui.sample_x = runtime.current.width() / 2;
    ui.sample_y = runtime.current.height() / 2;

    auto next_tick = std::chrono::steady_clock::now();

    while (!ui.quit_requested)
    {
        while (_kbhit())
        {
            handle_keypress(_getch(), runtime, ui);
        }

        const auto now = std::chrono::steady_clock::now();
        if (ui.running && now >= next_tick)
        {
            simulate_runtime_step(runtime);
            ui.needs_redraw = true;
            next_tick = now + std::chrono::milliseconds(ui.tick_ms);
        }

        if (ui.needs_redraw)
        {
            draw_frame(runtime, ui);
            ui.needs_redraw = false;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}
} // namespace agricultural_encounter_001




