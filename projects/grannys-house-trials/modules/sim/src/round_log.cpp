#include "grannys_house_trials/sim/round_log.h"

#include <algorithm>
#include <utility>

namespace grannys_house_trials::sim
{
void RoundLog::record(EvidenceItem item)
{
    entries_.push_back(std::move(item));
}

std::size_t RoundLog::size() const noexcept
{
    return entries_.size();
}

std::size_t RoundLog::count(EvidenceType type) const noexcept
{
    return static_cast<std::size_t>(std::count_if(
        entries_.begin(),
        entries_.end(),
        [type](const EvidenceItem &item) {
            return item.type == type;
        }));
}

const std::vector<EvidenceItem> &RoundLog::entries() const noexcept
{
    return entries_;
}
} // namespace grannys_house_trials::sim

