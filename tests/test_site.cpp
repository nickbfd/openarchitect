#include "oa/core/dimensions.h"
#include "oa/core/document.h"
#include "oa/core/terrain.h"
#include "oa/structural/takedown.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using Catch::Approx;
using namespace oa;
using namespace oa::structural;

namespace {

void addPoint(Document& d, Vec2 p, double z) {
    ElevationPoint e;
    e.id = d.newId();
    e.position = p;
    e.elevation = z;
    d.terrain.points.push_back(e);
}

ObjectId addWall(Document& d, ObjectId level, Vec2 a, Vec2 b) {
    Wall w;
    w.id = d.newId();
    w.levelId = level;
    w.a = a;
    w.b = b;
    w.thickness = 6;
    d.walls.push_back(w);
    return w.id;
}

} // namespace

TEST_CASE("terrain triangulates its points and interpolates between them") {
    Document d = Document::createDefault();
    // A plane falling 1" per foot to the east: z = -x / 12.
    for (Vec2 p : {Vec2{0, 0}, Vec2{480, 0}, Vec2{480, 480}, Vec2{0, 480}, Vec2{200, 260}})
        addPoint(d, p, -p.x / 12.0);
    const TerrainMesh mesh = terrainMesh(d.terrain);
    REQUIRE(mesh.triangles.size() == 4);
    double total = 0;
    for (const auto& t : mesh.triangles)
        total += area(Polygon{mesh.points[t[0]], mesh.points[t[1]], mesh.points[t[2]]});
    CHECK(total == Approx(480.0 * 480.0));
    CHECK(*terrainHeightAt(d.terrain, {120, 300}) == Approx(-10));
    CHECK_FALSE(terrainHeightAt(d.terrain, {600, 0}));

    // 1 ft contours every 12" of x: x = 144 is the -12" line.
    d.terrain.contourInterval = 12;
    const auto lines = contourLines(d.terrain);
    REQUIRE_FALSE(lines.empty());
    for (const auto& s : lines) {
        CHECK(s.a.x == Approx(-s.elevation * 12).margin(1e-6));
        CHECK(s.b.x == Approx(-s.elevation * 12).margin(1e-6));
    }
    const bool hasMajor = std::any_of(lines.begin(), lines.end(), [](const ContourSegment& s) { return s.major; });
    CHECK(hasMajor);
}

TEST_CASE("exterior dimensions string corners, openings and the overall length") {
    Document d = Document::createDefault();
    const ObjectId l1 = d.levels[0].id;
    const ObjectId south = addWall(d, l1, {0, 0}, {480, 0});
    addWall(d, l1, {480, 0}, {480, 336});
    addWall(d, l1, {480, 336}, {0, 336});
    addWall(d, l1, {0, 336}, {0, 0});
    Opening door;
    door.id = d.newId();
    door.wallId = south;
    door.offset = 120;
    d.openings.push_back(door);

    const auto dims = exteriorDimensions(d, l1);
    auto onSouth = [](const Dimension& x) { return x.a.y == 0 && x.b.y == 0; };
    std::vector<Dimension> south_;
    for (const auto& x : dims)
        if (onSouth(x)) south_.push_back(x);
    // Corner to door to corner, then the overall 40'.
    REQUIRE(south_.size() == 3);
    CHECK(south_[0].length() == Approx(120));
    CHECK(south_[1].length() == Approx(360));
    CHECK(south_[2].length() == Approx(480));
    // Strings sit outside the building, the overall one farthest out.
    for (const auto& x : south_) CHECK((x.b - x.a).normalized().perp().y * x.offset < 0);
    CHECK(std::abs(south_[2].offset) > std::abs(south_[0].offset));
    // East, north and west have no openings: just the overall.
    CHECK(dims.size() == 6);
    CHECK(std::all_of(dims.begin(), dims.end(), [](const Dimension& x) { return x.automatic; }));
}

TEST_CASE("a room with its own floor system changes what the walls below carry") {
    Document d = Document::createDefault();
    const ObjectId l1 = d.levels[0].id, l2 = d.levels[1].id;
    const ObjectId south = addWall(d, l1, {0, 0}, {240, 0});
    const ObjectId north = addWall(d, l1, {240, 240}, {0, 240});
    FloorPlatform floor;
    floor.id = d.newId();
    floor.levelId = l2;
    floor.area = {{0, 0}, {240, 240}};
    floor.joistAxis = Axis::Y;
    d.platforms.push_back(floor);
    Room bath;
    bath.id = d.newId();
    bath.levelId = l2;
    bath.name = "Bath";
    bath.outline = Rect{{0, 0}, {120, 240}}.corners();
    d.rooms.push_back(bath);

    const double before = runTakedown(d).wall(south)->topLoads().peakPlf[LoadCase::Dead];
    CHECK(before == Approx(10 * 10)); // 20 ft span, 10 ft each side, 10 psf

    // A mortar bed shower floor: 2" of slab on the framing over the west half.
    d.rooms[0].floor.kind = FloorSystemKind::Slab;
    d.rooms[0].floor.slabThickness = 2;
    d.rooms[0].floor.deadPsf = 10;
    auto result = runTakedown(d);
    CHECK(result.warnings.empty());
    const auto southLoads = result.wall(south)->topLoads();
    CHECK(southLoads.peakPlf[LoadCase::Dead] == Approx((10 + 25) * 10));
    CHECK(southLoads.peakPlf[LoadCase::Live] == Approx(40 * 10)); // Living by default
    // The east half still carries the level's 10 psf.
    double eastDead = 0;
    for (const auto& l : result.wall(north)->lineLoads) eastDead += l.at(60)[LoadCase::Dead];
    CHECK(eastDead == Approx(10 * 10));

    // A slab room on the lowest level bears on the ground and adds nothing.
    Room garage;
    garage.id = d.newId();
    garage.levelId = l1;
    garage.outline = Rect{{0, 0}, {240, 240}}.corners();
    garage.floor.kind = FloorSystemKind::Slab;
    d.rooms.push_back(garage);
    CHECK(runTakedown(d).wall(south)->topLoads().peakPlf[LoadCase::Dead] == Approx((10 + 25) * 10));
}
