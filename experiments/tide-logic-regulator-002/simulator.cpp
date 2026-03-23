#include "simulator.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <utility>

namespace tide_logic_regulator_002
{
namespace
{
std::size_t index_for(int x, int y, int width)
{
    return static_cast<std::size_t>(y * width + x);
}

double clamp01(double value)
{
    return std::clamp(value, 0.0, 1.0);
}

const char *siphon_state_name(TileSiphonState state)
{
    switch (state)
    {
    case TileSiphonState::Unprimed:
        return "unprimed";
    case TileSiphonState::Priming:
        return "priming";
    case TileSiphonState::Active:
        return "active";
    case TileSiphonState::Broken:
        return "broken";
    }

    return "unknown";
}
} // namespace

Simulator::Simulator()
{
    reset();
}

void Simulator::reset()
{
    initialize_layout();

    tick_ = 0;
    tide_phase_ = 0.0;
    overflow_capacity_bonus_ = 0.0;
    previous_release_siphon_state_ = TileSiphonState::Unprimed;
    previous_overflow_active_ = false;

    metrics_.reservoir_level = 7.2;
    metrics_.tide_actual = 3.4;
    metrics_.tide_sensed = 2.5;
    metrics_.basin_charge = 0.24;
    metrics_.marsh_level = 3.1;
    metrics_.air_intrusion = 0.16;

    const double initial_reservoir_pressure = std::max(0.0, metrics_.reservoir_level * 0.92 - 1.1);
    update_fluidic_control(initial_reservoir_pressure, metrics_.tide_actual);
    update_physical_state(initial_reservoir_pressure, metrics_.tide_actual);
    update_tiles();
}

void Simulator::apply_intervention(InterventionType intervention)
{
    switch (intervention)
    {
    case InterventionType::ClearTideIntake:
        for (int x = 3; x <= 4; ++x)
        {
            TileState &tile = tile_at(x, 1);
            tile.sediment = std::max(0.08, tile.sediment - 0.22);
            tile.flow_resistance.value = std::max(0.10, tile.flow_resistance.value - 0.12);
            tile.geometry_drift = std::max(0.02, tile.geometry_drift - 0.08);
        }
        push_event("Intervention: tide intake cleared. Backpressure sensing should track the sea more faithfully.");
        break;
    case InterventionType::SealReservoirLeak:
    {
        TileState &reservoir = tile_at(1, 3);
        reservoir.leak_rate = std::max(0.03, reservoir.leak_rate - 0.10);
        reservoir.structural_damage = std::min(0.95, reservoir.structural_damage + 0.05);
        push_event("Intervention: reservoir leak sealed. Inland head will rise, which improves release but amplifies mistakes.");
        break;
    }
    case InterventionType::RepairDelayBasin:
    {
        TileState &basin = tile_at(3, 2);
        basin.leak_rate = std::max(0.02, basin.leak_rate - 0.09);
        basin.structural_damage = std::max(0.10, basin.structural_damage - 0.18);
        basin.geometry_drift = std::max(0.02, basin.geometry_drift - 0.10);
        push_event("Intervention: delay basin repaired. Memory now holds longer, so bad inferences can persist instead of self-canceling.");
        break;
    }
    case InterventionType::ReopenOverflowPath:
        overflow_capacity_bonus_ = std::min(2.2, overflow_capacity_bonus_ + 0.55);
        for (int x = 4; x <= 6; ++x)
        {
            TileState &spillway = tile_at(x, 4);
            spillway.flow_resistance.value = std::max(0.12, spillway.flow_resistance.value - 0.08);
            spillway.sediment = std::max(0.04, spillway.sediment - 0.06);
        }
        push_event("Intervention: overflow spillway reopened. Surges can leave through the marsh instead of corrupting the comparator.");
        break;
    }
}

void Simulator::step()
{
    ++tick_;
    tide_phase_ += 0.37;

    const TileState &reservoir = tile_at(1, 3);
    const double tide_actual = 4.7 + 3.3 * std::sin(tide_phase_);
    const double reservoir_feed = 1.15 + 0.45 * std::sin(tick_ * 0.11 + 0.4);
    const double leakage_loss = 0.30 + (metrics_.reservoir_level * (0.03 + reservoir.leak_rate * 0.09));

    metrics_.reservoir_level += reservoir_feed - leakage_loss - metrics_.siphon_release - metrics_.overflow_release;
    metrics_.reservoir_level = std::max(0.0, metrics_.reservoir_level);

    const double leak_penalty = reservoir.leak_rate * 1.6;
    const double reservoir_pressure = std::max(0.0, metrics_.reservoir_level * 0.92 - 1.1 - leak_penalty);

    update_fluidic_control(reservoir_pressure, tide_actual);
    update_physical_state(reservoir_pressure, tide_actual);
    update_tiles();
}

SimulationSnapshot Simulator::snapshot() const
{
    return SimulationSnapshot{
        .tick = tick_,
        .width = width_,
        .height = height_,
        .tiles = tiles_,
        .control = control_,
        .metrics = metrics_,
        .recent_events = recent_events_};
}

void Simulator::initialize_layout()
{
    tiles_.assign(static_cast<std::size_t>(width_ * height_), {});
    recent_events_.clear();

    auto set_tile = [&](int x, int y, TileKind kind, double capacity, double resistance, double sediment,
                        double damage, double leak_rate, double geometry_drift, double air_volume,
                        TileSiphonState siphon_state)
    {
        TileState &tile = tile_at(x, y);
        tile.kind = kind;
        tile.flow_capacity = capacity;
        tile.flow_resistance.value = resistance;
        tile.sediment = sediment;
        tile.structural_damage = damage;
        tile.leak_rate = leak_rate;
        tile.geometry_drift = geometry_drift;
        tile.air_volume.value = air_volume;
        tile.siphon_state = siphon_state;
    };

    set_tile(1, 3, TileKind::Reservoir, 10.0, 0.10, 0.08, 0.18, 0.18, 0.04, 0.04, TileSiphonState::Unprimed);
    set_tile(2, 3, TileKind::FlowBiasChannel, 6.5, 0.22, 0.14, 0.22, 0.04, 0.10, 0.08, TileSiphonState::Unprimed);
    set_tile(3, 3, TileKind::PressureComparator, 4.2, 0.42, 0.10, 0.28, 0.02, 0.12, 0.14, TileSiphonState::Unprimed);
    set_tile(3, 2, TileKind::DelayBasin, 3.2, 0.50, 0.05, 0.52, 0.14, 0.18, 0.18, TileSiphonState::Unprimed);
    set_tile(4, 2, TileKind::AirTrapChamber, 2.8, 0.48, 0.08, 0.34, 0.02, 0.08, 0.34, TileSiphonState::Unprimed);
    set_tile(4, 3, TileKind::ThresholdLip, 3.6, 0.32, 0.10, 0.20, 0.01, 0.12, 0.20, TileSiphonState::Unprimed);
    set_tile(5, 3, TileKind::SiphonChannel, 7.2, 0.18, 0.12, 0.18, 0.03, 0.06, 0.26, TileSiphonState::Unprimed);
    set_tile(6, 3, TileKind::FlowBiasChannel, 6.2, 0.16, 0.14, 0.18, 0.01, 0.05, 0.10, TileSiphonState::Unprimed);
    set_tile(7, 3, TileKind::Settlement, 2.5, 0.50, 0.00, 0.10, 0.00, 0.00, 0.00, TileSiphonState::Unprimed);
    set_tile(4, 4, TileKind::OverflowSpillway, 5.0, 0.36, 0.14, 0.08, 0.00, 0.10, 0.06, TileSiphonState::Unprimed);
    set_tile(5, 4, TileKind::OverflowSpillway, 5.0, 0.34, 0.12, 0.08, 0.00, 0.08, 0.06, TileSiphonState::Unprimed);
    set_tile(6, 4, TileKind::OverflowSpillway, 5.0, 0.34, 0.12, 0.08, 0.00, 0.08, 0.06, TileSiphonState::Unprimed);
    set_tile(3, 1, TileKind::TideChannel, 3.0, 0.58, 0.72, 0.32, 0.00, 0.16, 0.24, TileSiphonState::Unprimed);
    set_tile(4, 1, TileKind::TideChannel, 3.0, 0.54, 0.66, 0.30, 0.00, 0.14, 0.28, TileSiphonState::Unprimed);
    set_tile(5, 1, TileKind::Sea, 10.0, 0.05, 0.00, 0.00, 0.00, 0.00, 0.00, TileSiphonState::Unprimed);

    push_event("Ruin bootstrapped in degraded state: silted tide intake, leaking reservoir branch, unstable air trap, cracked delay basin.");
}

void Simulator::update_fluidic_control(double reservoir_pressure, double tide_actual)
{
    TileState &tide_a = tile_at(3, 1);
    TileState &tide_b = tile_at(4, 1);
    TileState &comparator = tile_at(3, 3);
    TileState &delay_basin = tile_at(3, 2);
    TileState &air_trap = tile_at(4, 2);
    TileState &threshold_lip = tile_at(4, 3);
    TileState &release_siphon = tile_at(5, 3);

    metrics_.reservoir_pressure = reservoir_pressure;
    metrics_.tide_actual = tide_actual;
    metrics_.tide_phase = tide_phase_;

    const double average_tide_sediment = (tide_a.sediment + tide_b.sediment) * 0.5;
    const double average_tide_resistance = (tide_a.flow_resistance.value + tide_b.flow_resistance.value) * 0.5;
    const double tide_response = std::clamp(0.40 - (average_tide_sediment * 0.22) - (average_tide_resistance * 0.18), 0.08, 0.40);
    metrics_.tide_sensed += (tide_actual - metrics_.tide_sensed) * tide_response;

    const double tide_backpressure_threshold = 4.7 - threshold_lip.geometry_drift * 0.9;
    const double release_threshold = 4.8 + threshold_lip.geometry_drift * 1.1;
    const double differential = reservoir_pressure - metrics_.tide_sensed * 0.34;

    control_.reservoir_pressure_ready = reservoir_pressure >= release_threshold;
    control_.tide_backpressure_safe = metrics_.tide_sensed <= tide_backpressure_threshold;
    control_.differential_ready = differential >= (2.0 + comparator.geometry_drift * 0.6);

    if (control_.reservoir_pressure_ready && control_.tide_backpressure_safe && control_.differential_ready)
    {
        metrics_.basin_charge += 0.24 - delay_basin.leak_rate * 0.08;
        air_trap.air_volume.value = std::max(0.06, air_trap.air_volume.value - 0.06);
    }
    else
    {
        metrics_.basin_charge -= 0.07 + delay_basin.leak_rate * 0.38 + delay_basin.geometry_drift * 0.12;
        air_trap.air_volume.value = std::min(0.92, air_trap.air_volume.value + 0.05 + threshold_lip.geometry_drift * 0.05);
    }

    const double air_intrusion_rate = clamp01((tide_actual - 6.0) / 2.8) * 0.18 + air_trap.structural_damage * 0.08;
    metrics_.air_intrusion = std::clamp(metrics_.air_intrusion * 0.78 + air_intrusion_rate + air_trap.air_volume.value * 0.06, 0.0, 1.0);
    metrics_.basin_charge = clamp01(metrics_.basin_charge);

    control_.basin_memory_charged = metrics_.basin_charge >= 0.58;

    TileSiphonState next_state = control_.release_siphon_state;
    switch (control_.release_siphon_state)
    {
    case TileSiphonState::Unprimed:
        if (control_.reservoir_pressure_ready && control_.tide_backpressure_safe && control_.differential_ready)
        {
            next_state = TileSiphonState::Priming;
        }
        break;
    case TileSiphonState::Priming:
        if (metrics_.air_intrusion > 0.62)
        {
            next_state = TileSiphonState::Broken;
        }
        else if (metrics_.basin_charge >= 0.56 && air_trap.air_volume.value <= 0.28)
        {
            next_state = TileSiphonState::Active;
        }
        break;
    case TileSiphonState::Active:
        if (metrics_.air_intrusion > 0.68 || tide_actual > 7.25)
        {
            next_state = TileSiphonState::Broken;
        }
        else if (!control_.differential_ready && metrics_.basin_charge < 0.26)
        {
            next_state = TileSiphonState::Unprimed;
        }
        break;
    case TileSiphonState::Broken:
        if (metrics_.basin_charge < 0.14 && metrics_.air_intrusion < 0.32)
        {
            next_state = TileSiphonState::Unprimed;
        }
        break;
    }

    control_.release_siphon_state = next_state;
    control_.release_siphon_active = next_state == TileSiphonState::Active;
    release_siphon.siphon_state = next_state;

    if (next_state != previous_release_siphon_state_)
    {
        push_event(std::string("Release siphon shifted to ") + siphon_state_name(next_state) + '.');
    }

    if (next_state == TileSiphonState::Broken && metrics_.air_intrusion > 0.68)
    {
        push_event("Air intrusion broke the siphon path before the inland basin had fully discharged.");
    }

    previous_release_siphon_state_ = next_state;
}

void Simulator::update_physical_state(double reservoir_pressure, double tide_actual)
{
    const TileState &reservoir = tile_at(1, 3);
    const TileState &threshold_lip = tile_at(4, 3);
    const double tide_backpressure = std::clamp((tide_actual - 3.8) / 3.4, 0.0, 1.0);
    const double release_potential = std::max(0.0, reservoir_pressure - tide_actual * 0.35);

    double siphon_factor = 0.0;
    switch (control_.release_siphon_state)
    {
    case TileSiphonState::Unprimed:
        siphon_factor = 0.0;
        break;
    case TileSiphonState::Priming:
        siphon_factor = 0.34;
        break;
    case TileSiphonState::Active:
        siphon_factor = 0.90;
        break;
    case TileSiphonState::Broken:
        siphon_factor = 0.12;
        break;
    }

    metrics_.siphon_release = siphon_factor * release_potential * (0.66 - tide_backpressure * 0.28);
    metrics_.siphon_release = std::max(0.0, metrics_.siphon_release);

    const double overflow_threshold = 8.5 + overflow_capacity_bonus_ - threshold_lip.geometry_drift * 0.8;
    metrics_.overflow_release = std::max(0.0, metrics_.reservoir_level - overflow_threshold) * 0.68;
    control_.overflow_active = metrics_.overflow_release > 0.12;

    if (control_.overflow_active && !previous_overflow_active_)
    {
        push_event("Overflow spillway engaged to protect the siphon throat and comparator basin.");
    }
    else if (!control_.overflow_active && previous_overflow_active_)
    {
        push_event("Overflow spillway settled back to idle.");
    }

    previous_overflow_active_ = control_.overflow_active;

    metrics_.downstream_delivery = metrics_.siphon_release * (0.88 - tide_backpressure * 0.18);
    metrics_.downstream_delivery = std::max(0.0, metrics_.downstream_delivery);

    const double marsh_inflow = metrics_.overflow_release + 0.22 + (reservoir.leak_rate * metrics_.reservoir_level * 0.10);
    const double marsh_outflow = 0.18 + metrics_.downstream_delivery * 0.04;
    metrics_.marsh_level += marsh_inflow - marsh_outflow;
    metrics_.marsh_level = std::max(0.0, metrics_.marsh_level);

    metrics_.settlement_risk = std::clamp((metrics_.marsh_level - 4.8) / 3.4, 0.0, 1.0);

    if (control_.release_siphon_state == TileSiphonState::Active && tide_actual > 6.9)
    {
        push_event("Unsafe release window: delayed tide sensing kept the siphon active into hostile backpressure.");
    }

    if (control_.release_siphon_state == TileSiphonState::Broken && metrics_.reservoir_level > 8.6)
    {
        push_event("The regulator is miscomputing: inland head remains high while the siphon has lost prime.");
    }

    if (metrics_.settlement_risk > 0.82)
    {
        push_event("Marsh-edge settlement entered severe flood risk after prolonged spill activity.");
    }
}

void Simulator::update_tiles()
{
    for (TileState &tile : tiles_)
    {
        tile.water_level.value = 0.0;
        tile.pressure_head.value = 0.0;
    }

    auto apply = [&](int x, int y, double water_level, double pressure_head, double air_volume, TileSiphonState siphon_state)
    {
        TileState &tile = tile_at(x, y);
        tile.water_level.value = water_level;
        tile.pressure_head.value = pressure_head;
        tile.air_volume.value = air_volume;
        tile.siphon_state = siphon_state;
    };

    apply(1, 3, metrics_.reservoir_level, metrics_.reservoir_pressure, tile_at(1, 3).air_volume.value, TileSiphonState::Unprimed);
    apply(2, 3, metrics_.reservoir_level * 0.68, metrics_.reservoir_pressure * 0.76, 0.08, TileSiphonState::Unprimed);
    apply(3, 3, control_.differential_ready ? 3.8 : 1.4, control_.differential_ready ? 5.0 : 1.3, 0.12, TileSiphonState::Unprimed);
    apply(3, 2, metrics_.basin_charge * 6.2, metrics_.basin_charge * 4.2, 0.18, TileSiphonState::Unprimed);
    apply(4, 2, metrics_.basin_charge * 2.8, metrics_.basin_charge * 3.6, tile_at(4, 2).air_volume.value, TileSiphonState::Unprimed);
    apply(4, 3, control_.differential_ready ? 3.6 : 1.0, control_.differential_ready ? 4.1 : 0.8, 0.16, TileSiphonState::Unprimed);
    apply(5, 3, metrics_.siphon_release * 1.2, metrics_.siphon_release * 0.82, tile_at(5, 3).air_volume.value, control_.release_siphon_state);
    apply(6, 3, metrics_.downstream_delivery, metrics_.downstream_delivery * 0.56, 0.08, TileSiphonState::Unprimed);
    apply(7, 3, metrics_.settlement_risk * 5.5, metrics_.settlement_risk * 2.0, 0.0, TileSiphonState::Unprimed);
    apply(4, 4, metrics_.marsh_level * 0.72, metrics_.marsh_level * 0.24, 0.06, TileSiphonState::Unprimed);
    apply(5, 4, metrics_.marsh_level * 0.85, metrics_.marsh_level * 0.28, 0.06, TileSiphonState::Unprimed);
    apply(6, 4, metrics_.marsh_level * 0.68, metrics_.marsh_level * 0.21, 0.06, TileSiphonState::Unprimed);
    apply(3, 1, metrics_.tide_sensed * 0.88, metrics_.tide_sensed, 0.22, TileSiphonState::Unprimed);
    apply(4, 1, metrics_.tide_sensed, metrics_.tide_sensed * 1.04, 0.26, TileSiphonState::Unprimed);
    apply(5, 1, metrics_.tide_actual, metrics_.tide_actual, 0.0, TileSiphonState::Unprimed);
}

void Simulator::push_event(std::string event_text)
{
    std::ostringstream stream;
    stream << "t=" << tick_ << ": " << std::move(event_text);

    recent_events_.push_back(stream.str());

    constexpr std::size_t kMaxEvents = 6;
    if (recent_events_.size() > kMaxEvents)
    {
        recent_events_.erase(recent_events_.begin(), recent_events_.begin() + 1);
    }
}

TileState &Simulator::tile_at(int x, int y)
{
    return tiles_[index_for(x, y, width_)];
}

const TileState &Simulator::tile_at(int x, int y) const
{
    return tiles_[index_for(x, y, width_)];
}
} // namespace tide_logic_regulator_002
