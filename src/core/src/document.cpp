#include "oa/core/document.h"

#include "oa/core/dimensions.h"
#include "oa/core/roof_builder.h"
#include "oa/core/rooms.h"
#include "oa/core/units.h"

namespace oa {

double WallType::thickness() const {
    switch (kind) {
    case WallKind::Exterior: return interiorFinish + studDepth + sheathing + siding;
    case WallKind::Interior: return interiorFinish * 2.0 + studDepth;
    case WallKind::RoomDivider: return 0.0;
    }
    return studDepth;
}

double WallType::selfWeightPsf() const {
    if (kind == WallKind::RoomDivider) return 0.0;
    // Studs at 35 pcf, plus about 15% for kings, jacks and blocking, plus
    // three plates spread over an 8 ft wall.
    const double studPlf = 1.5 * studDepth / 144.0 * 35.0;
    double psf = studPlf * (12.0 / studSpacing * 1.15 + 3.0 / 8.0);
    psf += 4.4 * interiorFinish * (kind == WallKind::Interior ? 2.0 : 1.0); // gypsum, 4.4 psf per inch
    if (kind == WallKind::Exterior) {
        psf += 3.2 * sheathing; // OSB or plywood
        psf += 3.0 * siding;    // a middle value for wood, vinyl and fiber cement
    }
    if (insulated) psf += 0.04 * studDepth; // glass fiber batts
    return psf;
}

std::vector<WallType> defaultWallTypes() {
    auto make = [](const char* name, WallKind kind, double stud, bool insulated, const char* insulation) {
        WallType t;
        t.name = name;
        t.kind = kind;
        t.studDepth = stud;
        t.insulated = insulated;
        t.insulation = insulation;
        if (kind != WallKind::Exterior) t.sheathing = t.siding = 0.0;
        return t;
    };
    return {
        make("6\" exterior wall (2x6)", WallKind::Exterior, 5.5, true, "R-21 batts"),
        make("4\" exterior wall (2x4)", WallKind::Exterior, 3.5, true, "R-13 batts"),
        make("4\" interior wall (2x4)", WallKind::Interior, 3.5, false, ""),
        make("6\" interior wall (2x6)", WallKind::Interior, 5.5, false, ""),
        make("Room divider (invisible)", WallKind::RoomDivider, 0.0, false, ""),
    };
}

double RoofPlane::width() const {
    if (outline.empty()) return 0.0;
    double lo = 1e30, hi = -1e30;
    for (Vec2 p : outline) {
        lo = std::min(lo, alongFrom(p));
        hi = std::max(hi, alongFrom(p));
    }
    return hi - lo;
}

double RoofPlane::runLength() const {
    double hi = 0.0;
    for (Vec2 p : outline) hi = std::max(hi, upFrom(p));
    return hi;
}

double RoofPlane::eaveOverhang() const {
    double lo = 0.0;
    for (Vec2 p : outline) lo = std::min(lo, upFrom(p));
    return -lo;
}

Polygon RoofPlane::rectangle(Vec2 a, Vec2 b, double run, double overhang) {
    const Vec2 up = (b - a).normalized().perp();
    return {a - up * overhang, b - up * overhang, b + up * run, a + up * run};
}

Document Document::createDefault() {
    Document d;
    d.wallTypes = defaultWallTypes();
    for (auto& t : d.wallTypes) t.id = d.newId();
    Level l1;
    l1.id = d.newId();
    l1.name = "Floor 1";
    l1.elevation = 0.0;
    Level l2;
    l2.id = d.newId();
    l2.name = "Floor 2";
    l2.elevation = l1.elevation + l1.wallHeight + l1.floorThickness;
    d.levels = {l1, l2};
    return d;
}

std::vector<const Level*> Document::levelsByElevation() const {
    std::vector<const Level*> out;
    for (const auto& l : levels) out.push_back(&l);
    std::stable_sort(out.begin(), out.end(),
                     [](const Level* a, const Level* b) { return a->elevation < b->elevation; });
    return out;
}

const Level* Document::levelBelow(ObjectId levelId) const {
    auto sorted = levelsByElevation();
    for (size_t i = 1; i < sorted.size(); ++i)
        if (sorted[i]->id == levelId) return sorted[i - 1];
    return nullptr;
}

const Level* Document::levelAbove(ObjectId levelId) const {
    auto sorted = levelsByElevation();
    for (size_t i = 0; i + 1 < sorted.size(); ++i)
        if (sorted[i]->id == levelId) return sorted[i + 1];
    return nullptr;
}

double Document::wallHeight(const Wall& w) const {
    if (w.height > 0.0) return w.height;
    const Level* l = level(w.levelId);
    return l ? l->wallHeight : 97.125;
}

RoofSettings* Document::roofSettings(ObjectId id) {
    if (Roof* r = roof(id)) return r;
    return roofPlane(id);
}

const RoofSettings* Document::roofSettings(ObjectId id) const {
    if (const Roof* r = roof(id)) return r;
    return roofPlane(id);
}

const WallType* Document::firstWallType(WallKind kind) const {
    for (const auto& t : wallTypes)
        if (t.kind == kind) return &t;
    return nullptr;
}

double Document::postHeight(const Post& p) const {
    if (p.height > 0.0) return p.height;
    const Level* l = level(p.levelId);
    return l ? l->wallHeight : 97.125;
}

ObjectId Document::levelOf(ObjectId id) const {
    if (const Wall* w = wall(id)) return w->levelId;
    if (const Opening* o = opening(id)) {
        const Wall* w = wall(o->wallId);
        return w ? w->levelId : kNoId;
    }
    if (const Beam* b = beam(id)) return b->levelId;
    if (const Roof* r = roof(id)) return r->levelId;
    if (const RoofPlane* p = roofPlane(id)) return p->levelId;
    if (const Post* p = post(id)) return p->levelId;
    if (const Dimension* d = dimension(id)) return d->levelId;
    if (elevationPoint(id)) {
        const Level* g = groundLevel();
        return g ? g->id : kNoId;
    }
    if (const FloorPlatform* f = findIn(platforms, id)) return f->levelId;
    if (const Room* r = findIn(rooms, id)) return r->levelId;
    if (level(id)) return id;
    return kNoId;
}

const Level* Document::groundLevel() const {
    const auto sorted = levelsByElevation();
    return sorted.empty() ? nullptr : sorted.front();
}

double Document::roomLivePsf(const Room& r) const {
    return r.floor.livePsf > 0.0 ? r.floor.livePsf : defaultLiveLoadPsf(r.type);
}

bool Document::contains(ObjectId id) const { return id != kNoId && levelOf(id) != kNoId; }

std::vector<const Opening*> Document::openingsIn(ObjectId wallId) const {
    std::vector<const Opening*> out;
    for (const auto& o : openings)
        if (o.wallId == wallId) out.push_back(&o);
    std::sort(out.begin(), out.end(),
              [](const Opening* a, const Opening* b) { return a->offset < b->offset; });
    return out;
}

bool Document::remove(ObjectId id) {
    auto eraseId = [id](auto& v) {
        auto n = v.size();
        std::erase_if(v, [id](const auto& o) { return o.id == id; });
        return v.size() != n;
    };

    // Walls of a removed type keep their current sizes.
    auto eraseWallType = [&](ObjectId typeId) {
        if (!eraseId(wallTypes)) return false;
        for (auto& w : walls)
            if (w.wallTypeId == typeId) w.wallTypeId = kNoId;
        return true;
    };
    if (eraseId(walls)) {
        std::erase_if(openings, [id](const Opening& o) { return o.wallId == id; });
        return true;
    }
    if (eraseId(levels)) {
        std::vector<ObjectId> gone;
        for (const auto& w : walls)
            if (w.levelId == id) gone.push_back(w.id);
        for (ObjectId w : gone) remove(w);
        auto onLevel = [id](const auto& o) { return o.levelId == id; };
        std::erase_if(rooms, onLevel);
        std::erase_if(platforms, onLevel);
        std::erase_if(roofs, onLevel);
        std::erase_if(roofPlanes, onLevel);
        std::erase_if(beams, onLevel);
        std::erase_if(posts, onLevel);
        std::erase_if(dimensions, onLevel);
        return true;
    }
    return eraseId(openings) || eraseId(rooms) || eraseId(platforms) || eraseId(roofs) ||
           eraseId(roofPlanes) || eraseId(beams) || eraseId(posts) || eraseId(dimensions) ||
           eraseId(terrain.points) || eraseWallType(id);
}

void Document::refresh() {
    upgrade();
    if (roofDefaults.autoRebuild) rebuildAutomaticRoofs(*this);
    updateRooms(*this);
    if (autoRefreshDimensions) refreshAutomaticDimensions(*this);
}

void Document::upgrade() {
    if (wallTypes.empty()) {
        wallTypes = defaultWallTypes();
        for (auto& t : wallTypes) t.id = newId();
    }
    for (auto& w : walls) {
        const WallType* t = wallType(w.wallTypeId);
        if (!t) {
            w.wallTypeId = kNoId;
            continue;
        }
        w.thickness = t->thickness();
        w.selfWeightPsf = t->selfWeightPsf();
        w.roomDivider = t->isDivider();
        if (w.roomDivider) w.bearing = false;
    }
    // Dividers carry nothing and have no openings.
    std::erase_if(openings, [this](const Opening& o) {
        const Wall* w = wall(o.wallId);
        return w && w->roomDivider;
    });
    for (auto& p : roofPlanes) {
        if (p.outline.size() < 3) p.outline = RoofPlane::rectangle(p.a, p.b, p.run > 0.0 ? p.run : 120.0, p.overhang);
        p.run = 0.0;
    }
}

double plateHeight(const Document& doc, ObjectId levelId) {
    const Level* l = doc.level(levelId);
    return l ? l->elevation + l->wallHeight : 97.125;
}

double roofPlaneHeight(const Document& doc, const RoofPlane& plane, Vec2 p) {
    return plateHeight(doc, plane.levelId) + plane.riseAt(p);
}

Polygon joinRoofPlanes(const Document& doc, const RoofPlane& plane, int edge, const RoofPlane& other) {
    // Each surface is z = g . p + c; they meet where (g1 - g2) . p = c2 - c1.
    auto coefficients = [&](const RoofPlane& r, Vec2& g, double& c) {
        const double slope = r.pitch / 12.0;
        g = r.upslope() * slope;
        c = plateHeight(doc, r.levelId) - dot(r.a, g);
    };
    Vec2 g1, g2;
    double c1, c2;
    coefficients(plane, g1, c1);
    coefficients(other, g2, c2);
    const Vec2 n = g1 - g2;
    if (n.lengthSq() < 1e-9) return {};
    const Vec2 onLine = n * ((c2 - c1) / n.lengthSq());
    return replaceEdge(plane.outline, edge, onLine, n.perp());
}

const char* toString(RoomType t) {
    switch (t) {
    case RoomType::Living: return "Living";
    case RoomType::Bedroom: return "Bedroom";
    case RoomType::Kitchen: return "Kitchen";
    case RoomType::Bath: return "Bath";
    case RoomType::Garage: return "Garage";
    case RoomType::Deck: return "Deck";
    case RoomType::Balcony: return "Balcony";
    case RoomType::Other: return "Other";
    }
    return "Other";
}

double defaultLiveLoadPsf(RoomType t) {
    switch (t) {
    case RoomType::Bedroom: return 30.0;  // sleeping rooms
    case RoomType::Deck: return 40.0;
    case RoomType::Balcony: return 60.0;  // exterior balconies
    case RoomType::Garage: return 50.0;   // passenger vehicle garages
    default: return 40.0;                 // rooms other than sleeping rooms
    }
}

} // namespace oa
