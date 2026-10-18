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

class Builder {
public:
    Mesh mesh;

    void triangle(Vec3 a, Vec3 b, Vec3 c, Color col) {
        const Vec3 u{b.x - a.x, b.y - a.y, b.z - a.z};
        const Vec3 v{c.x - a.x, c.y - a.y, c.z - a.z};
        Vec3 n{u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x};
        const double len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
        if (len < 1e-12) return;
        n = {n.x / len, n.y / len, n.z / len};
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

    // Cut openings by splitting the wall into solid pieces.
    double cursor = 0.0;
    for (const Opening* o : doc.openingsIn(w.id)) {
        const double s0 = std::clamp(o->offset - o->width / 2.0, 0.0, len);
        const double s1 = std::clamp(o->offset + o->width / 2.0, 0.0, len);
        if (s1 <= cursor) continue;
        const double from = std::max(s0, cursor);
        b.box(w.a + dir * cursor, dir, from - cursor, w.thickness, z0, z0 + h, kWall);
        const double sill = std::clamp(o->sillHeight, 0.0, h);
        const double head = std::clamp(o->sillHeight + o->height, 0.0, h);
        b.box(w.a + dir * from, dir, s1 - from, w.thickness, z0, z0 + sill, kWall);
        b.box(w.a + dir * from, dir, s1 - from, w.thickness, z0 + head, z0 + h, kWall);
        cursor = s1;
    }
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
    // The roof surface sits on the rafters, above the wall plate.
    constexpr double kHeel = 8.0;
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

Mesh buildMesh(const Document& doc) {
    Builder b;
    for (const auto& p : doc.platforms) addPlatform(b, doc, p);
    for (const auto& w : doc.walls) addWall(b, doc, w);
    for (const auto& beam : doc.beams) addBeam(b, doc, beam);
    for (const auto& r : doc.roofs) addRoof(b, doc, r);
    return b.mesh;
}
