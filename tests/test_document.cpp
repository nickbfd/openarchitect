#include "oa/core/document.h"
#include "oa/core/serialization.h"
#include "oa/core/undo.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;
using namespace oa;

namespace {
Document sample() {
    Document d = Document::createDefault();
    Wall w;
    w.id = d.newId();
    w.levelId = d.levels[0].id;
    w.a = {0, 0};
    w.b = {240, 0};
    d.walls.push_back(w);
    Opening o;
    o.id = d.newId();
    o.wallId = w.id;
    o.offset = 120;
    o.kind = OpeningKind::Door;
    d.openings.push_back(o);
    Roof r;
    r.id = d.newId();
    r.levelId = d.levels[1].id;
    r.footprint = {{0, 0}, {240, 336}};
    r.style = RoofStyle::Hip;
    r.framing = RoofFraming::Trusses;
    r.customSnow = true;
    r.snowPsf = 42.0;
    d.roofs.push_back(r);
    RoofPlane p;
    p.id = d.newId();
    p.levelId = d.levels[0].id;
    p.a = {240, 0};
    p.b = {0, 0};
    p.outline = RoofPlane::rectangle(p.a, p.b, 96, 12);
    d.roofPlanes.push_back(p);
    return d;
}
ObjectId planeId(const Document& d) { return d.roofPlanes.front().id; }
} // namespace

TEST_CASE("roof planes rise to the left of their eave line") {
    RoofPlane p;
    p.a = {0, 0};
    p.b = {240, 0};
    p.outline = RoofPlane::rectangle(p.a, p.b, 120, 12);
    CHECK(p.upslope() == Vec2{0, 1});
    CHECK(p.outline[0] == Vec2{0, -12});
    CHECK(p.outline[2] == Vec2{240, 120});
    CHECK(p.width() == Approx(240));
    CHECK(p.runLength() == Approx(120));
    CHECK(p.eaveOverhang() == Approx(12));
    CHECK(p.riseAt({50, 48}) == Approx(12));
}

TEST_CASE("roof planes from older files get an outline from their run") {
    Document d = sample();
    d.roofPlanes[0].outline.clear();
    d.roofPlanes[0].run = 96;
    d.roofPlanes[0].overhang = 12;
    d.upgrade();
    const RoofPlane& p = d.roofPlanes[0];
    REQUIRE(p.outline.size() == 4);
    CHECK(p.runLength() == Approx(96));
    CHECK(p.eaveOverhang() == Approx(12));
    CHECK(p.width() == Approx(240));
}

TEST_CASE("joining a roof plane to another trims its edge to the hip line") {
    Document d = Document::createDefault();
    const ObjectId l1 = d.levels[0].id;
    // Two 6/12 planes meeting at the south-east corner of a box: a hip.
    RoofPlane south;
    south.id = d.newId();
    south.levelId = l1;
    south.pitch = 6;
    south.a = {0, 0};
    south.b = {240, 0};
    south.outline = RoofPlane::rectangle({-120, 0}, {360, 0}, 120, 0);
    RoofPlane east;
    east.id = d.newId();
    east.levelId = l1;
    east.pitch = 6;
    east.a = {240, 0};
    east.b = {240, 240};
    east.outline = RoofPlane::rectangle({240, -120}, {240, 360}, 120, 0);
    // South's right edge (b side, index 1) runs from (360,0) up to (360,120).
    const Polygon joined = joinRoofPlanes(d, south, 1, east);
    REQUIRE(joined.size() == 4);
    // Equal pitches meet on the 45 degree line from the corner.
    CHECK(joined[1].x == Approx(240));
    CHECK(joined[1].y == Approx(0).margin(1e-9));
    CHECK(joined[2].x == Approx(120));
    CHECK(joined[2].y == Approx(120));
}

TEST_CASE("walls take their thickness and weight from their wall type") {
    Document d = Document::createDefault();
    REQUIRE(d.wallTypes.size() >= 4);
    const WallType* six = d.firstWallType(WallKind::Exterior);
    REQUIRE(six != nullptr);
    Wall w;
    w.id = d.newId();
    w.levelId = d.levels[0].id;
    w.b = {120, 0};
    w.wallTypeId = six->id;
    w.thickness = 1;
    d.walls.push_back(w);
    d.refresh();
    CHECK(d.walls[0].thickness == Approx(six->thickness()));
    CHECK(six->thickness() == Approx(0.5 + 5.5 + 0.4375 + 0.75));
    const double heavy = d.walls[0].selfWeightPsf;
    CHECK(heavy > 8);
    CHECK(heavy < 16);

    // Wider spacing and no insulation make it lighter.
    d.wallType(six->id)->studSpacing = 24;
    d.wallType(six->id)->insulated = false;
    d.refresh();
    CHECK(d.walls[0].selfWeightPsf < heavy);
}

TEST_CASE("room dividers carry nothing and take no openings") {
    Document d = Document::createDefault();
    const WallType* divider = d.firstWallType(WallKind::RoomDivider);
    REQUIRE(divider != nullptr);
    Wall w;
    w.id = d.newId();
    w.levelId = d.levels[0].id;
    w.b = {120, 0};
    w.wallTypeId = divider->id;
    d.walls.push_back(w);
    Opening o;
    o.id = d.newId();
    o.wallId = w.id;
    d.openings.push_back(o);
    d.refresh();
    CHECK(d.walls[0].roomDivider);
    CHECK_FALSE(d.walls[0].bearing);
    CHECK(d.openings.empty());
}

TEST_CASE("plan files round-trip through JSON") {
    const Document d = sample();
    const Document back = documentFromJson(documentToJson(d));
    REQUIRE(back.walls.size() == 1);
    CHECK(back.walls[0].b == Vec2{240, 0});
    REQUIRE(back.openings.size() == 1);
    CHECK(back.openings[0].kind == OpeningKind::Door);
    REQUIRE(back.roofs.size() == 1);
    CHECK(back.roofs[0].style == RoofStyle::Hip);
    CHECK(back.roofs[0].footprint.max == Vec2{240, 336});
    CHECK(back.roofs[0].framing == RoofFraming::Trusses);
    CHECK(back.roofs[0].customSnow);
    CHECK(back.roofs[0].snowPsf == 42.0);
    REQUIRE(back.roofPlanes.size() == 1);
    CHECK(back.roofPlanes[0].a == Vec2{240, 0});
    CHECK(back.roofPlanes[0].outline.size() == 4);
    CHECK(back.roofPlanes[0].runLength() == Approx(96));
    CHECK(back.roofPlanes[0].pitch == 3.0);
    CHECK_FALSE(back.roofPlanes[0].hasCeiling);
    CHECK(back.levelOf(planeId(back)) == back.levels[0].id);
    CHECK(back.nextId == d.nextId);
    CHECK(back.levels.size() == 2);
}

TEST_CASE("plan files from a newer app or another format are refused") {
    CHECK_THROWS_AS(documentFromJson(R"({"format":"openarchitect-plan","schemaVersion":999,"document":{}})"),
                    FileError);
    CHECK_THROWS_AS(documentFromJson(R"({"format":"something-else","schemaVersion":1})"), FileError);
    CHECK_THROWS_AS(documentFromJson("not json"), FileError);
}

TEST_CASE("missing fields take their defaults and ids are never reused") {
    const Document d = documentFromJson(
        R"({"format":"openarchitect-plan","schemaVersion":1,"document":{"walls":[{"id":41,"a":[0,0],"b":[10,0]}]}})");
    REQUIRE(d.walls.size() == 1);
    CHECK(d.walls[0].thickness == 5.5);
    // Older files get the default wall types, numbered after what's there.
    REQUIRE_FALSE(d.wallTypes.empty());
    CHECK(d.wallTypes.front().id == 42);
    CHECK(d.nextId == 42 + d.wallTypes.size());
}

TEST_CASE("removing a wall removes its openings") {
    Document d = sample();
    CHECK(d.remove(d.walls[0].id));
    CHECK(d.walls.empty());
    CHECK(d.openings.empty());
}

TEST_CASE("undo and redo restore document snapshots") {
    UndoStack stack;
    Document d = Document::createDefault();
    stack.setClean();

    Document before = d;
    d.name = "House";
    stack.push("Rename", before, d);
    CHECK_FALSE(stack.isClean());
    CHECK(stack.undoText() == "Rename");

    d = stack.undo();
    CHECK(d.name == "Untitled");
    CHECK(stack.isClean());
    d = stack.redo();
    CHECK(d.name == "House");
    CHECK_FALSE(stack.canRedo());
}
