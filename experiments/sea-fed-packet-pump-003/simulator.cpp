#include "simulator.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <utility>

namespace sea_fed_packet_pump_003
{
namespace
{
constexpr int event_limit = 10;

int index_of(int width, int x, int y)
{
    return y * width + x;
}

double clamp01(double value)
{
    return std::clamp(value, 0.0, 1.0);
}

double lerp(double a, double b, double t)
{
    return a + (b - a) * t;
}
} // namespace

Simulator::Simulator()
{
    reset();
}

void Simulator::reset()
{
    tick_ = 0;
    tiles_.assign(static_cast<std::size_t>(width_ * height_), {});
    metrics_ = PumpMetrics{};
    siphon_state_ = SiphonState::Charging;
    recent_events_.clear();

    metrics_.sea_head = 6.0;
    metrics_.charge_level = 1.6;
    metrics_.pulse_energy = 0.2;
    metrics_.cycle_progress = 0.15;
    metrics_.upper_cistern = 1.2;

    stages_[0] = StageState{
        .source_volume = 3.6,
        .intake_volume = 0.25,
        .capture_volume = 0.8,
        .riser_height = 1.4,
        .packet_target = 0.9,
        .vent_clearance = 0.72,
        .spill_lip_integrity = 0.76,
        .salt_contamination = 0.08,
        .packet_state = PacketState::Filling,
    };

    stages_[1] = StageState{
        .source_volume = 0.5,
        .intake_volume = 0.08,
        .capture_volume = 0.45,
        .riser_height = 1.7,
        .packet_target = 0.85,
        .vent_clearance = 0.61,
        .spill_lip_integrity = 0.68,
        .salt_contamination = 0.05,
        .packet_state = PacketState::Empty,
    };

    build_layout();
    update_tiles();
    push_event("Packet pump reset. Sea charge is building toward the next siphon catch.");
}

void Simulator::step()
{
    ++tick_;
    decay_cycle_metrics();
    update_charge_cycle();
    run_packet_cycle();
    update_tiles();
}

void Simulator::apply_intervention(InterventionType intervention)
{
    switch (intervention)
    {
    case InterventionType::ClearSeaInlet:
        for (int y = 2; y <= 4; ++y)
        {
            TileState &tile = tiles_[index_of(width_, 2, y)];
            tile.resistance = std::max(0.15, tile.resistance - 0.22);
            tile.structural_wear = std::max(0.05, tile.structural_wear - 0.06);
        }
        metrics_.sea_head = std::min(7.0, metrics_.sea_head + 0.3);
        push_event("Sea inlet cleared. Charge chamber fill is less damped and pulse timing should sharpen.");
        break;

    case InterventionType::RepairTriggerSiphon:
        for (int x = 6; x <= 8; ++x)
        {
            TileState &tile = tiles_[index_of(width_, x, 2)];
            tile.air_clearance = std::min(1.0, tile.air_clearance + 0.24);
            tile.structural_wear = std::max(0.02, tile.structural_wear - 0.12);
        }
        metrics_.pulse_energy = std::min(1.0, metrics_.pulse_energy + 0.16);
        push_event("Trigger siphon repaired. Catch and break behavior should become more decisive.");
        break;

    case InterventionType::ClearVentStack:
        for (StageState &stage : stages_)
        {
            stage.vent_clearance = std::min(1.0, stage.vent_clearance + 0.2);
        }
        push_event("Vent stacks cleared. Packet isolation should improve after each lift.");
        break;

    case InterventionType::RepairSpillLip:
        for (StageState &stage : stages_)
        {
            stage.spill_lip_integrity = std::min(1.0, stage.spill_lip_integrity + 0.18);
        }
        push_event("Spill lips restored. Perched basins should hold gained elevation more reliably.");
        break;
    }

    update_tiles();
}

SimulationSnapshot Simulator::snapshot() const
{
    return SimulationSnapshot{
        .tick = tick_,
        .width = width_,
        .height = height_,
        .tiles = tiles_,
        .stages = stages_,
        .metrics = metrics_,
        .siphon_state = siphon_state_,
        .recent_events = recent_events_,
    };
}

void Simulator::build_layout()
{
    auto set_tile = [&](int x, int y, TileKind kind, double resistance, double wear, double air) {
        TileState &tile = tiles_[index_of(width_, x, y)];
        tile.kind = kind;
        tile.resistance = resistance;
        tile.structural_wear = wear;
        tile.air_clearance = air;
    };

    for (int y = 1; y <= 6; ++y)
    {
        set_tile(1, y, TileKind::SeaReservoir, 0.1, 0.05, 0.0);
    }

    set_tile(2, 3, TileKind::RestrictedInlet, 0.62, 0.22, 0.05);
    set_tile(3, 3, TileKind::ChargeChamber, 0.35, 0.12, 0.15);
    set_tile(4, 3, TileKind::ChargeChamber, 0.35, 0.12, 0.15);
    set_tile(5, 3, TileKind::TriggerSiphon, 0.3, 0.18, 0.45);
    set_tile(6, 2, TileKind::TriggerSiphon, 0.32, 0.2, 0.42);
    set_tile(7, 2, TileKind::TriggerSiphon, 0.28, 0.16, 0.48);
    set_tile(8, 2, TileKind::TriggerSiphon, 0.34, 0.18, 0.44);
    set_tile(7, 3, TileKind::PulseChamber, 0.24, 0.1, 0.22);

    set_tile(9, 4, TileKind::Spring, 0.12, 0.08, 0.0);
    set_tile(10, 4, TileKind::IntakePocket, 0.18, 0.09, 0.15);
    set_tile(11, 3, TileKind::PacketRiser, 0.33, 0.14, 0.1);
    set_tile(12, 2, TileKind::CaptureBasin, 0.2, 0.11, 0.2);
    set_tile(12, 3, TileKind::VentStack, 0.18, 0.12, 0.65);
    set_tile(13, 2, TileKind::SpillLip, 0.22, 0.2, 0.14);

    set_tile(10, 5, TileKind::IntakePocket, 0.24, 0.12, 0.12);
    set_tile(11, 4, TileKind::PacketRiser, 0.38, 0.16, 0.08);
    set_tile(12, 4, TileKind::CaptureBasin, 0.25, 0.12, 0.2);
    set_tile(12, 5, TileKind::VentStack, 0.22, 0.14, 0.58);
    set_tile(13, 4, TileKind::SpillLip, 0.26, 0.22, 0.12);
    set_tile(14, 2, TileKind::UpperCistern, 0.16, 0.08, 0.16);
}

void Simulator::update_charge_cycle()
{
    const double inlet_resistance = tiles_[index_of(width_, 2, 3)].resistance;
    const double inlet_factor = std::max(0.18, 1.0 - inlet_resistance);
    const double charge_gain = 0.16 * inlet_factor + 0.045 * (metrics_.sea_head / 6.0);
    const double siphon_air = (tiles_[index_of(width_, 6, 2)].air_clearance +
                               tiles_[index_of(width_, 7, 2)].air_clearance +
                               tiles_[index_of(width_, 8, 2)].air_clearance) /
                              3.0;

    switch (siphon_state_)
    {
    case SiphonState::Idle:
    case SiphonState::Charging:
        siphon_state_ = SiphonState::Charging;
        metrics_.charge_level = std::min(4.2, metrics_.charge_level + charge_gain);
        metrics_.cycle_progress = clamp01(metrics_.charge_level / 3.2);

        if (metrics_.charge_level >= 3.2)
        {
            if (siphon_air < 0.3)
            {
                siphon_state_ = SiphonState::Misfiring;
                metrics_.pulse_energy = std::max(metrics_.pulse_energy, 0.28);
                push_event("Trigger siphon reached crest but misfired. Air clearance is too poor for a clean catch.");
            }
            else
            {
                siphon_state_ = SiphonState::Triggered;
                metrics_.pulse_energy = std::min(1.0, 0.62 + 0.5 * siphon_air);
                push_event("Trigger siphon caught. Slow sea charge is converting into a discharge pulse.");
            }
        }
        break;

    case SiphonState::Triggered:
        metrics_.charge_level = std::max(0.6, metrics_.charge_level - (1.3 + metrics_.pulse_energy));
        metrics_.cycle_progress = 1.0;
        siphon_state_ = SiphonState::Recovering;
        break;

    case SiphonState::Recovering:
        metrics_.charge_level = std::max(0.45, metrics_.charge_level - 0.2);
        metrics_.pulse_energy *= 0.72;
        metrics_.cycle_progress = std::max(0.0, metrics_.cycle_progress - 0.35);
        if (metrics_.charge_level <= 0.7)
        {
            siphon_state_ = SiphonState::Charging;
            metrics_.cycle_progress = 0.1;
            push_event("Charge chamber reset. The next slow fill cycle is beginning.");
        }
        break;

    case SiphonState::Misfiring:
        metrics_.charge_level = std::max(1.2, metrics_.charge_level - 0.35);
        metrics_.pulse_energy *= 0.58;
        metrics_.cycle_progress = 0.55;
        siphon_state_ = SiphonState::Charging;
        push_event("Misfire dissipated. The chamber kept some charge, but the pulse was weak and untidy.");
        break;
    }
}

void Simulator::run_packet_cycle()
{
    metrics_.delivered_this_cycle = 0.0;
    metrics_.lost_to_backflow = 0.0;

    StageState &first = stages_[0];
    StageState &second = stages_[1];
    second.source_volume = first.capture_volume;

    const bool strong_pulse = metrics_.pulse_energy > 0.55;
    const bool weak_pulse = metrics_.pulse_energy > 0.18;

    for (std::size_t index = 0; index < stages_.size(); ++index)
    {
        StageState &stage = stages_[index];
        const double source_available = std::max(0.0, stage.source_volume);
        const double intake_capacity = std::max(0.0, stage.packet_target - stage.intake_volume);
        const double intake_gain = weak_pulse ? std::min(intake_capacity, 0.28 * metrics_.pulse_energy + 0.08) : 0.0;

        if (intake_gain > 0.01 && source_available > 0.05)
        {
            stage.intake_volume += std::min(source_available * 0.18, intake_gain);
            stage.packet_state = PacketState::Filling;
        }

        if (!strong_pulse)
        {
            if (stage.packet_state == PacketState::Captured)
            {
                stage.packet_state = PacketState::Captured;
            }
            else if (stage.intake_volume < 0.05)
            {
                stage.packet_state = PacketState::Empty;
            }
            continue;
        }

        const double lift_efficiency =
            clamp01(0.65 * stage.vent_clearance +
                    0.45 * stage.spill_lip_integrity -
                    0.18 * stage.riser_height +
                    0.12 * metrics_.pulse_energy);

        const double lifted = stage.intake_volume * lift_efficiency;
        const double retained = lifted * clamp01(0.55 * stage.vent_clearance + 0.55 * stage.spill_lip_integrity);
        const double backflow = std::max(0.0, lifted - retained);

        stage.capture_volume = std::min(stage.packet_target, stage.capture_volume + retained);
        stage.intake_volume = std::max(0.0, stage.intake_volume - lifted);
        metrics_.lost_to_backflow += backflow;

        if (retained > 0.05)
        {
            stage.packet_state = PacketState::Captured;
            metrics_.delivered_this_cycle += retained;
        }
        else if (lifted > 0.02)
        {
            stage.packet_state = PacketState::Lost;
        }
        else
        {
            stage.packet_state = PacketState::Lifting;
        }

        const double contamination_gain =
            clamp01((1.0 - stage.vent_clearance) * 0.08 + (1.0 - stage.spill_lip_integrity) * 0.05) *
            metrics_.pulse_energy;
        stage.salt_contamination = clamp01(stage.salt_contamination * 0.92 + contamination_gain);
    }

    if (second.capture_volume > 0.75)
    {
        const double delivered = second.capture_volume * 0.34;
        metrics_.upper_cistern = std::min(6.0, metrics_.upper_cistern + delivered);
        second.capture_volume = std::max(0.2, second.capture_volume - delivered);
        push_event("Upper cistern received a new freshwater packet from stage two.");
    }

    const bool isolation_failure =
        first.vent_clearance < 0.42 || second.vent_clearance < 0.42 ||
        first.spill_lip_integrity < 0.4 || second.spill_lip_integrity < 0.4;
    if (isolation_failure && metrics_.lost_to_backflow > 0.08)
    {
        push_event("Packet isolation failure: a vent or lip is re-coupling staged gains into backflow.");
    }

    metrics_.salt_intrusion = 0.5 * first.salt_contamination + 0.5 * second.salt_contamination;
    if (metrics_.salt_intrusion > 0.26)
    {
        push_event("Salt breach risk rising: sea-side pulse is contaminating the freshwater packet train.");
    }
}

void Simulator::update_tiles()
{
    for (TileState &tile : tiles_)
    {
        tile.water_level = 0.0;
        tile.pressure = 0.0;
        tile.salinity = 0.0;
    }

    auto set_water = [&](int x, int y, double level, double pressure, double salinity) {
        TileState &tile = tiles_[index_of(width_, x, y)];
        tile.water_level = level;
        tile.pressure = pressure;
        tile.salinity = salinity;
    };

    for (int y = 1; y <= 6; ++y)
    {
        set_water(1, y, metrics_.sea_head / 6.0, metrics_.sea_head, 1.0);
    }

    const double charge_ratio = clamp01(metrics_.charge_level / 4.0);
    set_water(2, 3, charge_ratio * 0.7, metrics_.sea_head * 0.7, 1.0);
    set_water(3, 3, charge_ratio, lerp(1.6, 5.0, charge_ratio), 1.0);
    set_water(4, 3, charge_ratio, lerp(1.6, 5.0, charge_ratio), 1.0);

    const double siphon_level = (siphon_state_ == SiphonState::Triggered || siphon_state_ == SiphonState::Recovering)
                                    ? 0.9
                                    : charge_ratio * 0.5;
    set_water(5, 3, siphon_level, metrics_.pulse_energy * 4.0, 1.0);
    set_water(6, 2, siphon_level, metrics_.pulse_energy * 4.6, 1.0);
    set_water(7, 2, siphon_level, metrics_.pulse_energy * 4.8, 1.0);
    set_water(8, 2, siphon_level, metrics_.pulse_energy * 4.4, 1.0);
    set_water(7, 3, clamp01(metrics_.pulse_energy), 1.0 + metrics_.pulse_energy * 4.0, 0.4);

    set_water(9, 4, clamp01(stages_[0].source_volume / 4.0), 1.2, 0.0);
    set_water(10, 4, clamp01(stages_[0].intake_volume / stages_[0].packet_target), 1.0 + stages_[0].intake_volume, stages_[0].salt_contamination);
    set_water(11, 3, clamp01((stages_[0].intake_volume + stages_[0].capture_volume) / 1.4), 1.1 + metrics_.pulse_energy * 2.8, stages_[0].salt_contamination);
    set_water(12, 2, clamp01(stages_[0].capture_volume / stages_[0].packet_target), 1.5 + stages_[0].capture_volume, stages_[0].salt_contamination);
    set_water(12, 3, clamp01(stages_[0].vent_clearance), 0.7, stages_[0].salt_contamination * 0.3);
    set_water(13, 2, clamp01(stages_[0].capture_volume / stages_[0].packet_target), 1.1, stages_[0].salt_contamination * 0.5);

    set_water(10, 5, clamp01(stages_[1].intake_volume / stages_[1].packet_target), 1.0 + stages_[1].intake_volume, stages_[1].salt_contamination);
    set_water(11, 4, clamp01((stages_[1].intake_volume + stages_[1].capture_volume) / 1.2), 1.0 + metrics_.pulse_energy * 2.2, stages_[1].salt_contamination);
    set_water(12, 4, clamp01(stages_[1].capture_volume / stages_[1].packet_target), 1.6 + stages_[1].capture_volume, stages_[1].salt_contamination);
    set_water(12, 5, clamp01(stages_[1].vent_clearance), 0.7, stages_[1].salt_contamination * 0.25);
    set_water(13, 4, clamp01(stages_[1].capture_volume / stages_[1].packet_target), 1.1, stages_[1].salt_contamination * 0.5);

    set_water(14, 2, clamp01(metrics_.upper_cistern / 6.0), 2.0 + metrics_.upper_cistern * 0.25, stages_[1].salt_contamination * 0.1);

    tiles_[index_of(width_, 12, 3)].air_clearance = stages_[0].vent_clearance;
    tiles_[index_of(width_, 12, 5)].air_clearance = stages_[1].vent_clearance;
    tiles_[index_of(width_, 13, 2)].structural_wear = 1.0 - stages_[0].spill_lip_integrity;
    tiles_[index_of(width_, 13, 4)].structural_wear = 1.0 - stages_[1].spill_lip_integrity;
}

void Simulator::push_event(std::string message)
{
    recent_events_.insert(recent_events_.begin(), std::move(message));
    if (recent_events_.size() > event_limit)
    {
        recent_events_.resize(event_limit);
    }
}

void Simulator::decay_cycle_metrics()
{
    metrics_.pulse_energy *= 0.94;
    metrics_.salt_intrusion *= 0.97;

    for (StageState &stage : stages_)
    {
        stage.capture_volume *= 0.995;
        stage.intake_volume *= 0.96;
        stage.source_volume = std::max(stage.source_volume, 0.0);
        stage.vent_clearance = std::clamp(stage.vent_clearance - 0.001, 0.2, 1.0);
        stage.spill_lip_integrity = std::clamp(stage.spill_lip_integrity - 0.0008, 0.18, 1.0);
    }

    tiles_[index_of(width_, 2, 3)].resistance = std::clamp(tiles_[index_of(width_, 2, 3)].resistance + 0.0012, 0.15, 0.82);
    tiles_[index_of(width_, 6, 2)].air_clearance = std::clamp(tiles_[index_of(width_, 6, 2)].air_clearance - 0.001, 0.12, 1.0);
    tiles_[index_of(width_, 7, 2)].air_clearance = std::clamp(tiles_[index_of(width_, 7, 2)].air_clearance - 0.001, 0.12, 1.0);
    tiles_[index_of(width_, 8, 2)].air_clearance = std::clamp(tiles_[index_of(width_, 8, 2)].air_clearance - 0.001, 0.12, 1.0);
}
} // namespace sea_fed_packet_pump_003
