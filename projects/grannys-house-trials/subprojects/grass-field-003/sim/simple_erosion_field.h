#pragma once

// SimpleErosionField — a gravity-settling erosion simulation on a flat 2D grid.
//
// This is a from-scratch implementation of the core erosion algorithm so you
// can see every piece. It intentionally drops the sub-inch fine grid and the
// per-material erodibility mask from the production GravityErosionField —
// those are enhancements you add once you understand the base algorithm.
//
// The model:
//   - The terrain is a width × depth grid of column heights (in inches).
//   - Each step_cycle() call examines every column. If a column is more than
//     1 inch taller than any of its 4 axis-aligned neighbours, one inch of
//     material falls to the lowest neighbour.
//   - Updates are computed in two passes (accumulate deltas, then apply) so
//     the result does not depend on the scan order. This is the same two-pass
//     pattern used by every stable cellular automaton.

#include <cstddef>
#include <vector>

namespace grannys_house_trials::sim
{
class SimpleErosionField
{
public:
    // Default construction yields an empty (unusable) field.
    // Used when the field needs to be assigned after construction.
    SimpleErosionField() = default;

    // Construct with explicit dimensions and a flat row-major height array.
    // initial_heights_inches must contain exactly width * depth entries, one
    // per column in (z * width + x) order.
    SimpleErosionField(int width, int depth, std::vector<int> initial_heights_inches);

    [[nodiscard]] int width()       const noexcept { return width_; }
    [[nodiscard]] int depth()       const noexcept { return depth_; }
    [[nodiscard]] int cycle_count() const noexcept { return cycle_count_; }

    // Returns the column height at (x, z) in inches. Throws if out of range.
    [[nodiscard]] int height_at(int x, int z) const;

    // Advance the simulation by one gravity-settling cycle.
    void step_cycle();

private:
    [[nodiscard]] std::size_t index_of(int x, int z) const;

    int width_       = 0;
    int depth_       = 0;
    int cycle_count_ = 0;
    std::vector<int> heights_;
};
} // namespace grannys_house_trials::sim
