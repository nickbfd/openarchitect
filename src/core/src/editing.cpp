#include "oa/core/editing.h"

#include <algorithm>
#include <vector>

namespace oa {
namespace {
constexpr double kJoined = 0.5;
}

void setWallEnds(Document& doc, ObjectId id, Vec2 a, Vec2 b) {
    Wall* w = doc.wall(id);
    if (!w) return;
    const Vec2 oldA = w->a, oldB = w->b;
    // Doors and windows keep their place on the plan while their walls
    // stretch or shrink: note where each one is before anything moves.
    std::vector<std::pair<Opening*, Vec2>> placed;
    for (auto& o : doc.openings)
        if (const Wall* host = doc.wall(o.wallId); host && host->length() > 1e-9)
            placed.emplace_back(&o, host->a + (host->b - host->a).normalized() * o.offset);
    for (auto& v : doc.walls) {
        if (v.id == id || v.levelId != w->levelId) continue;
        for (Vec2* end : {&v.a, &v.b}) {
            if (distance(*end, oldA) <= kJoined) *end = a;
            else if (distance(*end, oldB) <= kJoined) *end = b;
        }
    }
    w->a = a;
    w->b = b;
    for (auto& [o, at] : placed) {
        const Wall* host = doc.wall(o->wallId);
        const double length = host->length();
        if (length < 1e-9) continue;
        const double along = dot(at - host->a, (host->b - host->a).normalized());
        o->offset = std::clamp(along, std::min(o->width / 2, length / 2), std::max(length - o->width / 2, length / 2));
    }
}

void slideWall(Document& doc, ObjectId id, double distance) {
    const Wall* w = doc.wall(id);
    if (!w || w->length() < 1e-9) return;
    const Vec2 n = (w->b - w->a).normalized().perp() * distance;
    setWallEnds(doc, id, w->a + n, w->b + n);
}

void setWallLength(Document& doc, ObjectId id, double length, int anchor) {
    const Wall* w = doc.wall(id);
    if (!w || w->length() < 1e-9 || length <= 0) return;
    const Vec2 dir = (w->b - w->a).normalized();
    Vec2 a = w->a, b = w->b;
    if (anchor == 0) {
        b = a + dir * length;
    } else if (anchor == 1) {
        a = b - dir * length;
    } else {
        const Vec2 mid = (a + b) * 0.5;
        a = mid - dir * (length / 2.0);
        b = mid + dir * (length / 2.0);
    }
    setWallEnds(doc, id, a, b);
}

ObjectId buildNewFloor(Document& doc) {
    auto levels = doc.levelsByElevation();
    auto hasWalls = [&](ObjectId id) {
        return std::any_of(doc.walls.begin(), doc.walls.end(), [&](const Wall& w) { return w.levelId == id; });
    };
    if (levels.empty()) {
        Level l;
        l.id = doc.newId();
        l.name = "Floor 1";
        doc.levels.push_back(l);
        return l.id;
    }
    // The source is the highest level with walls; the target is the level
    // above it, which must be empty.
    size_t src = levels.size() - 1;
    while (src > 0 && !hasWalls(levels[src]->id)) --src;
    const Level from = *levels[src];
    ObjectId target = kNoId;
    if (src + 1 < levels.size() && !hasWalls(levels[src + 1]->id)) {
        target = levels[src + 1]->id;
    } else {
        const Level top = *levels.back();
        Level l;
        l.id = doc.newId();
        l.elevation = top.elevation + top.wallHeight + top.floorThickness;
        l.wallHeight = top.wallHeight;
        l.floorThickness = top.floorThickness;
        l.name = "Floor " + std::to_string(levels.size() + 1);
        doc.levels.push_back(l);
        target = l.id;
    }
    std::vector<Wall> copies;
    for (const auto& w : doc.walls) {
        if (w.levelId != from.id || w.roomDivider) continue;
        const WallType* t = doc.wallType(w.wallTypeId);
        const bool exterior = t ? t->kind == WallKind::Exterior : w.bearing;
        if (!exterior) continue;
        Wall c = w;
        c.levelId = target;
        c.height = 0.0;
        copies.push_back(c);
    }
    for (Wall& c : copies) {
        c.id = doc.newId();
        doc.walls.push_back(c);
    }
    return target;
}

} // namespace oa
