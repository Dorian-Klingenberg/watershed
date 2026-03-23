#pragma once

#include <string>
#include <vector>

namespace tide_logic_regulator_002
{
enum class InterventionType
{
    ClearTideIntake,
    SealReservoirLeak,
    RepairDelayBasin,
    ReopenOverflowPath
};

enum class TileKind
{
    Void,
    Reservoir,
    FlowBiasChannel,
    TideChannel,
    PressureComparator,
    DelayBasin,
    AirTrapChamber,
    ThresholdLip,
    SiphonChannel,
    OverflowSpillway,
    Settlement,
    Sea
};

enum class TileSiphonState
{
    Unprimed,
    Priming,
    Active,
    Broken
};

struct TileWaterLevel
{
    double value = 0.0;
};

struct TilePressureHead
{
    double value = 0.0;
};

struct TileAirVolume
{
    double value = 0.0;
};

struct TileFlowResistance
{
    double value = 0.0;
};

struct TileState
{
    TileKind kind = TileKind::Void;
    TileWaterLevel water_level;
    TilePressureHead pressure_head;
    TileAirVolume air_volume;
    TileFlowResistance flow_resistance;
    double flow_capacity = 0.0;
    double sediment = 0.0;
    double structural_damage = 0.0;
    double leak_rate = 0.0;
    double geometry_drift = 0.0;
    TileSiphonState siphon_state = TileSiphonState::Unprimed;
};

struct ControlState
{
    bool reservoir_pressure_ready = false;
    bool tide_backpressure_safe = false;
    bool differential_ready = false;
    bool basin_memory_charged = false;
    bool release_siphon_active = false;
    bool overflow_active = false;
    TileSiphonState release_siphon_state = TileSiphonState::Unprimed;
};

struct HydraulicMetrics
{
    double tide_phase = 0.0;
    double reservoir_level = 0.0;
    double reservoir_pressure = 0.0;
    double tide_actual = 0.0;
    double tide_sensed = 0.0;
    double basin_charge = 0.0;
    double siphon_release = 0.0;
    double overflow_release = 0.0;
    double downstream_delivery = 0.0;
    double marsh_level = 0.0;
    double settlement_risk = 0.0;
    double air_intrusion = 0.0;
};

struct SimulationSnapshot
{
    int tick = 0;
    int width = 0;
    int height = 0;
    std::vector<TileState> tiles;
    ControlState control;
    HydraulicMetrics metrics;
    std::vector<std::string> recent_events;
};

class Simulator
{
  public:
    Simulator();

    void step();
    void reset();
    void apply_intervention(InterventionType intervention);
    [[nodiscard]] SimulationSnapshot snapshot() const;

  private:
    void initialize_layout();
    void update_fluidic_control(double reservoir_pressure, double tide_actual);
    void update_physical_state(double reservoir_pressure, double tide_actual);
    void update_tiles();
    void push_event(std::string event_text);

    [[nodiscard]] TileState &tile_at(int x, int y);
    [[nodiscard]] const TileState &tile_at(int x, int y) const;

    int width_ = 12;
    int height_ = 7;
    int tick_ = 0;
    std::vector<TileState> tiles_;
    ControlState control_;
    HydraulicMetrics metrics_;
    std::vector<std::string> recent_events_;

    double tide_phase_ = 0.0;
    double overflow_capacity_bonus_ = 0.0;
    TileSiphonState previous_release_siphon_state_ = TileSiphonState::Unprimed;
    bool previous_overflow_active_ = false;
};
} // namespace tide_logic_regulator_002
