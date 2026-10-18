#include "oa/core/units.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;
using oa::formatFeetInches;
using oa::parseLength;

TEST_CASE("parses the ways people type lengths") {
    CHECK(*parseLength("12'") == Approx(144));
    CHECK(*parseLength("12'6\"") == Approx(150));
    CHECK(*parseLength("12' 6 1/2\"") == Approx(150.5));
    CHECK(*parseLength("12'-6\"") == Approx(150));
    CHECK(*parseLength("150\"") == Approx(150));
    CHECK(*parseLength("150") == Approx(150));
    CHECK(*parseLength("6 1/2") == Approx(6.5));
    CHECK(*parseLength("3/4\"") == Approx(0.75));
    CHECK(*parseLength("12.5'") == Approx(150));
    CHECK(*parseLength("-3\"") == Approx(-3));
    CHECK(*parseLength("  9' ") == Approx(108));
}

TEST_CASE("rejects text that is not a length") {
    CHECK_FALSE(parseLength(""));
    CHECK_FALSE(parseLength("abc"));
    CHECK_FALSE(parseLength("12' x"));
    CHECK_FALSE(parseLength("1/0"));
}

TEST_CASE("formats feet and inches") {
    CHECK(formatFeetInches(150.5) == "12'-6 1/2\"");
    CHECK(formatFeetInches(144) == "12'-0\"");
    CHECK(formatFeetInches(0.0625) == "0'-0 1/16\"");
    CHECK(formatFeetInches(-18) == "-1'-6\"");
    CHECK(formatFeetInches(11.999) == "1'-0\"");
}
