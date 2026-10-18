#include "SampleHouse.h"

#include "oa/core/units.h"

using namespace oa;

namespace {

ObjectId wall(Document& d, ObjectId level, Vec2 a, Vec2 b, bool bearing = true, double thickness = 5.5) {
    Wall w;
    w.id = d.newId();
    w.levelId = level;
    w.a = a;
    w.b = b;
    w.bearing = bearing;
    w.thickness = thickness;
    if (!bearing) w.selfWeightPsf = 8.0;
    d.walls.push_back(w);
    return w.id;
}

void opening(Document& d, ObjectId wallId, OpeningKind kind, double offset, double width, double height,
             double sill) {
    Opening o;
    o.id = d.newId();
    o.wallId = wallId;
    o.kind = kind;
    o.offset = offset;
    o.width = width;
    o.height = height;
    o.sillHeight = sill;
    d.openings.push_back(o);
}

void room(Document& d, ObjectId level, const char* name, RoomType type, Rect r) {
    Room rm;
    rm.id = d.newId();
    rm.levelId = level;
    rm.name = name;
    rm.type = type;
    rm.outline = r.corners();
    d.rooms.push_back(rm);
}

} // namespace

Document makeSampleHouse() {
    Document d = Document::createDefault();
    d.name = "Sample house";
    const ObjectId l1 = d.levels[0].id;
    const ObjectId l2 = d.levels[1].id;
    const double W = feet(40), D = feet(28), mid = D / 2;

    // Floor 1
    const ObjectId south1 = wall(d, l1, {0, 0}, {W, 0});
    const ObjectId east1 = wall(d, l1, {W, 0}, {W, D});
    const ObjectId north1 = wall(d, l1, {W, D}, {0, D});
    const ObjectId west1 = wall(d, l1, {0, D}, {0, 0});
    wall(d, l1, {0, mid}, {feet(24), mid}, true, 4.5);
    wall(d, l1, {feet(16), mid}, {feet(16), D}, false, 4.5);

    opening(d, south1, OpeningKind::Door, feet(8), 36, 80, 0);
    opening(d, south1, OpeningKind::Window, feet(20), 72, 48, 36);
    opening(d, south1, OpeningKind::Window, feet(32), 36, 48, 36);
    opening(d, east1, OpeningKind::Window, feet(7), 36, 48, 36);
    opening(d, north1, OpeningKind::Window, feet(12), 48, 48, 36);
    opening(d, north1, OpeningKind::Door, feet(30), 72, 80, 0);
    opening(d, west1, OpeningKind::Window, feet(7), 36, 48, 36);

    // The open living area gets a beam instead of a wall.
    Beam beam;
    beam.id = d.newId();
    beam.levelId = l1;
    beam.a = {feet(24), mid};
    beam.b = {W, mid};
    d.beams.push_back(beam);

    room(d, l1, "Living", RoomType::Living, {{feet(24), 0}, {W, D}});
    room(d, l1, "Kitchen", RoomType::Kitchen, {{0, 0}, {feet(24), mid}});
    room(d, l1, "Office", RoomType::Bedroom, {{0, mid}, {feet(16), D}});
    room(d, l1, "Dining", RoomType::Living, {{feet(16), mid}, {feet(24), D}});

    // Floor 2
    const ObjectId south2 = wall(d, l2, {0, 0}, {W, 0});
    const ObjectId east2 = wall(d, l2, {W, 0}, {W, D});
    const ObjectId north2 = wall(d, l2, {W, D}, {0, D});
    wall(d, l2, {0, D}, {0, 0});
    wall(d, l2, {feet(20), 0}, {feet(20), D}, false, 4.5);

    opening(d, south2, OpeningKind::Window, feet(8), 48, 48, 36);
    opening(d, south2, OpeningKind::Window, feet(32), 48, 48, 36);
    opening(d, east2, OpeningKind::Window, feet(14), 36, 48, 36);
    opening(d, north2, OpeningKind::Window, feet(10), 36, 48, 36);
    opening(d, north2, OpeningKind::Window, feet(30), 36, 48, 36);

    room(d, l2, "Bedroom 1", RoomType::Bedroom, {{0, 0}, {feet(20), D}});
    room(d, l2, "Bedroom 2", RoomType::Bedroom, {{feet(20), 0}, {W, D}});

    FloorPlatform floor;
    floor.id = d.newId();
    floor.levelId = l2;
    floor.area = {{0, 0}, {W, D}};
    floor.joistAxis = Axis::Y;
    floor.livePsf = 40.0;
    d.platforms.push_back(floor);

    Roof roof;
    roof.id = d.newId();
    roof.levelId = l2;
    roof.style = RoofStyle::Gable;
    roof.footprint = {{0, 0}, {W, D}};
    roof.ridgeAxis = Axis::X;
    roof.pitch = 6.0;
    roof.overhang = 18.0;
    d.roofs.push_back(roof);
    return d;
}
