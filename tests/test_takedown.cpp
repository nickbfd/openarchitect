#include "oa/core/document.h"
#include "oa/core/roof_builder.h"
#include "oa/structural/rafters.h"
#include "oa/structural/takedown.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using Catch::Approx;
using namespace oa;
using namespace oa::structural;

namespace {

ObjectId addWall(Document& d, ObjectId level, Vec2 a, Vec2 b) {
    Wall w;
    w.id = d.newId();
    w.levelId = level;
    w.a = a;
    w.b = b;
    d.walls.push_back(w);
    return w.id;
}

// A 40 ft by 28 ft box of bearing walls on Floor 1.
struct Box {
    Document doc = Document::createDefault();
    ObjectId l1, l2, south, east, north, west;

    Box() {
        l1 = doc.levels[0].id;
        l2 = doc.levels[1].id;
        south = addWall(doc, l1, {0, 0}, {480, 0});
        east = addWall(doc, l1, {480, 0}, {480, 336});
        north = addWall(doc, l1, {480, 336}, {0, 336});
        west = addWall(doc, l1, {0, 336}, {0, 0});
    }

    Roof& addRoof(RoofStyle style) {
        Roof r;
        r.id = doc.newId();
        r.levelId = l1;
        r.style = style;
        r.footprint = {{0, 0}, {480, 336}};
        r.ridgeAxis = Axis::X;
        r.pitch = 6;
        r.overhang = 18;
        r.hasCeiling = false;
        doc.roofs.push_back(r);
        return doc.roofs.back();
    }
};

const double kDeadHoriz = 15.0 / std::cos(std::atan(0.5)); // 16.77 psf

} // namespace

TEST_CASE("gable roof with ridge board loads the eave walls") {
    Box box;
    box.addRoof(RoofStyle::Gable);
    const auto result = runTakedown(box.doc);

    // Tributary = 14 ft half span + 1.5 ft overhang = 15.5 ft.
    const auto south = result.wall(box.south)->topLoads();
    CHECK(south.peakPlf[LoadCase::Dead] == Approx(kDeadHoriz * 15.5).epsilon(1e-6));
    CHECK(south.peakPlf[LoadCase::RoofLive] == Approx(20 * 15.5));
    CHECK(south.peakPlf[LoadCase::Snow] == Approx(23.1 * 15.5));
    CHECK(south.governingCombo == "D + S");

    const auto north = result.wall(box.north)->topLoads();
    CHECK(north.peakPlf[LoadCase::Dead] == Approx(kDeadHoriz * 15.5).epsilon(1e-6));
    // Gable end walls carry no roof load with a ridge board.
    CHECK(result.wall(box.east)->topLoads().peakPlf[LoadCase::Dead] == Approx(0));
    CHECK(result.warnings.empty());
}

TEST_CASE("ceiling joists add attic loads to the eave walls") {
    Box box;
    box.addRoof(RoofStyle::Gable).hasCeiling = true;
    const auto south = runTakedown(box.doc).wall(box.south)->topLoads();
    // Ceiling: 14 ft tributary at 10 psf dead and 10 psf attic live.
    CHECK(south.peakPlf[LoadCase::Dead] == Approx(kDeadHoriz * 15.5 + 140).epsilon(1e-6));
    CHECK(south.peakPlf[LoadCase::Live] == Approx(140));
}

TEST_CASE("structural ridge splits the load and posts down at the gable ends") {
    Box box;
    box.addRoof(RoofStyle::Gable).structuralRidge = true;
    const auto result = runTakedown(box.doc);

    const double run = 168, o = 18;
    const double wallTrib = (run + o) * (run + o) / (2 * run);
    const double ridgeTrib = (run * run - o * o) / run;
    CHECK(result.wall(box.south)->topLoads().peakPlf[LoadCase::Dead] ==
          Approx(kDeadHoriz * wallTrib / 12).epsilon(1e-6));

    const SupportLoads* ridge = result.ridgeBeam(box.doc.roofs[0].id);
    REQUIRE(ridge);
    const double ridgePlf = kDeadHoriz * ridgeTrib / 12;
    CHECK(ridge->topLoads().peakPlf[LoadCase::Dead] == Approx(ridgePlf).epsilon(1e-6));

    // Each end of the 40 ft ridge beam lands on a gable end wall.
    const SupportLoads* east = result.wall(box.east);
    REQUIRE(east->pointLoads.size() == 1);
    CHECK(east->pointLoads[0].p[LoadCase::Dead] == Approx(ridgePlf * 40 / 2).epsilon(1e-4));
    CHECK(east->pointLoads[0].position == Approx(168));
}

TEST_CASE("hip roof sends the whole roof area to the walls") {
    Box box;
    box.addRoof(RoofStyle::Hip);
    const auto result = runTakedown(box.doc);

    double total = 0.0;
    for (const auto& s : result.supports)
        for (const auto& l : s.lineLoads) total += l.total()[LoadCase::Dead];
    // Footprint plus the overhang strip on all four sides.
    const double area = (480.0 * 336 + 18.0 * 2 * (480 + 336)) / 144.0;
    CHECK(total == Approx(kDeadHoriz * area).epsilon(1e-6));

    // End walls peak at mid-length: half of the 28 ft width plus overhang.
    CHECK(result.wall(box.east)->topLoads().peakPlf[LoadCase::Dead] ==
          Approx(kDeadHoriz * (14 + 1.5)).epsilon(1e-6));
}

TEST_CASE("floor framing splits between walls and a center beam") {
    Box box;
    // Remove the 28 ft box and build a 24 ft deep one with a beam at mid-depth.
    box.doc.walls.clear();
    const ObjectId south = addWall(box.doc, box.l1, {0, 0}, {480, 0});
    const ObjectId north = addWall(box.doc, box.l1, {480, 288}, {0, 288});
    const ObjectId west = addWall(box.doc, box.l1, {0, 288}, {0, 0});
    addWall(box.doc, box.l1, {480, 0}, {480, 288});
    Beam beam;
    beam.id = box.doc.newId();
    beam.levelId = box.l1;
    beam.a = {0, 144};
    beam.b = {480, 144};
    box.doc.beams.push_back(beam);

    FloorPlatform floor;
    floor.id = box.doc.newId();
    floor.levelId = box.l2;
    floor.area = {{0, 0}, {480, 288}};
    floor.joistAxis = Axis::Y;
    box.doc.platforms.push_back(floor);

    const auto result = runTakedown(box.doc);
    const auto beamLoads = result.beam(beam.id)->topLoads();
    CHECK(beamLoads.peakPlf[LoadCase::Dead] == Approx(120));
    CHECK(beamLoads.peakPlf[LoadCase::Live] == Approx(480));
    CHECK(result.wall(south)->topLoads().peakPlf[LoadCase::Live] == Approx(240));
    CHECK(result.wall(north)->topLoads().peakPlf[LoadCase::Live] == Approx(240));

    // The beam's 2400 lb dead and 9600 lb live reactions land on the end walls.
    const SupportLoads* w = result.wall(west);
    REQUIRE(w->pointLoads.size() == 1);
    CHECK(w->pointLoads[0].p[LoadCase::Dead] == Approx(2400).epsilon(1e-4));
    CHECK(w->pointLoads[0].p[LoadCase::Live] == Approx(9600).epsilon(1e-4));
    CHECK(result.warnings.empty());
}

TEST_CASE("upper walls stack onto walls below, and unsupported walls are flagged") {
    Box box;
    const ObjectId upper = addWall(box.doc, box.l2, {0, 0}, {480, 0});
    addWall(box.doc, box.l2, {120, 168}, {360, 168}); // nothing below it

    const auto result = runTakedown(box.doc);
    const double selfWeight = 10.0 * 97.125 / 12.0;
    CHECK(result.wall(upper)->selfWeight.wStart[LoadCase::Dead] == Approx(selfWeight));
    CHECK(result.wall(box.south)->topLoads().peakPlf[LoadCase::Dead] == Approx(selfWeight));

    REQUIRE(result.warnings.size() == 1);
    CHECK(result.warnings[0].find("no bearing wall or beam beneath") != std::string::npos);
    CHECK(result.warnings[0].find("20.0 ft") != std::string::npos);
}

TEST_CASE("header loads come from the wall above the opening") {
    Box box;
    box.addRoof(RoofStyle::Gable);
    Opening o;
    o.id = box.doc.newId();
    o.wallId = box.south;
    o.offset = 240;
    o.width = 36;
    o.height = 48;
    o.sillHeight = 36;
    box.doc.openings.push_back(o);

    const auto result = runTakedown(box.doc);
    const BeamLoads h = headerLoads(box.doc, result, o.id);
    CHECK(h.span == Approx(39));
    const double wallAbove = 10.0 * (97.125 - 84) / 12.0;
    CHECK(h.totalLoad()[LoadCase::Dead] == Approx((kDeadHoriz * 15.5 + wallAbove) * 39 / 12).epsilon(1e-6));
    CHECK(h.totalLoad()[LoadCase::Snow] == Approx(23.1 * 15.5 * 39 / 12).epsilon(1e-6));

    // A roof-only header is checked at roof deflection limits and gets a recommendation.
    const auto picks = recommendMembers(h, criteriaFor(h));
    REQUIRE_FALSE(picks.empty());
    CHECK(picks.front().member->d <= 7.25);
}

TEST_CASE("a single roof plane splits between its eave and high side") {
    Document doc = Document::createDefault();
    const ObjectId l1 = doc.levels[0].id;
    const ObjectId low = addWall(doc, l1, {0, 0}, {240, 0});
    const ObjectId high = addWall(doc, l1, {240, 144}, {0, 144});
    RoofPlane p;
    p.id = doc.newId();
    p.levelId = l1;
    p.a = {0, 0};
    p.b = {240, 0};
    p.pitch = 3;
    p.outline = RoofPlane::rectangle(p.a, p.b, 144, 12);
    doc.roofPlanes.push_back(p);

    const auto result = runTakedown(doc);
    const double dead = 15.0 / std::cos(std::atan(0.25));
    const double eaveDepth = (144.0 + 12) * (144 + 12) / (2 * 144.0); // 84.5"
    const double highDepth = (144.0 * 144 - 12 * 12) / (2 * 144.0);  // 71.5"
    const auto eave = result.wall(low)->topLoads();
    CHECK(eave.peakPlf[LoadCase::Dead] == Approx(dead * eaveDepth / 12).epsilon(1e-6));
    CHECK(eave.peakPlf[LoadCase::RoofLive] == Approx(20 * eaveDepth / 12));
    CHECK(result.wall(high)->topLoads().peakPlf[LoadCase::RoofLive] == Approx(20 * highDepth / 12));
    // Together they carry the whole plane, overhang included.
    CHECK((eaveDepth + highDepth) == Approx(144 + 12));
    CHECK(result.warnings.empty());

    // With nothing under the high side, it says so.
    doc.remove(high);
    const auto lonely = runTakedown(doc);
    REQUIRE(lonely.warnings.size() == 1);
    CHECK(lonely.warnings[0].find("of high side has no bearing") != std::string::npos);
}

TEST_CASE("truss ceilings bear on the eave walls, rafter ceilings on interior walls too") {
    Box box;
    const ObjectId middle = addWall(box.doc, box.l1, {0, 168}, {480, 168});
    box.addRoof(RoofStyle::Gable).hasCeiling = true;

    const auto rafters = runTakedown(box.doc);
    // Ceiling joists span 14 ft to the middle wall: 7 ft to each eave wall.
    CHECK(rafters.wall(box.south)->topLoads().peakPlf[LoadCase::Live] == Approx(70));
    CHECK(rafters.wall(middle)->topLoads().peakPlf[LoadCase::Live] == Approx(140));

    box.doc.roofs[0].framing = RoofFraming::Trusses;
    box.doc.roofs[0].structuralRidge = true; // not possible with trusses, so ignored
    const auto trusses = runTakedown(box.doc);
    CHECK(trusses.wall(box.south)->topLoads().peakPlf[LoadCase::Live] == Approx(140));
    CHECK(trusses.wall(middle)->lineLoads.empty());
    CHECK(trusses.ridgeBeam(box.doc.roofs[0].id) == nullptr);
    CHECK(trusses.wall(box.south)->topLoads().peakPlf[LoadCase::RoofLive] == Approx(20 * 15.5));
}

TEST_CASE("a custom roof snow load replaces the calculated one") {
    Box box;
    Roof& roof = box.addRoof(RoofStyle::Gable);
    roof.customSnow = true;
    roof.snowPsf = 50;
    const auto south = runTakedown(box.doc).wall(box.south)->topLoads();
    CHECK(south.peakPlf[LoadCase::Snow] == Approx(50 * 15.5));
}

TEST_CASE("first floor walls keep roof, floor and wall loads apart") {
    Box box;
    // A second floor wall on the south wall, a roof on Floor 2 and a floor between.
    const ObjectId upper = addWall(box.doc, box.l2, {0, 0}, {480, 0});
    addWall(box.doc, box.l2, {480, 336}, {0, 336});
    Roof r;
    r.id = box.doc.newId();
    r.levelId = box.l2;
    r.footprint = {{0, 0}, {480, 336}};
    r.hasCeiling = false;
    box.doc.roofs.push_back(r);
    FloorPlatform floor;
    floor.id = box.doc.newId();
    floor.levelId = box.l2;
    floor.area = {{0, 0}, {480, 336}};
    floor.joistAxis = Axis::Y;
    box.doc.platforms.push_back(floor);

    const auto result = runTakedown(box.doc);
    const SupportLoads* south = result.wall(box.south);
    const auto groups = summarizeByOrigin(south->lineLoads);
    REQUIRE(groups.size() == 3);
    CHECK(groups[0].origin == LoadOrigin::Roof);
    CHECK(groups[0].summary.peakPlf[LoadCase::Dead] == Approx(kDeadHoriz * 15.5).epsilon(1e-6));
    CHECK(groups[1].origin == LoadOrigin::Floor);
    CHECK(groups[1].summary.peakPlf[LoadCase::Live] == Approx(40 * 14));
    CHECK(groups[2].origin == LoadOrigin::Wall);
    const double wallPlf = 10.0 * 97.125 / 12.0;
    CHECK(groups[2].summary.peakPlf[LoadCase::Dead] == Approx(wallPlf));
    CHECK(result.wall(upper)->selfWeight.origin == LoadOrigin::Wall);

    // The bottom of the wall adds its own weight to everything on top.
    CHECK(south->bottomLoads().peakPlf[LoadCase::Dead] ==
          Approx(south->topLoads().peakPlf[LoadCase::Dead] + wallPlf).epsilon(1e-6));
}

TEST_CASE("rafters are checked over their longest span at their spacing") {
    Document d = Document::createDefault();
    RoofPlane r;
    r.levelId = d.levels[0].id;
    r.pitch = 6;
    r.a = {0, 0};
    r.b = {480, 0};
    r.outline = RoofPlane::rectangle(r.a, r.b, 168, 18);
    r.rafterMemberId = "dfl-no2-1-2x6";
    auto small = structural::checkRafters(d, r);
    REQUIRE(small.checked);
    CHECK(small.span == Approx(168));
    CHECK_FALSE(small.result.passes);
    r.rafterMemberId = "dfl-no2-1-2x12";
    CHECK(structural::checkRafters(d, r).result.passes);
    // Wider spacing carries more roof.
    r.rafterSpacing = 24;
    CHECK(structural::checkRafters(d, r).plf == Approx(small.plf * 1.5));
    r.framing = RoofFraming::Trusses;
    CHECK_FALSE(structural::checkRafters(d, r).checked);
    CHECK(structural::rafterChoices().size() >= 20);
}

TEST_CASE("a gambrel's ceiling is counted once, under both of its planes") {
    Document d = Document::createDefault();
    d.wallTypes.clear();
    const ObjectId level = d.levels[0].id;
    const Vec2 pts[] = {{0, 0}, {480, 0}, {480, 336}, {0, 336}};
    std::vector<ObjectId> ids;
    for (int i = 0; i < 4; ++i) {
        Wall w;
        w.id = d.newId();
        w.levelId = level;
        w.a = pts[i];
        w.b = pts[(i + 1) % 4];
        d.walls.push_back(w);
        ids.push_back(w.id);
    }
    for (int i : {0, 2}) {
        d.wall(ids[i])->roof.pitch = 18;
        d.wall(ids[i])->roof.upperPitch = 5;
        d.wall(ids[i])->roof.upperStart = 84;
    }
    for (int i : {1, 3}) d.wall(ids[i])->roof.kind = WallRoof::FullGable;
    REQUIRE(buildRoof(d, level) == 4);
    double ceiling = 0;
    for (const auto& s : runTakedown(d).supports)
        for (const auto& l : s.lineLoads)
            if (l.origin == LoadOrigin::Ceiling) ceiling += l.total()[LoadCase::Dead];
    CHECK(ceiling == Approx(10.0 * 480 * 336 / 144.0).epsilon(0.01));
}
