#include "grannys_house_trials/util/non_empty_string.h"

#include <stdexcept>
#include <utility>

namespace grannys_house_trials::util
{
NonEmptyString::NonEmptyString(std::string value)
    : value_(std::move(value))
{
    if (!is_valid(value_))
    {
        throw std::invalid_argument("NonEmptyString requires non-empty text.");
    }
}

bool NonEmptyString::is_valid(std::string_view value) noexcept
{
    return !value.empty();
}

std::string_view NonEmptyString::view() const noexcept
{
    return value_;
}

const std::string &NonEmptyString::str() const noexcept
{
    return value_;
}
} // namespace grannys_house_trials::util

