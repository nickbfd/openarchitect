#include "oa/core/document.h"

#include "oa/core/units.h"

namespace oa {

Document Document::createDefault() {
    Document d;
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

ObjectId Document::levelOf(ObjectId id) const {
    if (const Wall* w = wall(id)) return w->levelId;
    if (const Opening* o = opening(id)) {
        const Wall* w = wall(o->wallId);
        return w ? w->levelId : kNoId;
    }
    if (const Beam* b = beam(id)) return b->levelId;
    if (const Roof* r = roof(id)) return r->levelId;
    if (const RoofPlane* p = roofPlane(id)) return p->levelId;
    if (const FloorPlatform* f = findIn(platforms, id)) return f->levelId;
    if (const Room* r = findIn(rooms, id)) return r->levelId;
    if (level(id)) return id;
    return kNoId;
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
        return true;
    }
    return eraseId(openings) || eraseId(rooms) || eraseId(platforms) || eraseId(roofs) ||
           eraseId(roofPlanes) || eraseId(beams);
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
