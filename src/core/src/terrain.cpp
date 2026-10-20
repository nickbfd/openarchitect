#include "oa/core/terrain.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace oa {

std::vector<std::array<int, 3>> delaunay(const std::vector<Vec2>& input) {
    // Drop points that sit on top of one another.
    std::vector<int> use;
    for (int i = 0; i < static_cast<int>(input.size()); ++i) {
        bool dup = false;
        for (int j : use)
            if (distance(input[i], input[j]) < 1e-6) dup = true;
        if (!dup) use.push_back(i);
    }
    if (use.size() < 3) return {};

    std::vector<Vec2> pts;
    for (int i : use) pts.push_back(input[i]);
    Vec2 lo = pts[0], hi = pts[0];
    for (Vec2 p : pts) {
        lo = {std::min(lo.x, p.x), std::min(lo.y, p.y)};
        hi = {std::max(hi.x, p.x), std::max(hi.y, p.y)};
    }
    const double span = std::max({hi.x - lo.x, hi.y - lo.y, 1.0}) * 20.0;
    const Vec2 mid = (lo + hi) * 0.5;
    const int n = static_cast<int>(pts.size());
    pts.push_back(mid + Vec2{-span, -span});
    pts.push_back(mid + Vec2{span, -span});
    pts.push_back(mid + Vec2{0, span});

    struct Tri {
        int a, b, c;
        Vec2 center;
        double r2;
    };
    auto makeTri = [&](int a, int b, int c) {
        if (cross(pts[b] - pts[a], pts[c] - pts[a]) < 0) std::swap(b, c);
        const Vec2 A = pts[a], B = pts[b], C = pts[c];
        const double d = 2.0 * (A.x * (B.y - C.y) + B.x * (C.y - A.y) + C.x * (A.y - B.y));
        Vec2 center = (A + B + C) * (1.0 / 3.0);
        if (std::abs(d) > 1e-12) {
            const double a2 = A.lengthSq(), b2 = B.lengthSq(), c2 = C.lengthSq();
            center = {(a2 * (B.y - C.y) + b2 * (C.y - A.y) + c2 * (A.y - B.y)) / d,
                      (a2 * (C.x - B.x) + b2 * (A.x - C.x) + c2 * (B.x - A.x)) / d};
        }
        return Tri{a, b, c, center, (A - center).lengthSq()};
    };
    std::vector<Tri> tris{makeTri(n, n + 1, n + 2)};
    for (int i = 0; i < n; ++i) {
        const Vec2 p = pts[i];
        std::map<std::pair<int, int>, int> edges; // boundary of the cavity
        std::vector<Tri> keep;
        for (const Tri& t : tris) {
            if ((p - t.center).lengthSq() < t.r2 * (1.0 + 1e-9)) {
                for (auto e : {std::pair{t.a, t.b}, std::pair{t.b, t.c}, std::pair{t.c, t.a}})
                    ++edges[std::minmax(e.first, e.second)];
            } else {
                keep.push_back(t);
            }
        }
        for (const auto& [e, count] : edges)
            if (count == 1) keep.push_back(makeTri(e.first, e.second, i));
        tris = std::move(keep);
    }
    std::vector<std::array<int, 3>> out;
    for (const Tri& t : tris) {
        if (t.a >= n || t.b >= n || t.c >= n) continue;
        if (std::abs(cross(pts[t.b] - pts[t.a], pts[t.c] - pts[t.a])) < 1e-9) continue;
        out.push_back({use[t.a], use[t.b], use[t.c]});
    }
    return out;
}

TerrainMesh terrainMesh(const Terrain& terrain) {
    TerrainMesh m;
    for (const auto& p : terrain.points) {
        m.points.push_back(p.position);
        m.elevations.push_back(p.elevation);
    }
    m.triangles = delaunay(m.points);
    return m;
}

std::vector<ContourSegment> contourLines(const Terrain& terrain) {
    std::vector<ContourSegment> out;
    const double step = terrain.contourInterval;
    if (step <= 0.0) return out;
    const TerrainMesh m = terrainMesh(terrain);
    for (const auto& t : m.triangles) {
        double zmin = 1e30, zmax = -1e30;
        for (int k : t) {
            zmin = std::min(zmin, m.elevations[k]);
            zmax = std::max(zmax, m.elevations[k]);
        }
        for (long n = static_cast<long>(std::ceil(zmin / step)); n * step <= zmax; ++n) {
            const double z = n * step;
            std::vector<Vec2> hits;
            for (int e = 0; e < 3; ++e) {
                const int i = t[e], j = t[(e + 1) % 3];
                const double zi = m.elevations[i], zj = m.elevations[j];
                if ((zi < z && zj >= z) || (zj < z && zi >= z))
                    hits.push_back(lerp(m.points[i], m.points[j], (z - zi) / (zj - zi)));
            }
            if (hits.size() == 2 && distance(hits[0], hits[1]) > 1e-6)
                out.push_back({z, hits[0], hits[1], n % 5 == 0});
        }
    }
    return out;
}

std::optional<double> terrainHeightAt(const Terrain& terrain, Vec2 p) {
    const TerrainMesh m = terrainMesh(terrain);
    for (const auto& t : m.triangles) {
        const Vec2 a = m.points[t[0]], b = m.points[t[1]], c = m.points[t[2]];
        const double area2 = cross(b - a, c - a);
        const double u = cross(c - b, p - b) / area2;
        const double v = cross(a - c, p - c) / area2;
        const double w = 1.0 - u - v;
        if (u >= -1e-9 && v >= -1e-9 && w >= -1e-9)
            return u * m.elevations[t[0]] + v * m.elevations[t[1]] + w * m.elevations[t[2]];
    }
    return std::nullopt;
}

} // namespace oa
