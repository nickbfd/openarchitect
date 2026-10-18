#include "oa/core/geometry.h"

#include <algorithm>

namespace oa {

double Segment::project(Vec2 p) const {
    Vec2 d = b - a;
    double l2 = d.lengthSq();
    return l2 > kEps ? dot(p - a, d) / l2 : 0.0;
}

Vec2 Segment::closestPoint(Vec2 p) const {
    return lerp(a, b, std::clamp(project(p), 0.0, 1.0));
}

std::optional<Overlap> collinearOverlap(const Segment& base, const Segment& other,
                                        double offsetTolerance, double angleToleranceRad) {
    const Vec2 u = base.direction();
    const Vec2 v = other.direction();
    if (u.lengthSq() < kEps || v.lengthSq() < kEps) return std::nullopt;
    if (std::abs(cross(u, v)) > std::sin(angleToleranceRad)) return std::nullopt;

    // Perpendicular offset of the other segment's midpoint from the base line.
    const Vec2 mid = (other.a + other.b) * 0.5;
    if (std::abs(cross(u, mid - base.a)) > offsetTolerance) return std::nullopt;

    double s0 = dot(other.a - base.a, u);
    double s1 = dot(other.b - base.a, u);
    if (s0 > s1) std::swap(s0, s1);
    const double lo = std::max(0.0, s0);
    const double hi = std::min(base.length(), s1);
    if (hi - lo < kSnapTolerance) return std::nullopt;
    return Overlap{lo, hi};
}

std::optional<Vec2> lineIntersection(const Segment& s1, const Segment& s2) {
    const Vec2 r = s1.b - s1.a;
    const Vec2 s = s2.b - s2.a;
    const double denom = cross(r, s);
    if (std::abs(denom) < kEps) return std::nullopt;
    const double t = cross(s2.a - s1.a, s) / denom;
    return s1.a + r * t;
}

double signedArea(const Polygon& poly) {
    double sum = 0.0;
    for (size_t i = 0, n = poly.size(); i < n; ++i)
        sum += cross(poly[i], poly[(i + 1) % n]);
    return sum * 0.5;
}

Vec2 centroid(const Polygon& poly) {
    const double a = signedArea(poly);
    if (std::abs(a) < kEps) {
        Vec2 c;
        for (auto p : poly) c += p;
        return poly.empty() ? c : c / static_cast<double>(poly.size());
    }
    Vec2 c;
    for (size_t i = 0, n = poly.size(); i < n; ++i) {
        const Vec2 p = poly[i], q = poly[(i + 1) % n];
        c += (p + q) * cross(p, q);
    }
    return c / (6.0 * a);
}

bool contains(const Polygon& poly, Vec2 p) {
    bool inside = false;
    for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        const Vec2 a = poly[i], b = poly[j];
        if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x)
            inside = !inside;
    }
    return inside;
}

Rect Rect::fromPoints(Vec2 a, Vec2 b) {
    return {{std::min(a.x, b.x), std::min(a.y, b.y)}, {std::max(a.x, b.x), std::max(a.y, b.y)}};
}

} // namespace oa
