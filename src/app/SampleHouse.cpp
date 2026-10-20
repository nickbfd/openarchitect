#include "SampleHouse.h"

#include "oa/core/dimensions.h"
#include "oa/core/units.h"

using namespace oa;

namespace {

// Wall types from Document::createDefault, by name.
ObjectId typeNamed(const Document& d, const char* name) {
    for (const auto& t : d.wallTypes)
        if (t.name == name) return t.id;
    return kNoId;
}

ObjectId wall(Document& d, ObjectId level, Vec2 a, Vec2 b, ObjectId type, bool bearing = true) {
    Wall w;
    w.id = d.newId();
    w.levelId = level;
    w.a = a;
    w.b = b;
    w.wallTypeId = type;
    w.bearing = bearing;
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
    const ObjectId exterior = typeNamed(d, "6\" exterior wall (2x6)");
    const ObjectId interior = typeNamed(d, "4\" interior wall (2x4)");
    const ObjectId divider = typeNamed(d, "Room divider (invisible)");

    // Floor 1
    const ObjectId south1 = wall(d, l1, {0, 0}, {W, 0}, exterior);
    const ObjectId east1 = wall(d, l1, {W, 0}, {W, D}, exterior);
    const ObjectId north1 = wall(d, l1, {W, D}, {0, D}, exterior);
    const ObjectId west1 = wall(d, l1, {0, D}, {0, 0}, exterior);
    wall(d, l1, {0, mid}, {feet(24), mid}, interior);
    wall(d, l1, {feet(16), mid}, {feet(16), D}, interior, false);
    // The kitchen and dining room open onto the living room.
    wall(d, l1, {feet(24), 0}, {feet(24), D}, divider, false);

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

    // Front porch: a single roof plane from a beam on two posts up to the
    // south wall, with a foot of overhang past the beam and at each end.
    const double porch = feet(6);
    Beam porchBeam;
    porchBeam.id = d.newId();
    porchBeam.levelId = l1;
    porchBeam.a = {0, -porch};
    porchBeam.b = {feet(16), -porch};
    porchBeam.memberId = "dfl-no2-1-4x10";
    d.beams.push_back(porchBeam);
    for (Vec2 at : {porchBeam.a, porchBeam.b}) {
        Post post;
        post.id = d.newId();
        post.levelId = l1;
        post.position = at;
        d.posts.push_back(post);
    }
    RoofPlane porchRoof;
    porchRoof.id = d.newId();
    porchRoof.levelId = l1;
    porchRoof.a = porchBeam.a;
    porchRoof.b = porchBeam.b;
    porchRoof.pitch = 3.0;
    porchRoof.outline = RoofPlane::rectangle(porchBeam.a - Vec2{12, 0}, porchBeam.b + Vec2{12, 0}, porch, 12.0);
    d.roofPlanes.push_back(porchRoof);
    // Room dividers close the porch off so it is a room of its own.
    wall(d, l1, {0, 0}, {0, -porch}, divider, false);
    wall(d, l1, {0, -porch}, {feet(16), -porch}, divider, false);
    wall(d, l1, {feet(16), -porch}, {feet(16), 0}, divider, false);
    room(d, l1, "Porch", RoomType::Deck, {{0, -porch}, {feet(16), 0}});
    // The porch floor is a concrete patio slab, stepped down from the house.
    d.rooms.back().floor.kind = FloorSystemKind::Slab;
    d.rooms.back().floor.drop = 4.0;

    // Floor 2
    const ObjectId south2 = wall(d, l2, {0, 0}, {W, 0}, exterior);
    const ObjectId east2 = wall(d, l2, {W, 0}, {W, D}, exterior);
    const ObjectId north2 = wall(d, l2, {W, D}, {0, D}, exterior);
    wall(d, l2, {0, D}, {0, 0}, exterior);
    wall(d, l2, {feet(20), 0}, {feet(20), D}, interior, false);

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
    // The lot falls from north to south, a little below the first floor at the house.
    const struct { double x, y, z; } grade[] = {
        {-feet(15), D + feet(15), -6},  {W / 2, D + feet(15), -4},     {W + feet(15), D + feet(15), -8},
        {-feet(15), D / 2, -8},         {W + feet(15), D / 2, -10},    {-feet(15), -feet(20), -30},
        {W / 2, -feet(20), -36},        {W + feet(15), -feet(20), -40}, {feet(8), -feet(10), -10},
    };
    for (const auto& g : grade) d.terrain.points.push_back({d.newId(), {g.x, g.y}, g.z});

    // Walls take their types' sizes, and the rooms above are matched to the walls.
    d.refresh();
    for (const ObjectId l : {l1, l2})
        for (Dimension dim : exteriorDimensions(d, l)) {
            dim.id = d.newId();
            d.dimensions.push_back(dim);
        }
    return d;
}
