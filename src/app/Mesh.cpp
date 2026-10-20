#include "Mesh.h"

#include "oa/core/terrain.h"
#include "oa/structural/members.h"
#include "oa/structural/posts.h"

#include <algorithm>
#include <cmath>

using namespace oa;

namespace {

struct Color {
    float r, g, b;
};
constexpr Color kWall{0.92f, 0.89f, 0.82f};
constexpr Color kFloor{0.78f, 0.66f, 0.50f};
constexpr Color kRoof{0.40f, 0.33f, 0.30f};
constexpr Color kBeam{0.85f, 0.62f, 0.35f};
constexpr Color kGable{0.88f, 0.85f, 0.78f};
constexpr Color kGlass{0.55f, 0.72f, 0.85f};
constexpr Color kDoor{0.55f, 0.38f, 0.24f};
constexpr Color kSelected{0.25f, 0.55f, 0.95f};
constexpr Color kPost{0.80f, 0.58f, 0.33f};
constexpr Color kLumber{0.87f, 0.74f, 0.52f};
constexpr Color kHeader{0.80f, 0.55f, 0.30f};
constexpr Color kInsulation{0.96f, 0.68f, 0.72f};
constexpr Color kConcrete{0.74f, 0.74f, 0.72f};
constexpr Color kGround{0.55f, 0.68f, 0.42f};

// Depth of the header over an opening: its assigned member, or a 2x10.
double headerDepth(const Opening& o) {
    if (const auto* m = structural::findMember(o.headerMemberId)) return m->d;
    return 9.25;
}

class Builder {
public:
    Mesh mesh;
    ObjectId owner = kNoId;    // object the next triangles belong to
    ObjectId selected = kNoId;

    void triangle(Vec3 a, Vec3 b, Vec3 c, Color col) {
        if (owner != kNoId && owner == selected)
            col = {col.r * 0.35f + kSelected.r * 0.65f, col.g * 0.35f + kSelected.g * 0.65f,
                   col.b * 0.35f + kSelected.b * 0.65f};
        const Vec3 u{b.x - a.x, b.y - a.y, b.z - a.z};
        const Vec3 v{c.x - a.x, c.y - a.y, c.z - a.z};
        Vec3 n{u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x};
        const double len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
        if (len < 1e-12) return;
        n = {n.x / len, n.y / len, n.z / len};
        mesh.owners.push_back(owner);
        for (const Vec3& p : {a, b, c}) {
            mesh.vertices.push_back({{float(p.x), float(p.y), float(p.z)},
                                     {float(n.x), float(n.y), float(n.z)},
                                     {col.r, col.g, col.b}});
            mesh.min = {std::min(mesh.min.x, p.x), std::min(mesh.min.y, p.y), std::min(mesh.min.z, p.z)};
            mesh.max = {std::max(mesh.max.x, p.x), std::max(mesh.max.y, p.y), std::max(mesh.max.z, p.z)};
        }
    }

    void quad(Vec3 a, Vec3 b, Vec3 c, Vec3 d, Color col) {
        triangle(a, b, c, col);
        triangle(a, c, d, col);
    }

    // A box running from `start` along unit `dir` for `length`, `width` wide
    // centered on that line, between heights z0 and z1.
    void box(Vec2 start, Vec2 dir, double length, double width, double z0, double z1, Color col) {
        if (length <= 1e-6 || z1 - z0 <= 1e-6) return;
        const Vec2 side = dir.perp() * (width / 2.0);
        const Vec2 p0 = start - side, p1 = start + dir * length - side;
        const Vec2 p2 = start + dir * length + side, p3 = start + side;
        auto at = [](Vec2 p, double z) { return Vec3{p.x, p.y, z}; };
        quad(at(p0, z0), at(p1, z0), at(p1, z1), at(p0, z1), col); // right face
        quad(at(p2, z0), at(p3, z0), at(p3, z1), at(p2, z1), col); // left face
        quad(at(p1, z0), at(p2, z0), at(p2, z1), at(p1, z1), col); // end
        quad(at(p3, z0), at(p0, z0), at(p0, z1), at(p3, z1), col); // start
        quad(at(p0, z1), at(p1, z1), at(p2, z1), at(p3, z1), col); // top
        quad(at(p0, z0), at(p3, z0), at(p2, z0), at(p1, z0), col); // bottom
    }
};

void addWall(Builder& b, const Document& doc, const Wall& w) {
    const Level* level = doc.level(w.levelId);
    if (!level || w.length() < 1e-6 || w.roomDivider) return;
    const double z0 = level->elevation;
    const double h = doc.wallHeight(w);
    const Vec2 dir = (w.b - w.a).normalized();
    const double len = w.length();

    // Cut openings by splitting the wall into solid pieces, and fill each
    // with a thin pane or door slab that belongs to the opening.
    double cursor = 0.0;
    for (const Opening* o : doc.openingsIn(w.id)) {
        const double s0 = std::clamp(o->offset - o->width / 2.0, 0.0, len);
        const double s1 = std::clamp(o->offset + o->width / 2.0, 0.0, len);
        if (s1 <= cursor) continue;
        const double from = std::max(s0, cursor);
        b.owner = w.id;
        b.box(w.a + dir * cursor, dir, from - cursor, w.thickness, z0, z0 + h, kWall);
        const double sill = std::clamp(o->sillHeight, 0.0, h);
        const double head = std::clamp(o->sillHeight + o->height, 0.0, h);
        b.box(w.a + dir * from, dir, s1 - from, w.thickness, z0, z0 + sill, kWall);
        b.box(w.a + dir * from, dir, s1 - from, w.thickness, z0 + head, z0 + h, kWall);
        b.owner = o->id;
        const bool door = o->kind == OpeningKind::Door;
        b.box(w.a + dir * from, dir, s1 - from, door ? 1.75 : 0.75, z0 + sill, z0 + head, door ? kDoor : kGlass);
        cursor = s1;
    }
    b.owner = w.id;
    b.box(w.a + dir * cursor, dir, len - cursor, w.thickness, z0, z0 + h, kWall);
}

void addPlatform(Builder& b, const Document& doc, const FloorPlatform& p) {
    const Level* level = doc.level(p.levelId);
    if (!level) return;
    const Rect& r = p.area;
    // The floor framing fills the band between the walls below and this floor.
    const Level* below = doc.levelBelow(p.levelId);
    const double thickness = below ? below->floorThickness : 4.0; // otherwise a slab on grade
    b.box({r.min.x, r.center().y}, {1, 0}, r.width(), r.height(), level->elevation - thickness,
          level->elevation, kFloor);
}

// A slab room's concrete, from its surface down by the slab thickness.
// It sits a hair above the level's floor so the two don't flicker.
void addSlab(Builder& b, const Document& doc, const Room& r) {
    const Level* level = doc.level(r.levelId);
    if (!level || r.floor.kind != FloorSystemKind::Slab || r.outline.size() < 3) return;
    const double z1 = level->elevation - r.floor.drop + 0.1;
    const double z0 = z1 - r.floor.slabThickness;
    const Polygon& o = r.outline;
    const bool ccw = signedArea(o) > 0;
    for (const auto& t : triangulate(o)) {
        Vec3 p[3];
        for (int i = 0; i < 3; ++i) p[i] = {o[t[i]].x, o[t[i]].y, z1};
        b.triangle(p[0], p[1], p[2], kConcrete);
        b.triangle({p[0].x, p[0].y, z0}, {p[2].x, p[2].y, z0}, {p[1].x, p[1].y, z0}, kConcrete);
    }
    const int n = static_cast<int>(o.size());
    for (int i = 0; i < n; ++i) {
        Vec2 a = o[i], c = o[(i + 1) % n];
        if (!ccw) std::swap(a, c);
        b.quad({a.x, a.y, z0}, {c.x, c.y, z0}, {c.x, c.y, z1}, {a.x, a.y, z1}, kConcrete);
    }
}

// The site surface between the elevation points, measured from the lowest level's floor.
void addTerrain(Builder& b, const Document& doc) {
    const Level* ground = doc.groundLevel();
    const TerrainMesh t = terrainMesh(doc.terrain);
    const double base = ground ? ground->elevation : 0.0;
    for (const auto& tri : t.triangles) {
        Vec3 p[3];
        for (int i = 0; i < 3; ++i) p[i] = {t.points[tri[i]].x, t.points[tri[i]].y, base + t.elevations[tri[i]]};
        b.triangle(p[0], p[1], p[2], kGround);
    }
}

void addBeam(Builder& b, const Document& doc, const Beam& beam) {
    const Level* level = doc.level(beam.levelId);
    if (!level || beam.length() < 1e-6) return;
    double depth = 11.25, width = 3.5;
    if (const auto* m = structural::findMember(beam.memberId)) {
        depth = m->d;
        width = m->b;
    }
    const double top = level->elevation + level->wallHeight;
    b.box(beam.a, (beam.b - beam.a).normalized(), beam.length(), width, top - depth, top, kBeam);
}

// The roof surface sits on the rafters, this far above the wall plate.
constexpr double kHeel = 8.0;

void addRoofPlane(Builder& b, const Document& doc, const RoofPlane& r) {
    const Level* level = doc.level(r.levelId);
    if (!level || r.outline.size() < 3 || distance(r.a, r.b) < 1.0) return;
    const double slope = r.pitch / 12.0;
    const double wallTop = plateHeight(doc, r.levelId);
    auto surface = [&](Vec2 p) { return Vec3{p.x, p.y, wallTop + kHeel + r.riseAt(p)}; };

    for (const auto& t : triangulate(r.outline))
        b.triangle(surface(r.outline[t[0]]), surface(r.outline[t[1]]), surface(r.outline[t[2]]), kRoof);

    // Close the gap between the walls under the plane and its underside: the
    // band over the eave wall, the gable triangles at the rakes, and the
    // wall under a shed roof's high side.
    if (slope < 1e-6) return;
    const double lowest = -kHeel / slope; // uphill distance where the surface meets the wall top
    for (const auto& w : doc.walls) {
        if (w.levelId != r.levelId || w.roomDivider) continue;
        for (auto [t0, t1] : clipToPolygon(w.centerline(), r.outline)) {
            const Vec2 q0 = lerp(w.a, w.b, t0), q1 = lerp(w.a, w.b, t1);
            const double s0 = r.upFrom(q0), s1 = r.upFrom(q1);
            if (s0 < lowest && s1 < lowest) continue;
            Vec2 p0 = q0, p1 = q1;
            if (s0 < lowest) p0 = lerp(q0, q1, (lowest - s0) / (s1 - s0));
            if (s1 < lowest) p1 = lerp(q0, q1, (lowest - s0) / (s1 - s0));
            b.quad({p0.x, p0.y, wallTop}, {p1.x, p1.y, wallTop}, surface(p1), surface(p0), kGable);
        }
    }
}

void addPost(Builder& b, const Document& doc, const Post& post) {
    const Level* level = doc.level(post.levelId);
    if (!level) return;
    double size = 3.5;
    if (const auto* m = structural::findPost(post.memberId)) size = m->b;
    b.box(post.position - Vec2{size / 2.0, 0}, {1, 0}, size, size, level->elevation,
          level->elevation + doc.postHeight(post), kPost);
}

// Framing of a wall from its type: plates, studs on their spacing, and at
// each opening king and jack studs, a header, a sill and cripples.
void addWallFraming(Builder& b, const Document& doc, const Wall& w) {
    const Level* level = doc.level(w.levelId);
    const WallType* type = doc.wallType(w.wallTypeId);
    if (!level || !type || type->isDivider() || w.length() < 1e-6) return;
    const double z0 = level->elevation;
    const double h = doc.wallHeight(w);
    const Vec2 dir = (w.b - w.a).normalized();
    const double len = w.length();
    const double depth = type->studDepth;
    // The studs sit toward the outside face of exterior walls, centered otherwise.
    const double shift = type->kind == WallKind::Exterior
                             ? (w.thickness / 2.0 - type->siding - type->sheathing - depth / 2.0)
                             : 0.0;
    const Vec2 base = w.a + dir.perp() * -shift;
    auto stick = [&](double s0, double s1, double za, double zb, Color c, double width) {
        b.box(base + dir * s0, dir, s1 - s0, width, z0 + za, z0 + zb, c);
    };
    constexpr double t = 1.5;
    b.owner = w.id;
    stick(0, len, 0, t, kLumber, depth);           // bottom plate
    stick(0, len, h - 2 * t, h, kLumber, depth);   // double top plate

    struct Hole {
        double s0, s1, sill, head;
        const Opening* o;
    };
    std::vector<Hole> holes;
    for (const Opening* o : doc.openingsIn(w.id)) {
        const double s0 = std::clamp(o->offset - o->width / 2.0, 0.0, len);
        const double s1 = std::clamp(o->offset + o->width / 2.0, 0.0, len);
        holes.push_back({s0, s1, std::clamp(o->sillHeight, 0.0, h), std::clamp(o->topHeight(), 0.0, h - 2 * t), o});
    }
    auto inHole = [&](double s0, double s1) {
        for (const auto& hl : holes)
            if (s1 > hl.s0 - 2 * t - 1e-6 && s0 < hl.s1 + 2 * t + 1e-6) return &hl;
        return static_cast<const Hole*>(nullptr);
    };

    // Common studs and cripples on the layout, measured from the wall start.
    const double spacing = std::max(type->studSpacing, 8.0);
    std::vector<double> layout{0.0};
    for (double s = spacing - t / 2.0; s < len - t; s += spacing) layout.push_back(s);
    layout.push_back(len - t);
    for (double s : layout) {
        if (const Hole* hl = inHole(s, s + t)) {
            if (s < hl->s0 - 2 * t + 1e-6 || s + t > hl->s1 + 2 * t - 1e-6) continue; // king and jack go there
            const double headerTop = hl->head + headerDepth(*hl->o);
            if (headerTop < h - 2 * t - 0.5) stick(s, s + t, headerTop, h - 2 * t, kLumber, depth);
            if (hl->sill > 2 * t + 0.5) stick(s, s + t, t, hl->sill - t, kLumber, depth);
            continue;
        }
        stick(s, s + t, t, h - 2 * t, kLumber, depth);
    }
    for (const auto& hl : holes) {
        const double hd = headerDepth(*hl.o);
        const double top = std::min(hl.head + hd, h - 2 * t);
        stick(hl.s0 - 2 * t, hl.s0 - t, t, h - 2 * t, kLumber, depth);  // king
        stick(hl.s1 + t, hl.s1 + 2 * t, t, h - 2 * t, kLumber, depth);  // king
        stick(hl.s0 - t, hl.s0, t, hl.head, kLumber, depth);            // jack
        stick(hl.s1, hl.s1 + t, t, hl.head, kLumber, depth);            // jack
        b.owner = hl.o->id;
        stick(hl.s0 - t, hl.s1 + t, hl.head, top, kHeader, depth);
        if (hl.sill > 2 * t) stick(hl.s0, hl.s1, hl.sill - t, hl.sill, kLumber, depth);
        b.owner = w.id;
    }

    // Insulation fills the bays between plates, around the openings.
    if (!type->insulated) return;
    double cursor = 0.0;
    auto bay = [&](double s0, double s1, double za, double zb) {
        if (s1 - s0 > 0.5 && zb - za > 0.5) stick(s0, s1, za, zb, kInsulation, depth * 0.5);
    };
    for (const auto& hl : holes) {
        bay(cursor, hl.s0 - 2 * t, t, h - 2 * t);
        bay(hl.s0, hl.s1, hl.head + headerDepth(*hl.o), h - 2 * t);
        bay(hl.s0, hl.s1, t, hl.sill - t);
        cursor = std::max(cursor, hl.s1 + 2 * t);
    }
    bay(cursor, len, t, h - 2 * t);
}

void addRoof(Builder& b, const Document& doc, const Roof& roof) {
    const Level* level = doc.level(roof.levelId);
    if (!level) return;
    const Rect& fp = roof.footprint;
    if (fp.width() < 1.0 || fp.height() < 1.0) return;

    // Work in (u along the ridge, v across it), then map back to plan x, y.
    const bool ridgeX = roof.ridgeAxis == Axis::X;
    const double u0 = ridgeX ? fp.min.x : fp.min.y, u1 = ridgeX ? fp.max.x : fp.max.y;
    const double v0 = ridgeX ? fp.min.y : fp.min.x, v1 = ridgeX ? fp.max.y : fp.max.x;
    auto P = [ridgeX](double u, double v, double z) { return ridgeX ? Vec3{u, v, z} : Vec3{v, u, z}; };

    const double slope = roof.pitch / 12.0;
    const double o = roof.overhang;
    const double plate = level->elevation + level->wallHeight + kHeel;
    const double vc = (v0 + v1) / 2.0;
    const double run = (v1 - v0) / 2.0;
    const double ridgeZ = plate + run * slope;
    const double eaveZ = plate - o * slope;

    // Faces wind so they read correctly from outside; the shader lights both sides.
    if (roof.style == RoofStyle::Gable) {
        b.quad(P(u0 - o, v0 - o, eaveZ), P(u1 + o, v0 - o, eaveZ), P(u1 + o, vc, ridgeZ), P(u0 - o, vc, ridgeZ), kRoof);
        b.quad(P(u1 + o, v1 + o, eaveZ), P(u0 - o, v1 + o, eaveZ), P(u0 - o, vc, ridgeZ), P(u1 + o, vc, ridgeZ), kRoof);
        const double wallTop = plate - kHeel;
        b.quad(P(u0, v1, wallTop), P(u0, v0, wallTop), P(u0, v0, plate), P(u0, v1, plate), kGable);
        b.quad(P(u1, v0, wallTop), P(u1, v1, wallTop), P(u1, v1, plate), P(u1, v0, plate), kGable);
        b.triangle(P(u0, v1, plate), P(u0, v0, plate), P(u0, vc, ridgeZ), kGable);
        b.triangle(P(u1, v0, plate), P(u1, v1, plate), P(u1, vc, ridgeZ), kGable);
    } else {
        const double half = run + o;
        const double ru0 = std::min((u0 + u1) / 2.0, u0 - o + half);
        const double ru1 = std::max((u0 + u1) / 2.0, u1 + o - half);
        const double topZ = eaveZ + std::min(half, (u1 - u0) / 2.0 + o) * slope;
        b.quad(P(u0 - o, v0 - o, eaveZ), P(u1 + o, v0 - o, eaveZ), P(ru1, vc, topZ), P(ru0, vc, topZ), kRoof);
        b.quad(P(u1 + o, v1 + o, eaveZ), P(u0 - o, v1 + o, eaveZ), P(ru0, vc, topZ), P(ru1, vc, topZ), kRoof);
        b.triangle(P(u1 + o, v0 - o, eaveZ), P(u1 + o, v1 + o, eaveZ), P(ru1, vc, topZ), kRoof);
        b.triangle(P(u0 - o, v1 + o, eaveZ), P(u0 - o, v0 - o, eaveZ), P(ru0, vc, topZ), kRoof);
    }
}

} // namespace

Mesh buildMesh(const Document& doc, ObjectId selected, bool framing) {
    Builder b;
    b.selected = selected;
    for (const auto& p : doc.platforms) {
        b.owner = p.id;
        addPlatform(b, doc, p);
    }
    for (const auto& r : doc.rooms) {
        b.owner = r.id;
        addSlab(b, doc, r);
    }
    b.owner = kNoId;
    addTerrain(b, doc);
    for (const auto& w : doc.walls) {
        if (framing && doc.wallType(w.wallTypeId)) addWallFraming(b, doc, w);
        else addWall(b, doc, w);
    }
    for (const auto& post : doc.posts) {
        b.owner = post.id;
        addPost(b, doc, post);
    }
    for (const auto& beam : doc.beams) {
        b.owner = beam.id;
        addBeam(b, doc, beam);
    }
    for (const auto& r : doc.roofs) {
        b.owner = r.id;
        addRoof(b, doc, r);
    }
    for (const auto& r : doc.roofPlanes) {
        b.owner = r.id;
        addRoofPlane(b, doc, r);
    }
    return b.mesh;
}

ObjectId pickMesh(const Mesh& mesh, Vec3 o, Vec3 d) {
    // Moller-Trumbore against every triangle; plans are small enough.
    auto sub = [](Vec3 a, const float* b) { return Vec3{a.x - b[0], a.y - b[1], a.z - b[2]}; };
    auto edge = [](const float* a, const float* b) { return Vec3{b[0] - a[0], b[1] - a[1], b[2] - a[2]}; };
    auto crossV = [](Vec3 a, Vec3 b) { return Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; };
    auto dotV = [](Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; };

    double best = 1e30;
    ObjectId hit = kNoId;
    for (size_t t = 0; t < mesh.owners.size(); ++t) {
        const float* p0 = mesh.vertices[t * 3].position;
        const float* p1 = mesh.vertices[t * 3 + 1].position;
        const float* p2 = mesh.vertices[t * 3 + 2].position;
        const Vec3 e1 = edge(p0, p1), e2 = edge(p0, p2);
        const Vec3 pv = crossV(d, e2);
        const double det = dotV(e1, pv);
        if (std::abs(det) < 1e-12) continue;
        const double inv = 1.0 / det;
        const Vec3 tv = sub(o, p0);
        const double u = dotV(tv, pv) * inv;
        if (u < 0.0 || u > 1.0) continue;
        const Vec3 qv = crossV(tv, e1);
        const double v = dotV(d, qv) * inv;
        if (v < 0.0 || u + v > 1.0) continue;
        const double dist = dotV(e2, qv) * inv;
        if (dist > 1e-6 && dist < best) {
            best = dist;
            hit = mesh.owners[t];
        }
    }
    return hit;
}
