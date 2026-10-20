#include "oa/core/document.h"
#include "oa/core/editing.h"
#include "oa/core/roof_builder.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using Catch::Approx;
using namespace oa;

namespace {

double totalArea(const std::vector<Polygon>& faces) {
    double sum = 0;
    for (const auto& f : faces) sum += f.empty() ? 0.0 : area(f);
    return sum;
}

double totalArea(const std::vector<RoofPlane>& planes) {
    double sum = 0;
    for (const auto& r : planes) sum += area(r.outline);
    return sum;
}

// A 40' by 28' box on the first level, drawn clockwise to show direction
// doesn't matter. Returns the walls south, west, north, east.
struct House {
    Document d = Document::createDefault();
    ObjectId level = d.levels[0].id;
    ObjectId south, west, north, east;
    House() {
        d.wallTypes.clear();
        auto wall = [&](Vec2 a, Vec2 b) {
            Wall w;
            w.id = d.newId();
            w.levelId = level;
            w.a = a;
            w.b = b;
            w.thickness = 6;
            d.walls.push_back(w);
            return w.id;
        };
        south = wall({480, 0}, {0, 0});
        west = wall({0, 0}, {0, 336});
        north = wall({0, 336}, {480, 336});
        east = wall({480, 336}, {480, 0});
    }
};

const Polygon kRect{{0, 0}, {480, 0}, {480, 336}, {0, 336}};

} // namespace

TEST_CASE("a hip roof over a rectangle has two trapezoids and two triangles") {
    const auto faces = skeletonFaces(kRect, {2, 2, 2, 2}); // 6 in 12
    REQUIRE(faces.size() == 4);
    CHECK(faces[0].size() == 4);
    CHECK(faces[1].size() == 3);
    CHECK(area(faces[0]) == Approx((480 + 144) / 2.0 * 168));
    CHECK(area(faces[1]) == Approx(336 * 168 / 2.0));
    CHECK(totalArea(faces) == Approx(480 * 336));
}

TEST_CASE("gable walls go up vertically, leaving two planes") {
    const auto faces = skeletonFaces(kRect, {2, 0, 2, 0});
    CHECK(faces[1].empty());
    CHECK(faces[3].empty());
    CHECK(area(faces[0]) == Approx(480 * 168));
    CHECK(area(faces[2]) == Approx(480 * 168));
}

TEST_CASE("an L-shaped house gets hips, a valley and two ridges that cover it") {
    const Polygon ell{{0, 0}, {480, 0}, {480, 240}, {240, 240}, {240, 480}, {0, 480}};
    const auto faces = skeletonFaces(ell, std::vector<double>(6, 2.0));
    REQUIRE(faces.size() == 6);
    for (const auto& f : faces) CHECK(f.size() >= 3);
    CHECK(totalArea(faces) == Approx(480 * 240 + 240 * 240));
}

TEST_CASE("different pitches on each side make a saltbox") {
    const auto faces = skeletonFaces(kRect, {1, 0, 2, 0}); // 12 in 12 south, 6 in 12 north
    CHECK(totalArea(faces) == Approx(480 * 336));
    // The steeper side reaches the ridge sooner: a third of the depth.
    CHECK(area(faces[0]) == Approx(480 * 112));
}

TEST_CASE("build roof makes a hip roof with its overhangs from the walls") {
    House h;
    const auto planes = buildRoofPlanes(h.d, h.level);
    REQUIRE(planes.size() == 4);
    CHECK(totalArea(planes) == Approx((480 + 36) * (336 + 36)));
    for (const auto& r : planes) {
        CHECK(r.automatic);
        CHECK(r.pitch == Approx(6));
        CHECK_FALSE(r.highBearing);
        // Every plane rises away from its wall, into the house.
        CHECK(r.riseAt({240, 168}) > 0);
    }
}

TEST_CASE("gable end walls give two planes with rake overhangs") {
    House h;
    h.d.wall(h.west)->roof.kind = WallRoof::FullGable;
    h.d.wall(h.east)->roof.kind = WallRoof::FullGable;
    const auto planes = buildRoofPlanes(h.d, h.level);
    REQUIRE(planes.size() == 2);
    for (const auto& r : planes) CHECK(area(r.outline) == Approx((480 + 24) * (168 + 18)));
}

TEST_CASE("a high shed wall makes one plane that bears on it") {
    House h;
    h.d.wall(h.west)->roof.kind = WallRoof::FullGable;
    h.d.wall(h.east)->roof.kind = WallRoof::FullGable;
    h.d.wall(h.north)->roof.kind = WallRoof::HighShed;
    h.d.roofDefaults.pitch = 2.0;
    const auto planes = buildRoofPlanes(h.d, h.level);
    REQUIRE(planes.size() == 1);
    CHECK(planes[0].highBearing);
    CHECK(planes[0].riseAt({0, 336}) == Approx(56));
    CHECK(area(planes[0].outline) == Approx((480 + 24) * (336 + 18 + 12)));
}

TEST_CASE("a wall's own pitch and overhang override the defaults") {
    House h;
    h.d.wall(h.west)->roof.kind = WallRoof::FullGable;
    h.d.wall(h.east)->roof.kind = WallRoof::FullGable;
    h.d.wall(h.south)->roof.pitch = 12;
    h.d.wall(h.south)->roof.overhang = 24;
    const auto planes = buildRoofPlanes(h.d, h.level);
    REQUIRE(planes.size() == 2);
    const auto south = std::find_if(planes.begin(), planes.end(), [](const RoofPlane& r) { return r.pitch == 12; });
    REQUIRE(south != planes.end());
    CHECK(south->overhang == Approx(24));
    CHECK(area(south->outline) == Approx(504 * (112 + 24)));
}

TEST_CASE("an automatic roof follows the walls and keeps its ids") {
    House h;
    h.d.roofDefaults.autoRebuild = true;
    REQUIRE(buildRoof(h.d, h.level) == 4);
    std::vector<ObjectId> ids;
    for (const auto& r : h.d.roofPlanes) ids.push_back(r.id);
    slideWall(h.d, h.east, 48); // left of north-to-south is east
    h.d.refresh();
    REQUIRE(h.d.roofPlanes.size() == 4);
    for (size_t i = 0; i < ids.size(); ++i) CHECK(h.d.roofPlanes[i].id == ids[i]);
    CHECK(totalArea(h.d.roofPlanes) == Approx((528 + 36) * (336 + 36)));
}

TEST_CASE("editing an automatic plane by hand is noticed, a rebuild is not") {
    House h;
    h.d.roofDefaults.autoRebuild = true;
    buildRoof(h.d, h.level);
    const Document built = h.d;

    Document wallsMoved = built;
    slideWall(wallsMoved, h.east, 48);
    CHECK_FALSE(roofEditedByHand(built, wallsMoved));

    Document rebuilt = wallsMoved;
    buildRoof(rebuilt, h.level);
    CHECK_FALSE(roofEditedByHand(wallsMoved, rebuilt));

    Document dragged = built;
    dragged.roofPlanes[0].outline[0] += Vec2{-24, 0};
    CHECK(roofEditedByHand(built, dragged));

    dragged.roofDefaults.autoRebuild = false;
    CHECK_FALSE(roofEditedByHand(built, dragged));
}

TEST_CASE("walls that don't close make no roof") {
    House h;
    h.d.walls.pop_back();
    CHECK(buildRoofPlanes(h.d, h.level).empty());
    CHECK(buildRoof(h.d, h.level) == 0);
}

TEST_CASE("a U-shaped house splits into wings that each get their own ridge") {
    const Polygon u{{0, 0}, {720, 0}, {720, 480}, {480, 480}, {480, 240}, {240, 240}, {240, 480}, {0, 480}};
    const auto faces = skeletonFaces(u, std::vector<double>(8, 2.0));
    REQUIRE(faces.size() == 8);
    for (const auto& f : faces) CHECK(f.size() >= 3);
    CHECK(totalArea(faces) == Approx(720 * 480 - 240 * 240));
}

TEST_CASE("a plus-shaped house with gable ends on every wing") {
    // Four 12' wings around a 12' square.
    const Polygon plus{{144, 0},   {288, 0},   {288, 144}, {432, 144}, {432, 288}, {288, 288},
                       {288, 432}, {144, 432}, {144, 288}, {0, 288},   {0, 144},   {144, 144}};
    std::vector<double> speed(12, 2.0);
    for (int end : {0, 3, 6, 9}) speed[end] = 0.0;
    const auto faces = skeletonFaces(plus, speed);
    double sum = 0;
    for (size_t i = 0; i < faces.size(); ++i) {
        if (speed[i] == 0.0) CHECK(faces[i].empty());
        else CHECK(faces[i].size() >= 3);
        sum += faces[i].empty() ? 0.0 : area(faces[i]);
    }
    CHECK(sum == Approx(144 * 432 * 2 - 144 * 144));
}

namespace {

const RoofPlane* planeOver(const std::vector<RoofPlane>& planes, Vec2 a, Vec2 b, double baseHeight = 0.0) {
    for (const auto& r : planes)
        if (distance(r.a, a) < 1e-6 && distance(r.b, b) < 1e-6 && std::abs(r.baseHeight - baseHeight) < 1e-6) return &r;
    return nullptr;
}

double maxX(const Polygon& p) {
    double x = -1e30;
    for (Vec2 q : p) x = std::max(x, q.x);
    return x;
}

double minRise(const RoofPlane& r) {
    double z = 1e30;
    for (Vec2 q : r.outline) z = std::min(z, r.riseAt(q));
    return z;
}

} // namespace

TEST_CASE("an edge that changes speed partway up gets a second face above the break") {
    // Steep 12 in 12 to 96" up, then 4 in 12, with gable ends.
    const std::vector<SkeletonEdge> edges{{1.0, 96.0, 3.0}, {0.0}, {1.0, 96.0, 3.0}, {0.0}};
    const auto faces = skeletonFaces(kRect, edges);
    REQUIRE(faces.size() == 6);
    double sum = 0;
    for (const auto& f : faces) sum += f.outline.empty() ? 0.0 : area(f.outline);
    CHECK(sum == Approx(480 * 336));
    CHECK(faces[0].phase == 0);
    CHECK(area(faces[0].outline) == Approx(480 * 96));
    const auto upper = std::find_if(faces.begin(), faces.end(), [](const SkeletonFace& f) { return f.edge == 0 && f.phase == 1; });
    REQUIRE(upper != faces.end());
    CHECK(upper->startHeight == Approx(96));
    CHECK(area(upper->outline) == Approx(480 * 72));
}

TEST_CASE("a gambrel roof breaks to a shallower upper pitch that meets the lower one") {
    House h;
    for (ObjectId id : {h.south, h.north}) {
        h.d.wall(id)->roof.pitch = 12;
        h.d.wall(id)->roof.upperPitch = 4;
        h.d.wall(id)->roof.upperStart = 96;
    }
    for (ObjectId id : {h.east, h.west}) h.d.wall(id)->roof.kind = WallRoof::FullGable;
    const auto planes = buildRoofPlanes(h.d, h.level);
    REQUIRE(planes.size() == 4);
    const RoofPlane* lower = planeOver(planes, {0, 0}, {480, 0});
    const RoofPlane* upper = planeOver(planes, {0, 0}, {480, 0}, 96.0 * (1 - 1.0 / 3.0));
    REQUIRE(lower);
    REQUIRE(upper);
    CHECK(upper->pitch == 4);
    // The two meet at the break, 96" up, and the ridge is 24" higher.
    CHECK(lower->riseAt({240, 96}) == Approx(96));
    CHECK(upper->riseAt({240, 96}) == Approx(96));
    CHECK(upper->riseAt({240, 168}) == Approx(120));
    // The upper plane runs out over the gable rakes like the lower one.
    CHECK(maxX(upper->outline) == Approx(492));
    CHECK(maxX(lower->outline) == Approx(492));
}

TEST_CASE("a mansard has a steep plane and a shallow one on every wall") {
    House h;
    for (ObjectId id : {h.south, h.west, h.north, h.east}) {
        h.d.wall(id)->roof.pitch = 24;
        h.d.wall(id)->roof.upperPitch = 3;
        h.d.wall(id)->roof.upperStart = 96;
    }
    const auto planes = buildRoofPlanes(h.d, h.level);
    REQUIRE(planes.size() == 8);
    int upper = 0;
    for (const auto& r : planes) {
        if (r.pitch != 3) continue;
        ++upper;
        // The upper hip roof sits on a 384" by 240" deck and peaks 30" above it.
        double top = 0;
        for (Vec2 q : r.outline) top = std::max(top, r.riseAt(q));
        CHECK(top == Approx(126));
        CHECK(minRise(r) == Approx(96));
    }
    CHECK(upper == 4);
}

TEST_CASE("a Dutch gable hips up to its start height, then stands up as a gable") {
    House h;
    for (ObjectId id : {h.east, h.west}) {
        h.d.wall(id)->roof.kind = WallRoof::DutchGable;
        h.d.wall(id)->roof.upperStart = 24; // 48" in at 6 in 12
    }
    const auto planes = buildRoofPlanes(h.d, h.level);
    REQUIRE(planes.size() == 4);
    const RoofPlane* east = planeOver(planes, {480, 0}, {480, 336});
    REQUIRE(east);
    double top = 0;
    for (Vec2 q : east->outline) top = std::max(top, east->riseAt(q));
    CHECK(top == Approx(24));
    // The little hip has the eave overhang, not the gable one.
    CHECK(maxX(east->outline) == Approx(498));
    const RoofPlane* south = planeOver(planes, {0, 0}, {480, 0});
    REQUIRE(south);
    // The ridge runs to the gable 48" in, then out over it by the rake overhang.
    double ridgeEnd = -1e30;
    for (Vec2 q : south->outline)
        if (std::abs(q.y - 168) < 1e-6) ridgeEnd = std::max(ridgeEnd, q.x);
    CHECK(ridgeEnd == Approx(444));
}

TEST_CASE("a full gable wall with an upper pitch makes a half hip") {
    House h;
    for (ObjectId id : {h.east, h.west}) {
        h.d.wall(id)->roof.kind = WallRoof::FullGable;
        h.d.wall(id)->roof.upperPitch = 6;
        h.d.wall(id)->roof.upperStart = 60;
    }
    const auto planes = buildRoofPlanes(h.d, h.level);
    REQUIRE(planes.size() == 4);
    const RoofPlane* east = planeOver(planes, {480, 0}, {480, 336}, 60.0);
    REQUIRE(east);
    // The clipped hip starts 60" up and hangs 12" past the gable.
    CHECK(maxX(east->outline) == Approx(492));
    CHECK(minRise(*east) == Approx(54));
    const RoofPlane* south = planeOver(planes, {0, 0}, {480, 0});
    REQUIRE(south);
    double ridgeEnd = -1e30;
    for (Vec2 q : south->outline)
        if (std::abs(q.y - 168) < 1e-6) ridgeEnd = std::max(ridgeEnd, q.x);
    CHECK(ridgeEnd == Approx(432));
    // Side and hip planes meet along the hip, out to the hip's eave.
    for (Vec2 q : east->outline)
        for (Vec2 p : south->outline)
            if (distance(p, q) < 1e-6) CHECK(east->riseAt(q) == Approx(south->riseAt(p)));
}

TEST_CASE("heel height and sheathing raise the roof surface above the plate") {
    RoofPlane r;
    r.pitch = 6;
    r.rafterMemberId = "dfl-no2-1-2x10";
    CHECK(r.rafterDepth() == Approx(9.25));
    // Two thirds of the rafter's plumb depth is left above the birdsmouth.
    CHECK(r.effectiveHeel() == Approx(9.25 * std::sqrt(1.25) * 2 / 3));
    r.heelHeight = 12;
    CHECK(r.effectiveHeel() == 12);
    CHECK(r.surfaceAbovePlate() == Approx(12 + 0.4375 * std::sqrt(1.25)));
    r.framing = RoofFraming::Trusses;
    r.heelHeight = 0;
    CHECK(r.effectiveHeel() == Approx(3.5 + 3.5 * std::sqrt(1.25)));
    CHECK(sawnLumberDepth("2x12") == Approx(11.25));
    CHECK(sawnLumberDepth("glulam-24f-5.125x12") == 0);
    CHECK(lumberSize("syp-no2-2-2x8") == "2x8");
}

TEST_CASE("build roof planes take their framing from the build roof defaults") {
    House h;
    h.d.roofDefaults.rafterMemberId = "dfl-no2-1-2x12";
    h.d.roofDefaults.rafterSpacing = 24;
    h.d.roofDefaults.heelHeight = 10;
    h.d.roofDefaults.sheathingThickness = 0.625;
    for (const auto& r : buildRoofPlanes(h.d, h.level)) {
        CHECK(r.rafterMemberId == "dfl-no2-1-2x12");
        CHECK(r.rafterSpacing == 24);
        CHECK(r.heelHeight == 10);
        CHECK(r.sheathingThickness == 0.625);
    }
}

TEST_CASE("an automatic rebuild keeps the rafter size set on a plane, build roof resets it") {
    House h;
    h.d.roofDefaults.autoRebuild = true;
    REQUIRE(buildRoof(h.d, h.level) == 4);
    RoofPlane& first = h.d.roofPlanes.front();
    const ObjectId id = first.id;
    const Document before = h.d;
    first.rafterMemberId = "dfl-no2-1-2x10";
    first.heelHeight = 11;
    CHECK_FALSE(roofEditedByHand(before, h.d));
    // Moving a wall rebuilds the roof; the plane keeps its framing.
    h.d.wall(h.north)->a.y = h.d.wall(h.north)->b.y = 360;
    h.d.wall(h.west)->b.y = h.d.wall(h.east)->a.y = 360;
    h.d.refresh();
    const RoofPlane* again = h.d.roofPlane(id);
    REQUIRE(again);
    CHECK(again->rafterMemberId == "dfl-no2-1-2x10");
    CHECK(again->heelHeight == 11);
    buildRoof(h.d, h.level);
    CHECK(h.d.roofPlane(id)->rafterMemberId == h.d.roofDefaults.rafterMemberId);
}
