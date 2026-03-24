#include "grannys_house_trials/gfx/orbit_camera.h"
#include "grannys_house_trials/sim/evidence_item.h"
#include "grannys_house_trials/sim/evidence_type.h"
#include "grannys_house_trials/sim/round_log.h"
#include "grannys_house_trials/util/non_empty_string.h"

#include <iostream>

int main()
{
    using grannys_house_trials::gfx::OrbitCamera;
    using grannys_house_trials::sim::EvidenceItem;
    using grannys_house_trials::sim::EvidenceType;
    using grannys_house_trials::sim::RoundLog;
    using grannys_house_trials::util::NonEmptyString;

    RoundLog round_log;
    round_log.record(EvidenceItem{
        NonEmptyString("Builder"),
        EvidenceType::ObjectiveCompleted,
        NonEmptyString("Water reached the garden beds."),
    });
    round_log.record(EvidenceItem{
        NonEmptyString("Systems Auditor"),
        EvidenceType::HiddenDependencyRevealed,
        NonEmptyString("The cellar drain is cross-linked to an older channel."),
    });

    OrbitCamera camera;
    camera.orbit(15.0f, -5.0f);
    camera.zoom(-2.0f);

    std::cout << "Granny's House Trials scaffold\n";
    std::cout << "Recorded evidence items: " << round_log.size() << "\n";
    std::cout << "Hidden dependency discoveries: "
              << round_log.count(EvidenceType::HiddenDependencyRevealed)
              << "\n";
    std::cout << "Camera distance: " << camera.distance() << "\n";

    return 0;
}

