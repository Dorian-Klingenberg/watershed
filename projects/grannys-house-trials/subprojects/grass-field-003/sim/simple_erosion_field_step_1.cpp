#include "sim/simple_erosion_field.h"

// ─────────────────────────────────────────────────────────────────────────────
// Why a delta buffer? (the two-pass design)
//
// The naive implementation mutates heights_ in-place during the scan. That
// creates an order-dependent bug: a column that was just raised by its left
// neighbour immediately donates material to its right neighbour in the same
// pass, causing material to "flow" across the whole grid in a single cycle
// rather than settling one inch at a time.
//
// The fix is to separate observation from mutation:
//   Pass 1 — observe the current state, compute what *would* change.
//   Pass 2 — apply all changes simultaneously.
//
// deltas[i] accumulates the net inch gain or loss for each column. A column
// may both lose to one neighbour and gain from another in the same cycle; the
// delta correctly captures the net effect.
// ─────────────────────────────────────────────────────────────────────────────

#include <array>
#include <stdexcept>
#include <utility>

namespace grannys_house_trials::sim
{
SimpleErosionField::SimpleErosionField(int width, int depth, std::vector<int> initial_heights_inches)
    : width_(width)
    , depth_(depth)
    , heights_(std::move(initial_heights_inches))
{
    if (width_ <= 0 || depth_ <= 0)
        throw std::invalid_argument("SimpleErosionField: width and depth must be positive.");

    if (static_cast<int>(heights_.size()) != width_ * depth_)
        throw std::invalid_argument("SimpleErosionField: initial_heights_inches size does not match width * depth.");
}

int SimpleErosionField::height_at(int x, int z) const
{
    // .at() does bounds checking and throws std::out_of_range on bad input.
    return heights_.at(index_of(x, z));
}

void SimpleErosionField::step_cycle()
{
    // Pass 1: build the delta buffer.
    //
    // We scan every column and ask: "is there a lower neighbour I should
    // fall towards?" If yes, we register -1 for this cell and +1 for the
    // target, but we do NOT write to heights_ yet.
    std::vector<int> deltas(heights_.size(), 0);

    // The four cardinal neighbours (no diagonals — gravity is axis-aligned here).
    constexpr std::array<std::pair<int, int>, 4> neighbor_offsets{{
        {-1,  0},   // West
        { 1,  0},   // East
        { 0, -1},   // North
        { 0,  1},   // South
    }};

    for (int z = 0; z < depth_; ++z)
    {
        for (int x = 0; x < width_; ++x)
        {
            const int current_height = heights_[index_of(x, z)];

            // Find the lowest accessible neighbour.
            int lowest_height  = current_height;
            int lowest_x       = x;
            int lowest_z       = z;

            for (auto [dx, dz] : neighbor_offsets)
            {
                const int nx = x + dx;
                const int nz = z + dz;

                // Skip neighbours outside the grid boundary.
                if (nx < 0 || nx >= width_ || nz < 0 || nz >= depth_)
                    continue;

                const int nh = heights_[index_of(nx, nz)];
                if (nh < lowest_height)
                {
                    lowest_height = nh;
                    lowest_x = nx;
                    lowest_z = nz;
                }
            }

            // Angle-of-repose check: only erode if the drop is at least 2 inches.
            // A difference of exactly 1 inch is considered stable — filling it
            // would just move the instability rather than settle it. This threshold
            // is what makes the simulation converge to a smooth slope rather than
            // oscillating forever.
            if (current_height - lowest_height < 2)
                continue;

            deltas[index_of(x, z)]             -= 1;
            deltas[index_of(lowest_x, lowest_z)] += 1;
        }
    }

    // Pass 2: apply the deltas simultaneously.
    for (std::size_t i = 0; i < heights_.size(); ++i)
        heights_[i] += deltas[i];

    ++cycle_count_;
}

std::size_t SimpleErosionField::index_of(int x, int z) const
{
    if (x < 0 || x >= width_ || z < 0 || z >= depth_)
        throw std::out_of_range("SimpleErosionField: coordinates out of range.");

    // Row-major layout: each row is `width_` entries wide.
    return static_cast<std::size_t>(z * width_ + x);
}
} // namespace grannys_house_trials::sim
