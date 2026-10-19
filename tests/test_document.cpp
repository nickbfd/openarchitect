#include "oa/core/document.h"
#include "oa/core/serialization.h"
#include "oa/core/undo.h"

#include <catch2/catch_test_macros.hpp>

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
    p.run = 96;
    d.roofPlanes.push_back(p);
    return d;
}
ObjectId planeId(const Document& d) { return d.roofPlanes.front().id; }
} // namespace

TEST_CASE("roof planes rise to the left of their eave line") {
    RoofPlane p;
    p.a = {0, 0};
    p.b = {240, 0};
    p.run = 120;
    p.overhang = 12;
    CHECK(p.upslope() == Vec2{0, 1});
    CHECK(p.highLine().a == Vec2{0, 120});
    const Polygon withOverhang = p.outline(true);
    CHECK(withOverhang[0] == Vec2{0, -12});
    CHECK(withOverhang[2] == Vec2{240, 120});
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
    CHECK(back.roofPlanes[0].run == 96.0);
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
    CHECK(d.nextId == 42);
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
