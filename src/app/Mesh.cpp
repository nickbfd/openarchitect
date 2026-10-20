#include "Mesh.h"

#include "oa/structural/members.h"

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
    if (!level || w.length() < 1e-6) return;
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
    if (!level || r.eaveLength() < 1.0 || r.run < 1.0) return;
    const double slope = r.pitch / 12.0;
    const double wallTop = level->elevation + level->wallHeight;
    const double plate = wallTop + kHeel;
    const Vec2 up = r.upslope();
    auto at = [](Vec2 p, double z) { return Vec3{p.x, p.y, z}; };

    const Vec2 e0 = r.a - up * r.overhang, e1 = r.b - up * r.overhang;
    const Segment high = r.highLine();
    const double eaveZ = plate - r.overhang * slope;
    const double highZ = plate + r.run * slope;
    b.quad(at(e0, eaveZ), at(e1, eaveZ), at(high.b, highZ), at(high.a, highZ), kRoof);
    // Fill below the plane at both ends and along the high side.
    b.triangle(at(r.b, plate), at(high.b, plate), at(high.b, highZ), kGable);
    b.triangle(at(high.a, plate), at(r.a, plate), at(high.a, highZ), kGable);
    b.quad(at(r.a, wallTop), at(r.b, wallTop), at(r.b, plate), at(r.a, plate), kGable);
    b.quad(at(high.b, wallTop), at(high.a, wallTop), at(high.a, highZ), at(high.b, highZ), kGable);
    b.quad(at(r.b, wallTop), at(high.b, wallTop), at(high.b, plate), at(r.b, plate), kGable);
    b.quad(at(high.a, wallTop), at(r.a, wallTop), at(r.a, plate), at(high.a, plate), kGable);
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

Mesh buildMesh(const Document& doc, ObjectId selected) {
    Builder b;
    b.selected = selected;
    for (const auto& p : doc.platforms) {
        b.owner = p.id;
        addPlatform(b, doc, p);
    }
    for (const auto& w : doc.walls) addWall(b, doc, w);
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
