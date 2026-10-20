#include "oa/core/document.h"
#include "oa/core/rooms.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using Catch::Approx;
using namespace oa;

namespace {

ObjectId addWall(Document& d, Vec2 a, Vec2 b, double thickness = 6.0) {
    Wall w;
    w.id = d.newId();
    w.levelId = d.levels[0].id;
    w.a = a;
    w.b = b;
    w.thickness = thickness;
    d.walls.push_back(w);
    return w.id;
}

// A 20 ft by 10 ft box of 6" walls, drawn clockwise to show direction doesn't matter.
Document box() {
    Document d = Document::createDefault();
    d.wallTypes.clear(); // keep hand-set thicknesses
    addWall(d, {0, 0}, {0, 120});
    addWall(d, {0, 120}, {240, 120});
    addWall(d, {240, 120}, {240, 0});
    addWall(d, {240, 0}, {0, 0});
    return d;
}

std::vector<double> areas(const std::vector<Polygon>& rooms) {
    std::vector<double> out;
    for (const auto& r : rooms) out.push_back(area(r));
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace

TEST_CASE("walls that close a loop make a room inside their faces") {
    const Document d = box();
    const auto rooms = findRoomOutlines(d, d.levels[0].id);
    REQUIRE(rooms.size() == 1);
    CHECK(area(rooms[0]) == Approx(234.0 * 114.0));
    CHECK(signedArea(rooms[0]) > 0);
}

TEST_CASE("an open run of walls makes no room") {
    Document d = box();
    d.walls.pop_back();
    CHECK(findRoomOutlines(d, d.levels[0].id).empty());
}

TEST_CASE("a wall or divider across the box splits it in two") {
    Document d = box();
    addWall(d, {120, 0}, {120, 120}, 0.0);
    auto rooms = findRoomOutlines(d, d.levels[0].id);
    REQUIRE(rooms.size() == 2);
    const auto a = areas(rooms);
    CHECK(a[0] == Approx(117.0 * 114.0));
    CHECK(a[1] == Approx(117.0 * 114.0));

    // A wall that crosses past the box and stops short still splits it.
    d.walls.pop_back();
    addWall(d, {80, -24}, {80, 140}, 4.0);
    rooms = findRoomOutlines(d, d.levels[0].id);
    CHECK(rooms.size() == 2);
}

TEST_CASE("rooms keep their names when walls change around them") {
    Document d = box();
    updateRooms(d);
    REQUIRE(d.rooms.size() == 1);
    d.rooms[0].name = "Great room";
    d.rooms[0].type = RoomType::Living;
    const ObjectId id = d.rooms[0].id;

    // Split off a bedroom at the east end: the bigger west part keeps the name.
    addWall(d, {180, 0}, {180, 120});
    updateRooms(d);
    REQUIRE(d.rooms.size() == 2);
    const auto named = std::find_if(d.rooms.begin(), d.rooms.end(), [&](const Room& r) { return r.id == id; });
    REQUIRE(named != d.rooms.end());
    CHECK(named->name == "Great room");
    CHECK(named->type == RoomType::Living);
    CHECK(contains(named->outline, {60, 60}));

    // Removing a box wall opens it up and the rooms go away.
    d.walls.erase(d.walls.begin() + 1);
    updateRooms(d);
    CHECK(d.rooms.empty());
}
