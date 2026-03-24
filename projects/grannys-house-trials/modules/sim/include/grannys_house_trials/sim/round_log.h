#pragma once

#include "grannys_house_trials/sim/evidence_item.h"

#include <cstddef>
#include <vector>

namespace grannys_house_trials::sim
{
class RoundLog
{
public:
    void record(EvidenceItem item);

    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::size_t count(EvidenceType type) const noexcept;
    [[nodiscard]] const std::vector<EvidenceItem> &entries() const noexcept;

private:
    std::vector<EvidenceItem> entries_;
};
} // namespace grannys_house_trials::sim

