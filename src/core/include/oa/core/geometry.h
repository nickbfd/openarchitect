#pragma once

#include <array>
#include <cmath>
#include <optional>
#include <utility>
#include <vector>

namespace oa {

constexpr double kPi = 3.14159265358979323846;
// Tolerance for model geometry, in inches.
constexpr double kEps = 1e-6;
constexpr double kSnapTolerance = 0.5;

struct Vec2 {
    double x = 0.0;
    double y = 0.0;

    constexpr Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
    constexpr Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
    constexpr Vec2 operator*(double s) const { return {x * s, y * s}; }
    constexpr Vec2 operator/(double s) const { return {x / s, y / s}; }
    constexpr Vec2 operator-() const { return {-x, -y}; }
    Vec2& operator+=(Vec2 o) { x += o.x; y += o.y; return *this; }
    Vec2& operator-=(Vec2 o) { x -= o.x; y -= o.y; return *this; }
    constexpr bool operator==(const Vec2&) const = default;

    double length() const { return std::hypot(x, y); }
    constexpr double lengthSq() const { return x * x + y * y; }
    Vec2 normalized() const {
        double l = length();
        return l > kEps ? *this / l : Vec2{};
    }
    // Left-hand perpendicular (rotated +90 degrees).
    constexpr Vec2 perp() const { return {-y, x}; }
};

constexpr double dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
constexpr double cross(Vec2 a, Vec2 b) { return a.x * b.y - a.y * b.x; }
inline double distance(Vec2 a, Vec2 b) { return (a - b).length(); }
constexpr Vec2 lerp(Vec2 a, Vec2 b, double t) { return a + (b - a) * t; }

struct Vec3 {
    double x = 0.0, y = 0.0, z = 0.0;
};

struct Segment {
    Vec2 a, b;

    double length() const { return distance(a, b); }
    Vec2 direction() const { return (b - a).normalized(); }
    // Parameter of the projection of p onto the infinite line, 0 at a, 1 at b.
    double project(Vec2 p) const;
    Vec2 closestPoint(Vec2 p) const;
    double distanceTo(Vec2 p) const { return distance(p, closestPoint(p)); }
};

// Overlap of segment `other` projected onto `base`, as distances along
// `base` from base.a, when both are parallel and within `offsetTolerance`
// of each other. Used to find which wall or beam carries another.
struct Overlap {
    double start = 0.0;
    double end = 0.0;
    double length() const { return end - start; }
};
std::optional<Overlap> collinearOverlap(const Segment& base, const Segment& other,
                                        double offsetTolerance, double angleToleranceRad = 0.01);

std::optional<Vec2> lineIntersection(const Segment& s1, const Segment& s2);

using Polygon = std::vector<Vec2>;

// Positive for counter-clockwise polygons (y up).
double signedArea(const Polygon& poly);
inline double area(const Polygon& poly) { return std::abs(signedArea(poly)); }
Vec2 centroid(const Polygon& poly);
bool contains(const Polygon& poly, Vec2 p);
// Splits a simple polygon (either winding) into triangles by ear clipping,
// as index triples into `poly`.
std::vector<std::array<int, 3>> triangulate(const Polygon& poly);
// Moves edge `edge` (from vertex edge to edge + 1) of a polygon parallel to
// itself by `offset` along its left normal, keeping the directions of the
// two neighboring edges, the way a drafter stretches one side of a shape.
Polygon moveEdge(const Polygon& poly, int edge, double offset);
// Replaces edge `edge` of a polygon with the line through `p` along `dir`,
// sliding its ends along the neighboring edges. Empty if a neighbor is
// parallel to the line.
Polygon replaceEdge(const Polygon& poly, int edge, Vec2 p, Vec2 dir);

// The parts of a segment inside a polygon, as parameter ranges 0..1 along it.
std::vector<std::pair<double, double>> clipToPolygon(const Segment& s, const Polygon& poly);
// The point where segments cross, as parameters along each, if they do
// (ends included, within `tolerance` inches).
struct Crossing {
    double t = 0.0; // along the first
    double u = 0.0; // along the second
};
std::optional<Crossing> segmentCrossing(const Segment& s1, const Segment& s2, double tolerance = kEps);

struct Rect {
    Vec2 min, max;

    double width() const { return max.x - min.x; }
    double height() const { return max.y - min.y; }
    Vec2 center() const { return (min + max) * 0.5; }
    bool contains(Vec2 p) const { return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y; }
    Polygon corners() const { return {min, {max.x, min.y}, max, {min.x, max.y}}; }
    static Rect fromPoints(Vec2 a, Vec2 b);
};

} // namespace oa
