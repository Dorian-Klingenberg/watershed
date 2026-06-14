#include "grannys_house_trials/playtest/agent_command.h"

#include <catch2/catch_test_macros.hpp>

using grannys_house_trials::playtest::AgentCommand;
using grannys_house_trials::playtest::parse_agent_command;

TEST_CASE("AgentCommand parser reads simple action", "[playtest][agent_command]")
{
    std::string error;
    const std::string payload = R"({"action_id":"look"})";
    const auto command = parse_agent_command(payload, error);

    REQUIRE(command);
    REQUIRE(error.empty());
    REQUIRE(command->action_id == "look");
    REQUIRE_FALSE(command->target.has_value());
}

TEST_CASE("AgentCommand parser decodes target names", "[playtest][agent_command]")
{
    std::string error;
    const std::string payload = R"({"action_id":"pack_flat_stone_run","target":"flat_stone_run"})";
    const auto command = parse_agent_command(payload, error);

    REQUIRE(command);
    REQUIRE(error.empty());
    REQUIRE(command->action_id == "pack_flat_stone_run");
    REQUIRE(command->target.has_value());
}

TEST_CASE("AgentCommand parser rejects unknown targets", "[playtest][agent_command]")
{
    std::string error;
    const std::string payload = R"({"action_id":"look","target":"unknown_target"})";
    const auto command = parse_agent_command(payload, error);

    REQUIRE_FALSE(command);
    REQUIRE(!error.empty());
    REQUIRE(error.find("Unknown target") != std::string::npos);
}

TEST_CASE("AgentCommand parser requires action_id", "[playtest][agent_command]")
{
    std::string error;
    const std::string payload = R"({"target":"cellar_edge"})";
    const auto command = parse_agent_command(payload, error);

    REQUIRE_FALSE(command);
    REQUIRE(error == "Missing required property \"action_id\".");
}

TEST_CASE("AgentCommand parser accepts action alias", "[playtest][agent_command]")
{
    std::string error;
    const std::string payload = R"({"action":"reset_round"})";
    const auto command = parse_agent_command(payload, error);

    REQUIRE(command);
    REQUIRE(error.empty());
    REQUIRE(command->action_id == "reset_round");
}

TEST_CASE("AgentCommand parser reads selection coordinates", "[playtest][agent_command]")
{
    std::string error;
    const std::string payload = R"({"action_id":"select_block","selection_x":89,"selection_y":0,"selection_z":38})";
    const auto command = parse_agent_command(payload, error);

    REQUIRE(command);
    REQUIRE(error.empty());
    REQUIRE(command->selection.has_value());
    REQUIRE(command->selection->x == 89);
    REQUIRE(command->selection->y == 0);
    REQUIRE(command->selection->z == 38);
}

TEST_CASE("AgentCommand parser requires full selection coordinate triplet", "[playtest][agent_command]")
{
    std::string error;
    const std::string payload = R"({"action_id":"select_block","selection_x":89,"selection_z":38})";
    const auto command = parse_agent_command(payload, error);

    REQUIRE_FALSE(command);
    REQUIRE(error == "selection_x, selection_y, and selection_z must be provided together.");
}
