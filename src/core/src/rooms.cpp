#include "oa/core/rooms.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace oa {
namespace {

constexpr double kJoinTolerance = 1.0;     // inches
constexpr double kMinRoomArea = 144.0;     // one square foot, in square inches

struct Edge {
    int a, b;
    double halfThickness;
};

// Index of the node at `p`, merging with one already within the tolerance.
int nodeAt(std::vector<Vec2>& nodes, Vec2 p) {
    for (size_t i = 0; i < nodes.size(); ++i)
        if (distance(nodes[i], p) <= kJoinTolerance) return static_cast<int>(i);
    nodes.push_back(p);
    return static_cast<int>(nodes.size()) - 1;
}

// Moves each edge of a counter-clockwise outline inward by its own offset.
Polygon inset(const Polygon& poly, const std::vector<double>& offsets) {
    const size_t n = poly.size();
    Polygon out(n);
    for (size_t i = 0; i < n; ++i) {
        const size_t prev = (i + n - 1) % n;
        const Vec2 dPrev = (poly[i] - poly[prev]).normalized();
        const Vec2 dNext = (poly[(i + 1) % n] - poly[i]).normalized();
        const Segment a{poly[prev] + dPrev.perp() * offsets[prev], poly[i] + dPrev.perp() * offsets[prev]};
        const Segment b{poly[i] + dNext.perp() * offsets[i], poly[(i + 1) % n] + dNext.perp() * offsets[i]};
        if (auto x = lineIntersection(a, b); x && std::abs(cross(dPrev, dNext)) > 1e-3) out[i] = *x;
        else out[i] = poly[i] + dNext.perp() * std::max(offsets[prev], offsets[i]);
    }
    return out;
}

} // namespace

std::vector<Polygon> findRoomOutlines(const Document& doc, ObjectId levelId) {
    std::vector<const Wall*> walls;
    for (const auto& w : doc.walls)
        if (w.levelId == levelId && w.length() > kJoinTolerance) walls.push_back(&w);

    // Split every wall where another meets or crosses it.
    std::vector<std::vector<double>> cuts(walls.size(), std::vector<double>{0.0, 1.0});
    for (size_t i = 0; i < walls.size(); ++i)
        for (size_t j = i + 1; j < walls.size(); ++j)
            if (auto c = segmentCrossing(walls[i]->centerline(), walls[j]->centerline(), kJoinTolerance)) {
                cuts[i].push_back(c->t);
                cuts[j].push_back(c->u);
            }

    std::vector<Vec2> nodes;
    std::map<std::pair<int, int>, double> unique; // undirected edge -> half thickness
    for (size_t i = 0; i < walls.size(); ++i) {
        auto& t = cuts[i];
        std::sort(t.begin(), t.end());
        const Segment line = walls[i]->centerline();
        int prev = nodeAt(nodes, line.a);
        for (size_t k = 1; k < t.size(); ++k) {
            const int next = nodeAt(nodes, lerp(line.a, line.b, t[k]));
            if (next == prev) continue;
            const auto key = std::minmax(prev, next);
            double& h = unique[{key.first, key.second}];
            h = std::max(h, walls[i]->thickness / 2.0);
            prev = next;
        }
    }

    // Drop dead ends, repeatedly, so only closed loops remain.
    std::vector<Edge> edges;
    for (auto& [k, h] : unique) edges.push_back({k.first, k.second, h});
    for (bool changed = true; changed;) {
        changed = false;
        std::vector<int> degree(nodes.size(), 0);
        for (const auto& e : edges) {
            ++degree[e.a];
            ++degree[e.b];
        }
        const auto n = edges.size();
        std::erase_if(edges, [&](const Edge& e) { return degree[e.a] < 2 || degree[e.b] < 2; });
        changed = edges.size() != n;
    }

    // Half-edges sorted around each node by angle.
    struct Half {
        int from, to;
        double half;
        bool used = false;
    };
    std::vector<Half> halves;
    std::vector<std::vector<int>> around(nodes.size());
    for (const auto& e : edges) {
        around[e.a].push_back(static_cast<int>(halves.size()));
        halves.push_back({e.a, e.b, e.halfThickness});
        around[e.b].push_back(static_cast<int>(halves.size()));
        halves.push_back({e.b, e.a, e.halfThickness});
    }
    auto angleOf = [&](int h) {
        const Vec2 d = nodes[halves[h].to] - nodes[halves[h].from];
        return std::atan2(d.y, d.x);
    };
    for (auto& list : around)
        std::sort(list.begin(), list.end(), [&](int x, int y) { return angleOf(x) < angleOf(y); });

    // Trace faces keeping each on the left: at every node take the next edge
    // clockwise from the one we arrived on.
    std::vector<Polygon> out;
    for (size_t start = 0; start < halves.size(); ++start) {
        if (halves[start].used) continue;
        Polygon poly;
        std::vector<double> offsets;
        int h = static_cast<int>(start);
        for (size_t guard = 0; guard <= halves.size() && !halves[h].used; ++guard) {
            halves[h].used = true;
            poly.push_back(nodes[halves[h].from]);
            offsets.push_back(halves[h].half);
            const auto& list = around[halves[h].to];
            const auto it = std::find_if(list.begin(), list.end(), [&](int x) {
                return halves[x].to == halves[h].from;
            });
            const int k = static_cast<int>(it - list.begin());
            const int count = static_cast<int>(list.size());
            h = list[(k + count - 1) % count];
        }
        if (poly.size() < 3 || signedArea(poly) < kMinRoomArea) continue; // the outside, or a sliver
        // Leave out straight-through nodes so the outline has only corners.
        Polygon corners;
        std::vector<double> cornerOffsets;
        for (size_t i = 0; i < poly.size(); ++i) {
            const Vec2 p0 = poly[(i + poly.size() - 1) % poly.size()], p1 = poly[i], p2 = poly[(i + 1) % poly.size()];
            const bool straight = std::abs(cross((p1 - p0).normalized(), (p2 - p1).normalized())) < 1e-6 &&
                                  dot(p1 - p0, p2 - p1) > 0;
            if (straight && std::abs(offsets[(i + poly.size() - 1) % poly.size()] - offsets[i]) < 1e-9) continue;
            corners.push_back(p1);
            cornerOffsets.push_back(offsets[i]);
        }
        Polygon inner = inset(corners, cornerOffsets);
        out.push_back(signedArea(inner) > kMinRoomArea ? inner : corners);
    }
    return out;
}

void updateRooms(Document& doc) {
    std::vector<Room> kept;
    for (const auto& level : doc.levels) {
        std::vector<const Room*> old;
        for (const auto& r : doc.rooms)
            if (r.levelId == level.id) old.push_back(&r);
        std::vector<bool> taken(old.size(), false);

        for (Polygon& outline : findRoomOutlines(doc, level.id)) {
            // The largest old room whose middle falls in this area keeps its identity.
            int best = -1;
            for (size_t i = 0; i < old.size(); ++i) {
                if (taken[i] || old[i]->outline.size() < 3) continue;
                if (!contains(outline, centroid(old[i]->outline))) continue;
                if (best < 0 || area(old[i]->outline) > area(old[best]->outline)) best = static_cast<int>(i);
            }
            Room r;
            if (best >= 0) {
                taken[best] = true;
                r = *old[best];
            } else {
                r.id = doc.newId();
                r.levelId = level.id;
                r.name = "Room";
            }
            r.outline = std::move(outline);
            kept.push_back(std::move(r));
        }
    }
    doc.rooms = std::move(kept);
}

} // namespace oa
