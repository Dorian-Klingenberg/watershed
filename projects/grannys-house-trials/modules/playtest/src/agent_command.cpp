#include "grannys_house_trials/playtest/agent_command.h"

#include <charconv>
#include <cctype>
#include <limits>
#include <string>

namespace grannys_house_trials::playtest
{
namespace
{
[[nodiscard]] std::size_t skip_whitespace(std::string_view text, std::size_t position)
{
    while (position < text.size() && std::isspace(static_cast<unsigned char>(text[position])))
    {
        ++position;
    }

    return position;
}

[[nodiscard]] std::optional<std::string> parse_string_literal(
    std::string_view text,
    std::size_t &position,
    std::string &error_message)
{
    if (position >= text.size() || text[position] != '"')
    {
        error_message = "Expected opening quote for string literal.";
        return std::nullopt;
    }

    ++position; // Skip opening quote
    std::string value;
    value.reserve(32);

    while (position < text.size())
    {
        const char character = text[position++];
        if (character == '\\')
        {
            if (position >= text.size())
            {
                error_message = "Incomplete escape sequence inside string literal.";
                return std::nullopt;
            }

            const char escape_char = text[position++];
            switch (escape_char)
            {
            case '"':
                value += '"';
                break;
            case '\\':
                value += '\\';
                break;
            case 'n':
                value += '\n';
                break;
            case 'r':
                value += '\r';
                break;
            case 't':
                value += '\t';
                break;
            default:
                value += escape_char;
                break;
            }
        }
        else if (character == '"')
        {
            return value;
        }
        else
        {
            value += character;
        }
    }

    error_message = "Unterminated string literal.";
    return std::nullopt;
}

struct FieldResult
{
    std::optional<std::string> value;
    bool found = false;
    bool null_value = false;
    std::string error;
};

[[nodiscard]] std::optional<std::string> extract_string_field(
    std::string_view json,
    std::string_view key,
    bool required,
    std::string &error_message,
    bool *found,
    bool *null_value)
{
    if (found)
    {
        *found = false;
    }

    std::string key_token;
    key_token.reserve(key.size() + 2);
    key_token.push_back('"');
    key_token.append(key);
    key_token.push_back('"');

    const std::size_t token_position = json.find(key_token);
    if (token_position == std::string_view::npos)
    {
        if (required)
        {
            error_message = "Missing required property \"" + std::string(key) + "\".";
        }
        return std::nullopt;
    }

    std::size_t position = token_position + key_token.size();
    position = skip_whitespace(json, position);

    if (position >= json.size() || json[position] != ':')
    {
        error_message = "Expected ':' after \"" + std::string(key) + "\".";
        if (found)
        {
            *found = true;
        }
        return std::nullopt;
    }

    position = skip_whitespace(json, position + 1);
    if (position >= json.size())
    {
        error_message = "Expected value for \"" + std::string(key) + "\".";
        if (found)
        {
            *found = true;
        }
        return std::nullopt;
    }

    if (found)
    {
        *found = true;
    }

    if (json[position] == '"')
    {
        if (null_value)
        {
            *null_value = false;
        }

        return parse_string_literal(json, position, error_message);
    }

    constexpr std::string_view null_token = "null";
    if (json.size() - position >= null_token.size()
        && json.compare(position, null_token.size(), null_token) == 0)
    {
        if (null_value)
        {
            *null_value = true;
        }

        return std::nullopt;
    }

    error_message = "Expected string literal or null for \"" + std::string(key) + "\".";
    return std::nullopt;
}

[[nodiscard]] FieldResult read_field(std::string_view json, std::string_view key, bool required)
{
    FieldResult result;
    result.value = extract_string_field(json, key, required, result.error, &result.found, &result.null_value);
    return result;
}

[[nodiscard]] std::optional<int> extract_int_field(
    std::string_view json,
    std::string_view key,
    std::string &error_message,
    bool *found)
{
    if (found)
    {
        *found = false;
    }

    std::string key_token;
    key_token.reserve(key.size() + 2);
    key_token.push_back('"');
    key_token.append(key);
    key_token.push_back('"');

    const std::size_t token_position = json.find(key_token);
    if (token_position == std::string_view::npos)
    {
        return std::nullopt;
    }

    std::size_t position = token_position + key_token.size();
    position = skip_whitespace(json, position);

    if (position >= json.size() || json[position] != ':')
    {
        error_message = "Expected ':' after \"" + std::string(key) + "\".";
        if (found)
        {
            *found = true;
        }
        return std::nullopt;
    }

    position = skip_whitespace(json, position + 1);
    if (position >= json.size())
    {
        error_message = "Expected value for \"" + std::string(key) + "\".";
        if (found)
        {
            *found = true;
        }
        return std::nullopt;
    }

    if (found)
    {
        *found = true;
    }

    if (json[position] == '"')
    {
        error_message = "Expected integer for \"" + std::string(key) + "\".";
        return std::nullopt;
    }

    const std::size_t value_start = position;
    if (json[position] == '-')
    {
        ++position;
    }

    if (position >= json.size() || !std::isdigit(static_cast<unsigned char>(json[position])))
    {
        error_message = "Expected integer for \"" + std::string(key) + "\".";
        return std::nullopt;
    }

    while (position < json.size() && std::isdigit(static_cast<unsigned char>(json[position])))
    {
        ++position;
    }

    const std::string_view value_token = json.substr(value_start, position - value_start);
    int parsed_value = 0;
    const auto parse_result = std::from_chars(
        value_token.data(),
        value_token.data() + value_token.size(),
        parsed_value);

    if (parse_result.ec != std::errc{} || parse_result.ptr != value_token.data() + value_token.size())
    {
        error_message = "Expected integer for \"" + std::string(key) + "\".";
        return std::nullopt;
    }

    return parsed_value;
}
} // namespace

[[nodiscard]] std::optional<AgentCommand> parse_agent_command(
    std::string_view json,
    std::string &error_message)
{
    error_message.clear();

    FieldResult action_field = read_field(json, "action_id", false);
    if (!action_field.error.empty())
    {
        error_message = action_field.error;
        return std::nullopt;
    }

    if (!action_field.found)
    {
        FieldResult alias_field = read_field(json, "action", false);
        if (!alias_field.error.empty())
        {
            error_message = alias_field.error;
            return std::nullopt;
        }

        if (alias_field.found)
        {
            action_field = alias_field;
        }
    }

    if (!action_field.found || action_field.null_value || !action_field.value.has_value())
    {
        error_message = "Missing required property \"action_id\".";
        return std::nullopt;
    }

    AgentCommand command;
    command.action_id = *action_field.value;

    FieldResult target_field = read_field(json, "target", false);
    if (!target_field.error.empty())
    {
        error_message = target_field.error;
        return std::nullopt;
    }

    if (!target_field.found)
    {
        FieldResult alias_field = read_field(json, "focused_target", false);
        if (!alias_field.error.empty())
        {
            error_message = alias_field.error;
            return std::nullopt;
        }

        if (alias_field.found)
        {
            target_field = alias_field;
        }
    }

    if (target_field.found && !target_field.null_value && target_field.value.has_value())
    {
        const auto maybe_target = sim::from_string(*target_field.value);
        if (!maybe_target)
        {
            error_message = "Unknown target \"" + *target_field.value + "\".";
            return std::nullopt;
        }

        command.target = *maybe_target;
    }

    bool found_selection_x = false;
    bool found_selection_y = false;
    bool found_selection_z = false;
    const auto selection_x = extract_int_field(json, "selection_x", error_message, &found_selection_x);
    if (!error_message.empty())
    {
        return std::nullopt;
    }

    const auto selection_y = extract_int_field(json, "selection_y", error_message, &found_selection_y);
    if (!error_message.empty())
    {
        return std::nullopt;
    }

    const auto selection_z = extract_int_field(json, "selection_z", error_message, &found_selection_z);
    if (!error_message.empty())
    {
        return std::nullopt;
    }

    const int found_selection_values =
        (found_selection_x ? 1 : 0)
        + (found_selection_y ? 1 : 0)
        + (found_selection_z ? 1 : 0);

    if (found_selection_values != 0 && found_selection_values != 3)
    {
        error_message = "selection_x, selection_y, and selection_z must be provided together.";
        return std::nullopt;
    }

    if (found_selection_values == 3)
    {
        command.selection = AgentBlockSelection{
            .x = *selection_x,
            .y = *selection_y,
            .z = *selection_z,
        };
    }

    return command;
}
} // namespace grannys_house_trials::playtest
