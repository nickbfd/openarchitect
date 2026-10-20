#include "oa/core/roof_builder.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace oa {
namespace {

constexpr double kTimeEps = 1e-9;
constexpr double kPointEps = 1e-4;

// The skeleton is found by shrinking the outline: every edge moves inward
// along its normal at its own speed, `t` being the height reached. Edge i's
// line at height t is n·x = c + s·t. An edge that changes pitch partway up
// gets a new line from that height on, with its own face.
struct Line {
    Vec2 n;   // inward unit normal
    Vec2 dir; // direction of the original edge
    double c = 0, s = 0;
    size_t edge = 0;   // the outline edge it came from
    int phase = 0;     // 0 from the plate, 1 after the change of pitch
    double start = 0;  // height it starts at
};

// A corner of the shrinking outline, where line `in` (the edge arriving)
// meets line `out` (the edge leaving). It moves in a straight line.
struct Corner {
    Vec2 p0;
    double t0 = 0;
    Vec2 vel;
    int in = 0, out = 0;
    Vec2 at(double t) const { return p0 + vel * (t - t0); }
};

class Skeleton {
    struct Change {
        double height;
        int line;
        double speed;
    };

public:
    Skeleton(const Polygon& outline, const std::vector<SkeletonEdge>& edges) : faces_(outline.size()) {
        const size_t n = outline.size();
        for (size_t i = 0; i < n; ++i) {
            const Vec2 a = outline[i], b = outline[(i + 1) % n];
            Line l;
            l.dir = (b - a).normalized();
            l.n = l.dir.perp();
            l.c = dot(l.n, a);
            l.s = edges[i].speed;
            l.edge = i;
            lines_.push_back(l);
            faces_[i].push_back({a, b});
            if (edges[i].changeHeight > kTimeEps) changes_.push_back({edges[i].changeHeight, static_cast<int>(i), edges[i].upperSpeed});
        }
        std::sort(changes_.begin(), changes_.end(), [](const Change& x, const Change& y) { return x.height < y.height; });
        std::vector<int> ring;
        for (size_t i = 0; i < n; ++i) {
            const int in = static_cast<int>((i + n - 1) % n), out = static_cast<int>(i);
            ring.push_back(addCorner(outline[i], 0.0, in, out));
        }
        rings_.push_back(ring);
    }

    std::vector<SkeletonFace> run() {
        const size_t limit = 50 * lines_.size() * lines_.size() + 100;
        for (size_t step = 0; step < limit && !rings_.empty(); ++step) {
            // Rings that have shrunk to nothing end here.
            for (size_t r = 0; r < rings_.size();) {
                if (rings_[r].size() < 3 || std::abs(ringArea(rings_[r], now_)) < 1e-3) {
                    collapse(rings_[r]);
                    rings_.erase(rings_.begin() + static_cast<long>(r));
                } else {
                    ++r;
                }
            }
            if (rings_.empty()) break;
            if (!nextEvent()) {
                // Nothing left to meet: whatever remains is a flat top.
                for (auto& ring : rings_) collapse(ring);
                rings_.clear();
            }
        }
        std::vector<SkeletonFace> out;
        for (size_t i = 0; i < faces_.size(); ++i)
            out.push_back({lines_[i].edge, lines_[i].phase, lines_[i].start, assemble(i)});
        return out;
    }

private:
    int addCorner(Vec2 p, double t, int in, int out) {
        Corner c;
        c.p0 = p;
        c.t0 = t;
        c.in = in;
        c.out = out;
        const Line &a = lines_[in], &b = lines_[out];
        const double det = cross(a.n, b.n);
        if (std::abs(det) > 1e-9) {
            // Solve [a.n; b.n] v = [a.s; b.s].
            c.vel = {(b.n.y * a.s - a.n.y * b.s) / det, (-b.n.x * a.s + a.n.x * b.s) / det};
        } else {
            // Edges in one straight line with different speeds: follow the faster one.
            c.vel = a.n * std::max(a.s, b.s);
        }
        corners_.push_back(c);
        return static_cast<int>(corners_.size() - 1);
    }

    bool parallel(int in, int out) const { return std::abs(cross(lines_[in].n, lines_[out].n)) < 1e-9; }

    double ringArea(const std::vector<int>& ring, double t) const {
        Polygon p;
        for (int k : ring) p.push_back(corners_[k].at(t));
        return signedArea(p);
    }

    // A corner stops at `p`: its path is a skeleton arc between its two faces.
    void finish(int k, Vec2 p) {
        const Corner& c = corners_[k];
        if (distance(c.p0, p) > kPointEps) {
            faces_[c.in].push_back({c.p0, p});
            faces_[c.out].push_back({c.p0, p});
        }
    }

    // A ring with no area left: its corners stop where they are, and its
    // edges are where faces meet flat (a ridge between parallel eaves).
    void collapse(const std::vector<int>& ring) {
        std::vector<Vec2> at;
        for (int k : ring) {
            at.push_back(corners_[k].at(now_));
            finish(k, at.back());
        }
        for (size_t i = 0; i < ring.size(); ++i) {
            const Vec2 p = at[i], q = at[(i + 1) % ring.size()];
            if (distance(p, q) > kPointEps) faces_[corners_[ring[i]].out].push_back({p, q});
        }
    }

    bool nextEvent() {
        double best = 1e300;
        int kind = 0; // 1 edge, 2 split
        size_t bestRing = 0, bestIndex = 0, bestEdge = 0;
        for (size_t r = 0; r < rings_.size(); ++r) {
            const auto& ring = rings_[r];
            const size_t m = ring.size();
            for (size_t i = 0; i < m; ++i) {
                const Corner &u = corners_[ring[i]], &w = corners_[ring[(i + 1) % m]];
                // The edge from u to w shrinks to nothing.
                const Vec2 dir = lines_[u.out].dir;
                const double len = dot(w.at(now_) - u.at(now_), dir);
                const double rate = dot(w.vel - u.vel, dir);
                if (rate < -1e-12) {
                    const double t = now_ - len / rate;
                    if (t >= now_ - kTimeEps && t < best) {
                        best = std::max(t, now_);
                        kind = 1;
                        bestRing = r;
                        bestIndex = i;
                    }
                }
            }
            for (size_t i = 0; i < m; ++i) {
                const Corner& v = corners_[ring[i]];
                if (cross(lines_[v.in].dir, lines_[v.out].dir) >= -1e-9) continue; // convex: can't split
                for (size_t j = 0; j < m; ++j) {
                    const size_t jn = (j + 1) % m;
                    if (j == i || jn == i) continue;
                    const Corner &fa = corners_[ring[j]], &fb = corners_[ring[jn]];
                    const Line& f = lines_[fa.out];
                    // Distance of v ahead of the edge's line, shrinking to zero.
                    const double g = dot(f.n, v.at(now_)) - (f.c + f.s * now_);
                    const double rate = dot(f.n, v.vel) - f.s;
                    if (rate > -1e-12 || g < -1e-6) continue;
                    const double t = now_ - g / rate;
                    if (t < now_ - kTimeEps || t >= best) continue;
                    // It must land on the edge itself, between its corners then.
                    const Vec2 p = v.at(t), pa = fa.at(t), pb = fb.at(t);
                    const double along = dot(p - pa, f.dir), len = dot(pb - pa, f.dir);
                    if (len < 1e-6 || along < 1e-6 || along > len - 1e-6) continue;
                    best = std::max(t, now_);
                    kind = 2;
                    bestRing = r;
                    bestIndex = i;
                    bestEdge = j;
                }
            }
        }
        // Edges changing pitch before anything else happens.
        while (!changes_.empty() && changes_.front().height <= best + kTimeEps) {
            const Change ch = changes_.front();
            changes_.erase(changes_.begin());
            if (ch.height < now_ - kTimeEps) continue;
            now_ = std::max(now_, ch.height);
            changePitch(ch);
            return true;
        }
        if (kind == 0) return false;
        now_ = best;
        if (kind == 1) edgeEvent(bestRing, bestIndex);
        else splitEvent(bestRing, bestIndex, bestEdge);
        return true;
    }

    void edgeEvent(size_t r, size_t i) {
        auto& ring = rings_[r];
        const size_t m = ring.size(), j = (i + 1) % m;
        const int u = ring[i], w = ring[j];
        const Vec2 p = (corners_[u].at(now_) + corners_[w].at(now_)) * 0.5;
        const int in = corners_[u].in, out = corners_[w].out;
        if (parallel(in, out)) {
            // The edges either side are parallel and meet along a line: done.
            collapse(ring);
            rings_.erase(rings_.begin() + static_cast<long>(r));
            return;
        }
        finish(u, p);
        finish(w, p);
        const int x = addCorner(p, now_, in, out);
        ring[i] = x;
        ring.erase(ring.begin() + static_cast<long>(j));
    }

    void splitEvent(size_t r, size_t i, size_t j) {
        std::vector<int> ring = rings_[r];
        rings_.erase(rings_.begin() + static_cast<long>(r));
        const size_t m = ring.size();
        std::rotate(ring.begin(), ring.begin() + static_cast<long>(i), ring.end());
        const size_t fa = (j + m - i) % m, fb = fa + 1; // fb may equal m (wraps to v, not possible) or < m
        const int v = ring[0];
        const Vec2 p = corners_[v].at(now_);
        const int edge = corners_[ring[fa]].out;
        finish(v, p);
        const int x1 = addCorner(p, now_, corners_[v].in, edge);
        const int x2 = addCorner(p, now_, edge, corners_[v].out);
        std::vector<int> one{x1}, two{x2};
        for (size_t k = fb; k < m; ++k) one.push_back(ring[k]);
        for (size_t k = 1; k <= fa; ++k) two.push_back(ring[k]);
        rings_.push_back(one);
        rings_.push_back(two);
    }

    // Edge `ch.line` takes its new speed from here up. Each stretch of it
    // still on the outline becomes a new line, with a face that starts
    // along the stretch; the face below closes along it.
    void changePitch(const Change& ch) {
        for (auto& ring : rings_) {
            const size_t m = ring.size();
            for (size_t i = 0; i < m; ++i) {
                if (corners_[ring[i]].out != ch.line) continue;
                const size_t j = (i + 1) % m;
                const int u = ring[i], w = ring[j];
                const Vec2 pu = corners_[u].at(now_), pw = corners_[w].at(now_);
                Line l = lines_[ch.line];
                l.c += (l.s - ch.speed) * now_;
                l.s = ch.speed;
                l.phase = lines_[ch.line].phase + 1;
                l.start = now_;
                lines_.push_back(l);
                const int nl = static_cast<int>(lines_.size() - 1);
                faces_.push_back({{pu, pw}});
                if (distance(pu, pw) > kPointEps) faces_[ch.line].push_back({pu, pw});
                finish(u, pu);
                finish(w, pw);
                ring[i] = addCorner(pu, now_, corners_[u].in, nl);
                ring[j] = addCorner(pw, now_, nl, corners_[w].out);
            }
        }
    }

    // The face of edge i: its pieces chained into a loop from the edge's end
    // back to its start.
    Polygon assemble(size_t i) const {
        auto segs = faces_[i];
        const Vec2 start = segs.front().first, end = segs.front().second;
        segs.erase(segs.begin());
        Polygon poly{start, end};
        Vec2 at = end;
        std::vector<bool> used(segs.size(), false);
        for (size_t guard = 0; guard <= segs.size(); ++guard) {
            if (distance(at, start) < 1e-3) break;
            int pick = -1;
            Vec2 next;
            for (size_t k = 0; k < segs.size(); ++k) {
                if (used[k]) continue;
                if (distance(segs[k].first, at) < 1e-3) next = segs[k].second;
                else if (distance(segs[k].second, at) < 1e-3) next = segs[k].first;
                else continue;
                pick = static_cast<int>(k);
                break;
            }
            if (pick < 0) return {};
            used[pick] = true;
            if (distance(next, start) >= 1e-3) poly.push_back(next);
            at = next;
        }
        if (distance(at, start) >= 1e-3) return {};
        // Drop points in a straight line with their neighbors.
        Polygon clean;
        for (size_t k = 0; k < poly.size(); ++k) {
            const Vec2 prev = poly[(k + poly.size() - 1) % poly.size()], p = poly[k],
                       next = poly[(k + 1) % poly.size()];
            if (std::abs(cross(p - prev, next - p)) > 1e-6 * std::max(1.0, distance(prev, next))) clean.push_back(p);
        }
        if (clean.size() < 3 || area(clean) < 1.0) return {};
        // Start with the edge itself.
        auto it = std::find_if(clean.begin(), clean.end(), [&](Vec2 q) { return distance(q, start) < 1e-3; });
        if (it != clean.end()) std::rotate(clean.begin(), it, clean.end());
        return clean;
    }

    std::vector<Change> changes_;
    std::vector<Line> lines_;
    std::vector<Corner> corners_;
    std::vector<std::vector<int>> rings_;
    std::vector<std::vector<std::pair<Vec2, Vec2>>> faces_;
    double now_ = 0.0;
};

bool isExteriorWall(const Document& doc, const Wall& w) {
    if (w.roomDivider) return false;
    if (const WallType* t = doc.wallType(w.wallTypeId)) return t->kind == WallKind::Exterior;
    return w.bearing;
}

double wallPitch(const Document& doc, const Wall& w) {
    return w.roof.pitch > 0.0 ? w.roof.pitch : doc.roofDefaults.pitch;
}

double wallOverhang(const Document& doc, const Wall& w) {
    if (w.roof.overhang >= 0.0) return w.roof.overhang;
    return w.roof.kind == WallRoof::Sloped || w.roof.kind == WallRoof::DutchGable ? doc.roofDefaults.eaveOverhang
                                                                                 : doc.roofDefaults.gableOverhang;
}

bool sameRoof(const Document& doc, const Wall& a, const Wall& b) {
    return a.roof.kind == b.roof.kind && std::abs(wallPitch(doc, a) - wallPitch(doc, b)) < 1e-9 &&
           std::abs(wallOverhang(doc, a) - wallOverhang(doc, b)) < 1e-9 &&
           a.roof.hasUpperPitch() == b.roof.hasUpperPitch() &&
           (!a.roof.hasUpperPitch() || (a.roof.upperPitch == b.roof.upperPitch && a.roof.upperStart == b.roof.upperStart)) &&
           (a.roof.kind != WallRoof::DutchGable || a.roof.upperStart == b.roof.upperStart);
}

std::optional<Vec2> intersect(Vec2 p, Vec2 d, Vec2 q, Vec2 e) {
    const double den = cross(d, e);
    if (std::abs(den) < 1e-9) return std::nullopt;
    return p + d * (cross(q - p, e) / den);
}

bool samePlane(const RoofPlane& x, const RoofPlane& y) {
    if (x.levelId != y.levelId || distance(x.a, y.a) > 1e-6 || distance(x.b, y.b) > 1e-6 ||
        std::abs(x.pitch - y.pitch) > 1e-9 || std::abs(x.baseHeight - y.baseHeight) > 1e-6 ||
        x.outline.size() != y.outline.size() || x.highBearing != y.highBearing)
        return false;
    for (size_t i = 0; i < x.outline.size(); ++i)
        if (distance(x.outline[i], y.outline[i]) > 1e-6) return false;
    return true;
}

std::vector<RoofPlane> automaticPlanes(const Document& doc, ObjectId levelId) {
    std::vector<RoofPlane> out;
    for (const auto& r : doc.roofPlanes)
        if (r.automatic && (levelId == kNoId || r.levelId == levelId)) out.push_back(r);
    return out;
}

} // namespace

std::vector<SkeletonFace> skeletonFaces(const Polygon& outline, const std::vector<SkeletonEdge>& edges) {
    if (outline.size() < 3 || edges.size() != outline.size() || signedArea(outline) <= 0) return {};
    return Skeleton(outline, edges).run();
}

std::vector<Polygon> skeletonFaces(const Polygon& outline, const std::vector<double>& speed) {
    if (speed.size() != outline.size()) return {};
    std::vector<SkeletonEdge> edges;
    for (double v : speed) edges.push_back({v, -1.0, 0.0});
    std::vector<Polygon> out;
    for (auto& f : skeletonFaces(outline, edges)) out.push_back(std::move(f.outline));
    return out;
}

std::vector<LoopEdge> exteriorWallLoop(const Document& doc, ObjectId levelId) {
    // Wall ends joined within half an inch are one node.
    std::vector<Vec2> nodes;
    auto node = [&](Vec2 p) {
        for (size_t i = 0; i < nodes.size(); ++i)
            if (distance(nodes[i], p) <= 0.5) return static_cast<int>(i);
        nodes.push_back(p);
        return static_cast<int>(nodes.size() - 1);
    };
    struct Link {
        int a, b;
        ObjectId wall;
    };
    std::vector<Link> links;
    for (const auto& w : doc.walls)
        if (w.levelId == levelId && isExteriorWall(doc, w) && w.length() > 1.0) links.push_back({node(w.a), node(w.b), w.id});
    // Drop walls that dead-end, repeatedly, so only closed loops remain.
    for (bool pruned = true; pruned;) {
        pruned = false;
        std::vector<int> degree(nodes.size(), 0);
        for (const auto& l : links) ++degree[l.a], ++degree[l.b];
        const size_t before = links.size();
        std::erase_if(links, [&](const Link& l) { return degree[l.a] < 2 || degree[l.b] < 2; });
        pruned = links.size() != before;
    }
    if (links.size() < 3) return {};

    // Walk the outside of the walls counterclockwise from the lowest
    // leftmost node, turning as far right as possible at each junction.
    int start = links.front().a;
    for (const auto& l : links)
        for (int k : {l.a, l.b}) {
            const Vec2 p = nodes[k], s = nodes[start];
            if (p.x < s.x - 1e-9 || (std::abs(p.x - s.x) < 1e-9 && p.y < s.y)) start = k;
        }
    std::vector<LoopEdge> loop;
    Vec2 heading{0, -1};
    int at = start;
    std::set<size_t> used;
    for (size_t guard = 0; guard <= links.size(); ++guard) {
        int pick = -1;
        double bestTurn = 1e9;
        for (size_t k = 0; k < links.size(); ++k) {
            if (used.count(k)) continue;
            const Link& l = links[k];
            if (l.a != at && l.b != at) continue;
            const int other = l.a == at ? l.b : l.a;
            const Vec2 d = (nodes[other] - nodes[at]).normalized();
            const double turn = std::atan2(cross(heading, d), dot(heading, d));
            if (turn < bestTurn) {
                bestTurn = turn;
                pick = static_cast<int>(k);
            }
        }
        if (pick < 0) return {};
        used.insert(static_cast<size_t>(pick));
        const Link& l = links[pick];
        const int other = l.a == at ? l.b : l.a;
        loop.push_back({nodes[at], nodes[other], l.wall});
        heading = (nodes[other] - nodes[at]).normalized();
        at = other;
        if (at == start) break;
    }
    if (at != start || loop.size() < 3) return {};
    Polygon poly;
    for (const auto& e : loop) poly.push_back(e.a);
    if (signedArea(poly) <= 0) return {};

    // Join straight runs of walls with the same roof settings.
    for (bool merged = true; merged && loop.size() > 3;) {
        merged = false;
        for (size_t i = 0; i < loop.size(); ++i) {
            const size_t j = (i + 1) % loop.size();
            const Vec2 d1 = (loop[i].b - loop[i].a).normalized(), d2 = (loop[j].b - loop[j].a).normalized();
            if (std::abs(cross(d1, d2)) > 1e-6 || dot(d1, d2) < 0) continue;
            if (!sameRoof(doc, *doc.wall(loop[i].wallId), *doc.wall(loop[j].wallId))) continue;
            loop[i].b = loop[j].b;
            loop.erase(loop.begin() + static_cast<long>(j));
            merged = true;
            break;
        }
    }
    return loop;
}

std::vector<RoofPlane> buildRoofPlanes(const Document& doc, ObjectId levelId) {
    const auto loop = exteriorWallLoop(doc, levelId);
    if (loop.empty()) return {};
    const size_t n = loop.size();
    Polygon outline;
    std::vector<SkeletonEdge> edges;
    std::vector<double> overhang;
    std::vector<const Wall*> walls;
    for (const auto& e : loop) {
        const Wall* w = doc.wall(e.wallId);
        walls.push_back(w);
        outline.push_back(e.a);
        SkeletonEdge se;
        const bool sloped = w->roof.kind == WallRoof::Sloped || w->roof.kind == WallRoof::DutchGable;
        se.speed = sloped ? 12.0 / std::max(wallPitch(doc, *w), 0.25) : 0.0;
        if (w->roof.kind == WallRoof::DutchGable) {
            se.changeHeight = std::max(w->roof.upperStart, 1.0);
            se.upperSpeed = 0.0;
        } else if (w->roof.hasUpperPitch()) {
            se.changeHeight = std::max(w->roof.upperStart, 1.0);
            se.upperSpeed = 12.0 / std::max(w->roof.upperPitch, 0.25);
        }
        edges.push_back(se);
        overhang.push_back(wallOverhang(doc, *w));
    }
    const auto faces = skeletonFaces(outline, edges);
    if (faces.size() < n) return {};

    // Corners of the roof's edge: where the overhangs of neighboring walls meet.
    auto edgeDir = [&](size_t i) { return (outline[(i + 1) % n] - outline[i]).normalized(); };
    std::vector<Vec2> eaveCorner(n);
    for (size_t i = 0; i < n; ++i) {
        const size_t h = (i + n - 1) % n;
        const Vec2 dh = edgeDir(h), di = edgeDir(i);
        const Vec2 ph = outline[i] - dh.perp() * overhang[h], pi = outline[i] - di.perp() * overhang[i];
        eaveCorner[i] = intersect(ph, dh, pi, di).value_or(pi);
    }

    // Where the roof goes up vertically: gable and high shed walls, and the
    // gable of a Dutch gable, which stands back from its wall as far as the
    // hip below it reaches.
    struct Vertical {
        size_t edge;
        int phase;
        double inset;    // inches inside the wall line
        double overhang; // how far the roof edge goes out past it
    };
    std::vector<Vertical> verticals;
    for (size_t g = 0; g < n; ++g) {
        if (edges[g].speed <= 0.0) verticals.push_back({g, 0, 0.0, overhang[g]});
        else if (edges[g].changeHeight > 0.0 && edges[g].upperSpeed <= 0.0) {
            const double inset = edges[g].speed * edges[g].changeHeight;
            verticals.push_back({g, 1, inset, std::min(doc.roofDefaults.gableOverhang, inset)});
        }
    }
    auto onVertical = [&](Vec2 p, const Vertical& v) {
        const Vec2 d = edgeDir(v.edge), a = outline[v.edge] + d.perp() * v.inset;
        const double along = dot(p - a, d), len = distance(outline[v.edge], outline[(v.edge + 1) % n]);
        return std::abs(cross(d, p - a)) < 1e-3 && along > -1e-3 && along < len + 1e-3;
    };
    auto onEdge = [&](Vec2 p, size_t g) { return onVertical(p, {g, 0, 0.0, 0.0}); };

    std::vector<RoofPlane> planes;
    for (const auto& face : faces) {
        const size_t i = face.edge;
        const double speed = face.phase == 0 ? edges[i].speed : edges[i].upperSpeed;
        if (speed <= 0.0 || face.outline.size() < 3) continue;
        const Polygon& poly = face.outline;
        RoofPlane r;
        r.levelId = levelId;
        r.a = outline[i];
        r.b = outline[(i + 1) % n];
        r.pitch = face.phase == 0 ? wallPitch(doc, *walls[i]) : walls[i]->roof.upperPitch;
        // An upper plane starts at the break: its height there, carried
        // back down its slope to the wall line.
        r.baseHeight = face.phase == 0 ? 0.0 : face.startHeight * (1.0 - edges[i].speed / speed);
        r.overhang = overhang[i];
        r.framing = doc.roofDefaults.framing;
        r.hasCeiling = doc.roofDefaults.ceiling;
        r.rafterMemberId = doc.roofDefaults.rafterMemberId;
        r.rafterSpacing = doc.roofDefaults.rafterSpacing;
        r.heelHeight = doc.roofDefaults.heelHeight;
        r.sheathingThickness = doc.roofDefaults.sheathingThickness;
        r.sheathingMaterial = doc.roofDefaults.sheathingMaterial;
        r.highBearing = false;
        r.automatic = true;
        // Carry the face out to the edge of the roof: its corners on the
        // walls go to the overhang corners, and its edges up gables move out
        // by their rake overhang. Where a rake starts or ends, the edge
        // arriving at it (a ridge, a hip, a break) runs on out to the rake.
        const size_t m = poly.size();
        for (size_t k = 0; k < m; ++k) {
            const Vec2 p = poly[k];
            Vec2 q = p;
            bool corner = false;
            for (size_t j = 0; j < n && face.phase == 0; ++j)
                if (distance(p, outline[j]) < 1e-3) {
                    q = eaveCorner[j];
                    corner = true;
                    break;
                }
            if (!corner)
                for (const auto& v : verticals) {
                    if (!onVertical(p, v) || (v.edge == i && face.phase < v.phase)) continue;
                    const Vec2 out = -edgeDir(v.edge).perp() * v.overhang;
                    const Vec2 prev = poly[(k + m - 1) % m], next = poly[(k + 1) % m];
                    const bool prevOn = onVertical(prev, v), nextOn = onVertical(next, v);
                    if (prevOn != nextOn) {
                        const Vec2 from = prevOn ? next : prev;
                        q = intersect(p, p - from, p + out, edgeDir(v.edge)).value_or(q + out);
                    } else {
                        q = q + out;
                    }
                }
            r.outline.push_back(q);
        }
        // Drop points left in a straight line with their neighbors.
        for (size_t k = 0; k < r.outline.size() && r.outline.size() > 3;) {
            const size_t c = r.outline.size();
            const Vec2 prev = r.outline[(k + c - 1) % c], p = r.outline[k], next = r.outline[(k + 1) % c];
            if (distance(prev, p) < 1e-6 || std::abs(cross((p - prev).normalized(), (next - p).normalized())) < 1e-9)
                r.outline.erase(r.outline.begin() + static_cast<long>(k));
            else
                ++k;
        }
        // A shed's high side bears on its wall.
        for (size_t k = 0; k < m && !r.highBearing && face.phase == 0; ++k) {
            const Vec2 p = poly[k], q = poly[(k + 1) % m];
            if (distance(p, q) < 1.0) continue;
            for (size_t g = 0; g < n; ++g)
                if (g != i && walls[g]->roof.kind == WallRoof::HighShed && onEdge(p, g) && onEdge(q, g))
                    r.highBearing = true;
        }
        planes.push_back(r);
    }
    return planes;
}

int buildRoof(Document& doc, ObjectId levelId, bool keepSettings) {
    auto planes = buildRoofPlanes(doc, levelId);
    if (planes.empty()) return 0;
    std::vector<RoofPlane> old;
    for (const auto& r : doc.roofPlanes)
        if (r.automatic && r.levelId == levelId) old.push_back(r);
    std::erase_if(doc.roofPlanes, [&](const RoofPlane& r) { return r.automatic && r.levelId == levelId; });
    // Each new plane takes the id of the old one over the same wall, or the
    // next one left over, and with it the settings it was given.
    std::vector<bool> used(old.size(), false);
    std::vector<int> from(planes.size(), -1);
    for (size_t k = 0; k < planes.size(); ++k)
        for (size_t j = 0; j < old.size(); ++j)
            if (!used[j] && distance(old[j].a, planes[k].a) < 1e-6 && distance(old[j].b, planes[k].b) < 1e-6 &&
                std::abs(old[j].baseHeight - planes[k].baseHeight) < 1e-6) {
                used[j] = true;
                from[k] = static_cast<int>(j);
                break;
            }
    size_t next = 0;
    for (size_t k = 0; k < planes.size(); ++k) {
        if (from[k] < 0) {
            while (next < old.size() && used[next]) ++next;
            if (next < old.size()) {
                used[next] = true;
                from[k] = static_cast<int>(next);
            }
        }
        RoofPlane& r = planes[k];
        if (from[k] >= 0) {
            const RoofPlane& was = old[static_cast<size_t>(from[k])];
            r.id = was.id;
            if (keepSettings) {
                // Pitch and overhang come from the walls; the rest stays as set on the plane.
                const double pitch = r.pitch, overhang = r.overhang;
                static_cast<RoofSettings&>(r) = was;
                r.pitch = pitch;
                r.overhang = overhang;
            }
        } else {
            r.id = doc.newId();
        }
    }
    // Keep the order planes were in, new ones after.
    std::stable_sort(planes.begin(), planes.end(), [&](const RoofPlane& x, const RoofPlane& y) {
        auto rank = [&](const RoofPlane& r) {
            for (size_t j = 0; j < old.size(); ++j)
                if (old[j].id == r.id) return j;
            return old.size();
        };
        return rank(x) < rank(y);
    });
    for (auto& r : planes) doc.roofPlanes.push_back(r);
    return static_cast<int>(planes.size());
}

void rebuildAutomaticRoofs(Document& doc) {
    std::set<ObjectId> levels;
    for (const auto& r : doc.roofPlanes)
        if (r.automatic) levels.insert(r.levelId);
    for (ObjectId l : levels) buildRoof(doc, l, true);
}

bool roofEditedByHand(const Document& before, const Document& after) {
    if (!after.roofDefaults.autoRebuild) return false;
    const auto was = automaticPlanes(before, kNoId), now = automaticPlanes(after, kNoId);
    auto same = [](const std::vector<RoofPlane>& x, const std::vector<RoofPlane>& y) {
        if (x.size() != y.size()) return false;
        for (size_t i = 0; i < x.size(); ++i)
            if (!samePlane(x[i], y[i])) return false;
        return true;
    };
    if (was.size() == now.size()) {
        bool unchanged = true;
        for (size_t i = 0; i < was.size() && unchanged; ++i)
            unchanged = was[i].id == now[i].id && samePlane(was[i], now[i]);
        if (unchanged) return false;
    }
    // Planes as Build Roof makes them, as when it just ran, are not a hand edit.
    std::set<ObjectId> levels;
    for (const auto& r : was) levels.insert(r.levelId);
    for (const auto& r : now) levels.insert(r.levelId);
    for (ObjectId l : levels)
        if (!same(automaticPlanes(after, l), buildRoofPlanes(after, l))) return true;
    return false;
}

} // namespace oa
