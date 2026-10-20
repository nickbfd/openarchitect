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

TEST_CASE("triangulating a concave outline covers its area") {
    const Polygon ell{{0, 0}, {200, 0}, {200, 100}, {100, 100}, {100, 200}, {0, 200}};
    const auto tris = triangulate(ell);
    REQUIRE(tris.size() == 4);
    double sum = 0;
    for (const auto& t : tris) sum += area(Polygon{ell[t[0]], ell[t[1]], ell[t[2]]});
    CHECK(sum == Approx(area(ell)));
    // Clockwise input works too.
    const Polygon cw(ell.rbegin(), ell.rend());
    CHECK(triangulate(cw).size() == 4);
}

TEST_CASE("moving one edge of an outline keeps its neighbors' directions") {
    const Polygon square{{0, 0}, {100, 0}, {100, 100}, {0, 100}};
    // Edge 1 runs up the right side; its left normal points into the square.
    const Polygon narrower = moveEdge(square, 1, 20);
    CHECK(narrower[1] == Vec2{80, 0});
    CHECK(narrower[2] == Vec2{80, 100});
    CHECK(narrower[0] == Vec2{0, 0});

    // Replacing it with a slanted line makes a trapezoid.
    const Polygon slanted = replaceEdge(square, 1, {100, 0}, {-1, 1});
    REQUIRE(slanted.size() == 4);
    CHECK(slanted[2].x == Approx(0));
    CHECK(slanted[2].y == Approx(100));
    // A line parallel to a neighbor can't meet it.
    CHECK(replaceEdge(square, 1, {50, 0}, {1, 0}).empty());
}

TEST_CASE("segments crossing and a segment clipped to an outline") {
    const auto x = segmentCrossing({{0, 0}, {100, 0}}, {{50, -10}, {50, 10}});
    REQUIRE(x);
    CHECK(x->t == Approx(0.5));
    CHECK(x->u == Approx(0.5));
    CHECK_FALSE(segmentCrossing({{0, 0}, {100, 0}}, {{150, -10}, {150, 10}}));
    const auto inside = clipToPolygon({{-50, 50}, {150, 50}}, {{0, 0}, {100, 0}, {100, 100}, {0, 100}});
    REQUIRE(inside.size() == 1);
    CHECK(inside[0].first == Approx(0.25));
    CHECK(inside[0].second == Approx(0.75));
}
