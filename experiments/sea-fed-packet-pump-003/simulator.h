#pragma once

#include <array>
#include <string>
#include <vector>

namespace sea_fed_packet_pump_003
{
enum class InterventionType
{
    ClearSeaInlet,
    RepairTriggerSiphon,
    ClearVentStack,
    RepairSpillLip
};

enum class TileKind
{
    Void,
    SeaReservoir,
    RestrictedInlet,
    ChargeChamber,
    TriggerSiphon,
    PulseChamber,
    Spring,
    IntakePocket,
    PacketRiser,
    CaptureBasin,
    VentStack,
    UpperCistern,
    SpillLip
};

enum class SiphonState
{
    Idle,
    Charging,
    Triggered,
    Recovering,
    Misfiring
};

enum class PacketState
{
    Empty,
    Filling,
    Lifting,
    Captured,
    Lost
};

struct TileState
{
    TileKind kind = TileKind::Void;
    double water_level = 0.0;
    double pressure = 0.0;
    double salinity = 0.0;
    double resistance = 0.0;
    double air_clearance = 0.0;
    double structural_wear = 0.0;
};

struct StageState
{
    double source_volume = 0.0;
    double intake_volume = 0.0;
    double capture_volume = 0.0;
    double riser_height = 0.0;
    double packet_target = 0.0;
    double vent_clearance = 0.0;
    double spill_lip_integrity = 0.0;
    double salt_contamination = 0.0;
    PacketState packet_state = PacketState::Empty;
};

struct PumpMetrics
{
    double sea_head = 0.0;
    double charge_level = 0.0;
    double pulse_energy = 0.0;
    double cycle_progress = 0.0;
    double upper_cistern = 0.0;
    double delivered_this_cycle = 0.0;
    double lost_to_backflow = 0.0;
    double salt_intrusion = 0.0;
};

struct SimulationSnapshot
{
    int tick = 0;
    int width = 0;
    int height = 0;
    std::vector<TileState> tiles;
    std::array<StageState, 2> stages;
    PumpMetrics metrics;
    SiphonState siphon_state = SiphonState::Idle;
    std::vector<std::string> recent_events;
};

class Simulator
{
public:
    Simulator();

    void reset();
    void step();
    void apply_intervention(InterventionType intervention);
    [[nodiscard]] SimulationSnapshot snapshot() const;

private:
    void build_layout();
    void update_charge_cycle();
    void run_packet_cycle();
    void update_tiles();
    void push_event(std::string message);
    void decay_cycle_metrics();

    int width_ = 16;
    int height_ = 8;
    int tick_ = 0;

    std::vector<TileState> tiles_;
    std::array<StageState, 2> stages_{};
    PumpMetrics metrics_{};
    SiphonState siphon_state_ = SiphonState::Idle;
    std::vector<std::string> recent_events_;
};
} // namespace sea_fed_packet_pump_003
