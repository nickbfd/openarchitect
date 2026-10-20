#include "oa/core/dimensions.h"
#include "oa/core/document.h"
#include "oa/core/editing.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using Catch::Approx;
using namespace oa;

namespace {

ObjectId addWall(Document& d, Vec2 a, Vec2 b) {
    Wall w;
    w.id = d.newId();
    w.levelId = d.levels[0].id;
    w.a = a;
    w.b = b;
    w.thickness = 6.0;
    d.walls.push_back(w);
    return w.id;
}

// A 40 ft by 28 ft box drawn counterclockwise from the southwest corner.
struct Box {
    Document d = Document::createDefault();
    ObjectId south, east, north, west;
    Box() {
        south = addWall(d, {0, 0}, {480, 0});
        east = addWall(d, {480, 0}, {480, 336});
        north = addWall(d, {480, 336}, {0, 336});
        west = addWall(d, {0, 336}, {0, 0});
    }
};

double overallWidth(const Document& d) {
    double best = 0;
    for (const auto& x : d.dimensions)
        if (std::abs(x.a.y - x.b.y) < 1e-6) best = std::max(best, x.length());
    return best;
}

} // namespace

TEST_CASE("moving a wall stretches the walls joined to it") {
    Box b;
    slideWall(b.d, b.east, -24); // right of south-to-north is east: negative is out
    const Wall* east = b.d.wall(b.east);
    CHECK(east->a.x == Approx(504));
    CHECK(east->b.x == Approx(504));
    CHECK(b.d.wall(b.south)->b.x == Approx(504));
    CHECK(b.d.wall(b.north)->a.x == Approx(504));
    CHECK(b.d.wall(b.west)->a.x == Approx(0));
}

TEST_CASE("a new wall length moves the end, the start or both") {
    Box b;
    setWallLength(b.d, b.south, 528, 0);
    CHECK(b.d.wall(b.south)->b.x == Approx(528));
    CHECK(b.d.wall(b.east)->a.x == Approx(528)); // the joined corner follows
    CHECK(b.d.wall(b.west)->b.x == Approx(0));

    Box c;
    setWallLength(c.d, c.south, 528, 1);
    CHECK(c.d.wall(c.south)->a.x == Approx(-48));
    CHECK(c.d.wall(c.west)->b.x == Approx(-48));

    Box e;
    setWallLength(e.d, e.south, 528, 2);
    CHECK(e.d.wall(e.south)->a.x == Approx(-24));
    CHECK(e.d.wall(e.south)->b.x == Approx(504));
}

TEST_CASE("build new floor copies the exterior walls up a level") {
    Box b;
    addWall(b.d, {240, 0}, {240, 336}); // interior, untyped and not bearing
    b.d.walls.back().bearing = false;
    for (auto& w : b.d.walls)
        if (w.id != b.d.walls.back().id) w.bearing = true;
    auto wallsOn = [&](ObjectId id) {
        return std::count_if(b.d.walls.begin(), b.d.walls.end(), [&](const Wall& w) { return w.levelId == id; });
    };
    // The default plan's empty Floor 2 gets the four exterior walls.
    const ObjectId up = buildNewFloor(b.d);
    CHECK(up == b.d.levels[1].id);
    CHECK(wallsOn(up) == 4);
    // Again: a Floor 3 goes on top, copied from Floor 2.
    const Level two = b.d.levels[1];
    const ObjectId three = buildNewFloor(b.d);
    const Level* l3 = b.d.level(three);
    REQUIRE(l3);
    CHECK(b.d.levels.size() == 3);
    CHECK(l3->elevation == Approx(two.elevation + two.wallHeight + two.floorThickness));
    CHECK(wallsOn(three) == 4);
}

TEST_CASE("automatic dimensions follow a moved wall") {
    Box b;
    b.d.dimensionTo = DimensionTo::FramingCenter;
    for (Dimension x : exteriorDimensions(b.d, b.d.levels[0].id)) {
        x.id = b.d.newId();
        b.d.dimensions.push_back(x);
    }
    CHECK(overallWidth(b.d) == Approx(480));
    slideWall(b.d, b.east, -24);
    b.d.refresh();
    CHECK(overallWidth(b.d) == Approx(504));

    b.d.autoRefreshDimensions = false;
    slideWall(b.d, b.east, 24);
    b.d.refresh();
    CHECK(overallWidth(b.d) == Approx(504));
}

TEST_CASE("doors and windows keep their place when their wall stretches") {
    Box b;
    Opening win;
    win.id = b.d.newId();
    win.wallId = b.north; // runs east to west, so its offset counts from the east end
    win.offset = 100;
    b.d.openings.push_back(win);
    slideWall(b.d, b.east, -24);
    CHECK(b.d.wall(b.north)->a.x == Approx(504));
    CHECK(b.d.opening(win.id)->offset == Approx(124)); // still 380" from the west wall
}
