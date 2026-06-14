#pragma once

#include "grannys_house_trials/sim/target_id.h"

#include <optional>
#include <string>
#include <string_view>

namespace grannys_house_trials::playtest
{
struct AgentBlockSelection
{
    int x = 0;
    int y = 0;
    int z = 0;
};

struct AgentCommand
{
    std::string action_id;
    std::optional<sim::TargetId> target;
    std::optional<AgentBlockSelection> selection;
};

[[nodiscard]] std::optional<AgentCommand> parse_agent_command(std::string_view json, std::string &error_message);
} // namespace grannys_house_trials::playtest
