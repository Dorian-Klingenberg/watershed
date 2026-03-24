#include "grannys_house_trials/playtest/evidence_board_view.h"

#include <algorithm>

namespace grannys_house_trials::playtest
{
EvidenceBoardView make_evidence_board_view(const sim::RoundLog &round_log)
{
    EvidenceBoardView view{};

    for (const sim::EvidenceItem &item : round_log.entries())
    {
        const auto it = std::find_if(
            view.stats.begin(),
            view.stats.end(),
            [type = item.type](const EvidenceBoardStat &stat) {
                return stat.type == type;
            });

        if (it == view.stats.end())
        {
            view.stats.push_back(EvidenceBoardStat{item.type, static_cast<std::size_t>(1)});
        }
        else
        {
            it->count += 1;
        }

        view.highlights.push_back(
            std::string(item.actor.view()) + ": " + item.description.str());
    }

    return view;
}
} // namespace grannys_house_trials::playtest
