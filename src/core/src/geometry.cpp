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

std::vector<std::array<int, 3>> triangulate(const Polygon& poly) {
    std::vector<std::array<int, 3>> out;
    const int n = static_cast<int>(poly.size());
    if (n < 3) return out;
    std::vector<int> idx(n);
    for (int i = 0; i < n; ++i) idx[i] = i;
    if (signedArea(poly) < 0) std::reverse(idx.begin(), idx.end());

    auto inside = [](Vec2 p, Vec2 a, Vec2 b, Vec2 c) {
        return cross(b - a, p - a) >= -kEps && cross(c - b, p - b) >= -kEps && cross(a - c, p - c) >= -kEps;
    };
    int guard = 0;
    while (idx.size() > 3 && guard++ < n * n) {
        bool clipped = false;
        const int m = static_cast<int>(idx.size());
        for (int i = 0; i < m; ++i) {
            const int i0 = idx[(i + m - 1) % m], i1 = idx[i], i2 = idx[(i + 1) % m];
            const Vec2 a = poly[i0], b = poly[i1], c = poly[i2];
            if (cross(b - a, c - b) <= kEps) continue; // reflex or straight
            bool ear = true;
            for (int k : idx) {
                if (k == i0 || k == i1 || k == i2) continue;
                if (inside(poly[k], a, b, c)) {
                    ear = false;
                    break;
                }
            }
            if (!ear) continue;
            out.push_back({i0, i1, i2});
            idx.erase(idx.begin() + i);
            clipped = true;
            break;
        }
        if (!clipped) {
            // Degenerate (straight runs only): drop a vertex and carry on.
            idx.erase(idx.begin());
        }
    }
    if (idx.size() == 3) out.push_back({idx[0], idx[1], idx[2]});
    return out;
}

std::optional<Crossing> segmentCrossing(const Segment& s1, const Segment& s2, double tolerance) {
    const Vec2 r = s1.b - s1.a;
    const Vec2 s = s2.b - s2.a;
    const double denom = cross(r, s);
    if (std::abs(denom) < kEps) return std::nullopt;
    const double t = cross(s2.a - s1.a, s) / denom;
    const double u = cross(s2.a - s1.a, r) / denom;
    const double tt = tolerance / std::max(r.length(), kEps);
    const double tu = tolerance / std::max(s.length(), kEps);
    if (t < -tt || t > 1 + tt || u < -tu || u > 1 + tu) return std::nullopt;
    return Crossing{std::clamp(t, 0.0, 1.0), std::clamp(u, 0.0, 1.0)};
}

std::vector<std::pair<double, double>> clipToPolygon(const Segment& seg, const Polygon& poly) {
    std::vector<double> ts = {0.0, 1.0};
    for (size_t i = 0; i < poly.size(); ++i) {
        const Segment edge{poly[i], poly[(i + 1) % poly.size()]};
        if (auto c = segmentCrossing(seg, edge)) ts.push_back(c->t);
    }
    std::sort(ts.begin(), ts.end());
    std::vector<std::pair<double, double>> out;
    for (size_t i = 0; i + 1 < ts.size(); ++i) {
        const double t0 = ts[i], t1 = ts[i + 1];
        if (t1 - t0 < 1e-9) continue;
        if (!contains(poly, lerp(seg.a, seg.b, (t0 + t1) / 2))) continue;
        if (!out.empty() && std::abs(out.back().second - t0) < 1e-9) out.back().second = t1;
        else out.push_back({t0, t1});
    }
    return out;
}

Polygon replaceEdge(const Polygon& poly, int edge, Vec2 p, Vec2 dir) {
    const int n = static_cast<int>(poly.size());
    if (n < 3 || edge < 0 || edge >= n || dir.lengthSq() < kEps) return {};
    const int i0 = edge, i1 = (edge + 1) % n;
    const Segment line{p, p + dir};
    const auto x0 = lineIntersection(line, {poly[(i0 + n - 1) % n], poly[i0]});
    const auto x1 = lineIntersection(line, {poly[i1], poly[(i1 + 1) % n]});
    if (!x0 || !x1) return {};
    Polygon out = poly;
    out[i0] = *x0;
    out[i1] = *x1;
    return out;
}

Polygon moveEdge(const Polygon& poly, int edge, double offset) {
    const int n = static_cast<int>(poly.size());
    if (n < 3 || edge < 0 || edge >= n) return poly;
    const Vec2 a = poly[edge], b = poly[(edge + 1) % n];
    const Vec2 dir = (b - a).normalized();
    Polygon out = replaceEdge(poly, edge, a + dir.perp() * offset, dir);
    if (out.empty()) {
        // A neighbor runs along the edge: just slide both ends.
        out = poly;
        out[edge] = a + dir.perp() * offset;
        out[(edge + 1) % n] = b + dir.perp() * offset;
    }
    return out;
}

} // namespace oa
