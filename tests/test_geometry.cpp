#include "oa/core/geometry.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;
using namespace oa;

TEST_CASE("collinear overlap of a wall over another") {
    const Segment base{{0, 0}, {240, 0}};
    auto ov = collinearOverlap(base, {{120, 2}, {360, 2}}, 3.0);
    REQUIRE(ov);
    CHECK(ov->start == Approx(120));
    CHECK(ov->end == Approx(240));

    // Reversed direction still overlaps.
    ov = collinearOverlap(base, {{200, 0}, {-50, 0}}, 3.0);
    REQUIRE(ov);
    CHECK(ov->start == Approx(0));
    CHECK(ov->end == Approx(200));

    CHECK_FALSE(collinearOverlap(base, {{0, 10}, {240, 10}}, 3.0)); // too far away
    CHECK_FALSE(collinearOverlap(base, {{0, 0}, {0, 240}}, 3.0));   // perpendicular
    CHECK_FALSE(collinearOverlap(base, {{300, 0}, {400, 0}}, 3.0)); // past the end
}

TEST_CASE("polygon area, centroid and containment") {
    const Polygon square{{0, 0}, {10, 0}, {10, 10}, {0, 10}};
    CHECK(signedArea(square) == Approx(100));
    CHECK(centroid(square).x == Approx(5));
    CHECK(contains(square, {5, 5}));
    CHECK_FALSE(contains(square, {15, 5}));
}

TEST_CASE("segment projection") {
    const Segment s{{0, 0}, {10, 0}};
    CHECK(s.project({5, 3}) == Approx(0.5));
    CHECK(s.distanceTo({5, 3}) == Approx(3));
    CHECK(s.distanceTo({-4, 3}) == Approx(5));
}
