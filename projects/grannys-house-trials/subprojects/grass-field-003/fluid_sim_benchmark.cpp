#include "grannys_house_trials/sim/grass_field.h"
#include "sim/simple_cellular_fluid_sim.h"
#include "sim/simple_cellular_fluid_sim_active_sources.h"
#include "sim/simple_cellular_fluid_sim_parallel.h"
#include "sim/simple_cellular_fluid_sim_round1.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string_view>
#include <thread>
#include <vector>

namespace sim = grannys_house_trials::sim;

namespace
{

constexpr int k_measurement_steps = 30;
constexpr int k_warmup_steps = 5;
constexpr int k_repetitions = 3;

struct Scenario
{
    const char* name = "";
    bool rain = false;
    int radius = 0;
    float depth_inches = 0.0f;
};

[[nodiscard]] std::vector<int> build_live_seed_heights()
{
    sim::GrassField grass_field{100, 100, 1.0f};
    const int source_w = grass_field.width();
    const int source_d = grass_field.depth();
    const int detail = sim::GrassField::detail_patch_resolution();
    std::vector<int> heights;
    heights.reserve(static_cast<std::size_t>(source_w * detail * source_d * detail));

    for (int source_z = 0; source_z < source_d; ++source_z)
    {
        for (int local_z = 0; local_z < detail; ++local_z)
        {
            for (int source_x = 0; source_x < source_w; ++source_x)
            {
                for (int local_x = 0; local_x < detail; ++local_x)
                {
                    heights.push_back(grass_field.fine_top_height_inches_at(
                        source_x, source_z, local_x, local_z));
                }
            }
        }
    }

    return heights;
}

template <typename FluidSim>
void initialize_scenario(
    FluidSim& fluid,
    const std::vector<int>& seed,
    int width,
    int depth,
    const Scenario& scenario)
{
    fluid.reset(width, depth, seed);
    if (scenario.rain)
        fluid.add_uniform_rain(scenario.depth_inches);
    else
        fluid.add_center_water(scenario.radius, scenario.depth_inches);
}

template <typename FluidSim>
[[nodiscard]] double measure_ms_per_step(
    const std::vector<int>& seed,
    int width,
    int depth,
    const Scenario& scenario)
{
    std::vector<double> samples;
    samples.reserve(k_repetitions);

    for (int repetition = 0; repetition < k_repetitions; ++repetition)
    {
        FluidSim fluid;
        initialize_scenario(fluid, seed, width, depth, scenario);
        for (int step = 0; step < k_warmup_steps; ++step)
            (void)fluid.step_once();

        const auto start = std::chrono::steady_clock::now();
        for (int step = 0; step < k_measurement_steps; ++step)
            (void)fluid.step_once();
        const auto stop = std::chrono::steady_clock::now();
        samples.push_back(
            std::chrono::duration<double, std::milli>(stop - start).count() /
            static_cast<double>(k_measurement_steps));
    }

    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2];
}

template <typename Candidate>
[[nodiscard]] bool compare_exactly_to_baseline(
    const std::vector<int>& seed,
    int width,
    int depth,
    const Scenario& scenario,
    std::string_view candidate_name)
{
    sim::SimpleCellularFluidSim baseline;
    Candidate candidate;
    initialize_scenario(baseline, seed, width, depth, scenario);
    initialize_scenario(candidate, seed, width, depth, scenario);

    for (int step = 1; step <= k_measurement_steps; ++step)
    {
        (void)baseline.step_once();
        (void)candidate.step_once();

        for (int z = 0; z < depth; ++z)
        {
            for (int x = 0; x < width; ++x)
            {
                const float expected = baseline.water_depth_inches_at(x, z);
                const float actual = candidate.water_depth_inches_at(x, z);
                if (std::bit_cast<std::uint32_t>(expected) !=
                    std::bit_cast<std::uint32_t>(actual))
                {
                    std::cerr << "Mismatch: " << candidate_name << ", " << scenario.name
                              << ", step " << step << ", cell [" << x << ", " << z
                              << "], baseline=" << expected << ", candidate=" << actual
                              << '\n';
                    return false;
                }
            }
        }
    }

    return true;
}

} // namespace

int main()
{
    const std::vector<int> seed = build_live_seed_heights();
    constexpr int detail = sim::GrassField::detail_patch_resolution();
    constexpr int width = 100 * detail;
    constexpr int depth = 100 * detail;
    constexpr Scenario scenarios[] = {
        { "Center pour (r=11, 22 in)", false, 11, 22.0f },
        { "Uniform rain (1 in)", true, 0, 1.0f },
    };

    std::cout << "# grass-field-003 Fluid Performance Benchmark\n\n";
    std::cout << "- Grid: " << width << " x " << depth << " cells\n";
    std::cout << "- Timing: median of " << k_repetitions << " runs, "
              << k_measurement_steps << " measured steps after "
              << k_warmup_steps << " warmup steps\n";
    std::cout << "- Hardware threads reported by C++ runtime: "
              << std::thread::hardware_concurrency() << "\n\n";
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "| Scenario | Baseline ms/step | Round 1 ms/step | Round 2 Active ms/step | "
                 "CPU Parallel ms/step | Round 1 speedup | Round 2 speedup | "
                 "CPU Parallel speedup | Exact state |\n";
    std::cout << "|---|---:|---:|---:|---:|---:|---:|---:|---|\n";

    bool all_equal = true;
    for (const Scenario& scenario : scenarios)
    {
        const double baseline_ms =
            measure_ms_per_step<sim::SimpleCellularFluidSim>(seed, width, depth, scenario);
        const double round1_ms =
            measure_ms_per_step<sim::SimpleCellularFluidSimRound1>(seed, width, depth, scenario);
        const double round2_active_ms =
            measure_ms_per_step<sim::SimpleCellularFluidSimActiveSources>(seed, width, depth, scenario);
        const double parallel_ms =
            measure_ms_per_step<sim::SimpleCellularFluidSimParallel>(seed, width, depth, scenario);
        const bool round1_equal = compare_exactly_to_baseline<sim::SimpleCellularFluidSimRound1>(
            seed, width, depth, scenario, "Round 1");
        const bool round2_active_equal =
            compare_exactly_to_baseline<sim::SimpleCellularFluidSimActiveSources>(
                seed, width, depth, scenario, "Round 2 Active Sources");
        const bool parallel_equal = compare_exactly_to_baseline<sim::SimpleCellularFluidSimParallel>(
            seed, width, depth, scenario, "CPU Parallel");
        const bool exact = round1_equal && round2_active_equal && parallel_equal;
        all_equal = all_equal && exact;

        std::cout << "| " << scenario.name
                  << " | " << baseline_ms
                  << " | " << round1_ms
                  << " | " << round2_active_ms
                  << " | " << parallel_ms
                  << " | " << baseline_ms / round1_ms << "x"
                  << " | " << baseline_ms / round2_active_ms << "x"
                  << " | " << baseline_ms / parallel_ms << "x"
                  << " | " << (exact ? "PASS" : "FAIL") << " |\n";
    }

    return all_equal ? 0 : 1;
}
