#pragma once

#include <string>
#include <string_view>

namespace grannys_house_trials::util
{
class NonEmptyString
{
public:
    NonEmptyString() = delete;
    explicit NonEmptyString(std::string value);

    [[nodiscard]] static bool is_valid(std::string_view value) noexcept;
    [[nodiscard]] std::string_view view() const noexcept;
    [[nodiscard]] const std::string &str() const noexcept;

    [[nodiscard]] bool operator==(const NonEmptyString &other) const noexcept = default;

private:
    std::string value_;
};
} // namespace grannys_house_trials::util

