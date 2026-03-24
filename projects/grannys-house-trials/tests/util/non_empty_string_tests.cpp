#include "grannys_house_trials/util/non_empty_string.h"

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>

using grannys_house_trials::util::NonEmptyString;

TEST_CASE("NonEmptyString stores non-empty text", "[util][non_empty_string]")
{
    const NonEmptyString name("Builder");

    REQUIRE(name.view() == "Builder");
}

TEST_CASE("NonEmptyString rejects empty text", "[util][non_empty_string]")
{
    REQUIRE_THROWS_AS(NonEmptyString(""), std::invalid_argument);
}
