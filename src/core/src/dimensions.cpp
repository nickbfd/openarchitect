#include "oa/core/dimensions.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace oa {
namespace {

constexpr double kOnSide = 1.0;        // inches a wall end may be off the side line
constexpr double kRowSpacing = 24.0;   // between dimension strings
constexpr double kJoin = 1.0;          // wall ends this close are joined

bool isExterior(const Document& doc, const Wall& w) {
    if (w.roomDivider) return false;
    if (const WallType* t = doc.wallType(w.wallTypeId)) return t->kind == WallKind::Exterior;
    return w.bearing;
}

std::vector<double> unique(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end(), [](double a, double b) { return b - a < 0.5; }), v.end());
    return v;
}

// Whether p is inside the building, by counting the exterior wall
// centerlines a ray from p to the east crosses.
bool insideExterior(const Document& doc, ObjectId levelId, Vec2 p) {
    int crossings = 0;
    for (const auto& w : doc.walls) {
        if (w.levelId != levelId || !isExterior(doc, w)) continue;
        const Vec2 a = w.a, b = w.b;
        if ((a.y > p.y) == (b.y > p.y)) continue;
        const double x = a.x + (p.y - a.y) / (b.y - a.y) * (b.x - a.x);
        if (x > p.x) ++crossings;
    }
    return crossings % 2 == 1;
}

// Distance from the centerline toward the outside of the framing's
// outside face or middle. The studs sit toward the siding of exterior wall
// types, centered in other walls, and fill the whole thickness of walls set
// by hand.
double framingOffset(const Document& doc, const Wall& w, DimensionTo to) {
    const WallType* t = doc.wallType(w.wallTypeId);
    double depth = w.thickness, middle = 0.0;
    if (t) {
        depth = t->studDepth;
        if (t->kind == WallKind::Exterior) middle = w.thickness / 2.0 - t->siding - t->sheathing - depth / 2.0;
    }
    return to == DimensionTo::FramingCenter ? middle : middle + depth / 2.0;
}

std::optional<Vec2> lineIntersection(Vec2 p, Vec2 d, Vec2 q, Vec2 e) {
    const double den = cross(d, e);
    if (std::abs(den) < 1e-9) return std::nullopt;
    return p + d * (cross(q - p, e) / den);
}

} // namespace

Vec2 wallOutside(const Document& doc, const Wall& w) {
    const Vec2 dir = (w.b - w.a).normalized();
    const Vec2 right = dir.perp() * -1.0;
    const Vec2 mid = (w.a + w.b) * 0.5;
    const double probe = w.thickness / 2.0 + 3.0;
    const bool rightIn = insideExterior(doc, w.levelId, mid + right * probe);
    const bool leftIn = insideExterior(doc, w.levelId, mid - right * probe);
    if (rightIn && !leftIn) return right * -1.0;
    return right;
}

Segment wallMeasureLine(const Document& doc, const Wall& w, DimensionTo to) {
    if (!isExterior(doc, w) || w.length() < 1e-6) return w.centerline();
    const Vec2 out = wallOutside(doc, w);
    const Vec2 dir = (w.b - w.a).normalized();
    const double f = framingOffset(doc, w, to);
    Segment s{w.a + out * f, w.b + out * f};
    // Carry each end to the corner of the neighbor's measure line.
    for (const auto& v : doc.walls) {
        if (v.id == w.id || v.levelId != w.levelId || !isExterior(doc, v) || v.length() < 1e-6) continue;
        const bool atA = distance(v.a, w.a) <= kJoin || distance(v.b, w.a) <= kJoin;
        const bool atB = distance(v.a, w.b) <= kJoin || distance(v.b, w.b) <= kJoin;
        if (!atA && !atB) continue;
        const Vec2 vout = wallOutside(doc, v);
        const double vf = framingOffset(doc, v, to);
        const auto corner = lineIntersection(s.a, dir, v.a + vout * vf, (v.b - v.a).normalized());
        if (!corner) continue;
        if (atA) s.a = *corner;
        if (atB) s.b = *corner;
    }
    return s;
}

std::vector<Vec2> dimensionSnapPoints(const Document& doc, ObjectId levelId) {
    std::vector<Vec2> out;
    for (const auto& w : doc.walls) {
        if (w.levelId != levelId || w.roomDivider || w.length() < 1e-6) continue;
        const Segment m = wallMeasureLine(doc, w, doc.dimensionTo);
        out.push_back(m.a);
        out.push_back(m.b);
        const Vec2 dir = (w.b - w.a).normalized(), n = dir.perp() * (w.thickness / 2.0);
        for (Vec2 p : {w.a, w.b}) {
            out.push_back(p + n);
            out.push_back(p - n);
        }
        for (const Opening* o : doc.openingsIn(w.id))
            for (double s : {o->offset, o->offset - o->width / 2.0, o->offset + o->width / 2.0}) {
                const double t = s / w.length();
                out.push_back(m.a + (m.b - m.a) * t);
            }
    }
    for (const auto& p : doc.posts)
        if (p.levelId == levelId) out.push_back(p.position);
    for (const auto& b : doc.beams)
        if (b.levelId == levelId) {
            out.push_back(b.a);
            out.push_back(b.b);
        }
    return out;
}

std::vector<Dimension> exteriorDimensions(const Document& doc, ObjectId levelId) {
    struct Measured {
        const Wall* wall;
        Segment line;
    };
    std::vector<Measured> walls;
    Vec2 lo{1e30, 1e30}, hi{-1e30, -1e30};
    for (const auto& w : doc.walls) {
        if (w.levelId != levelId || !isExterior(doc, w) || w.length() < 1.0) continue;
        const Segment line = wallMeasureLine(doc, w, doc.dimensionTo);
        walls.push_back({&w, line});
        for (Vec2 p : {line.a, line.b}) {
            lo = {std::min(lo.x, p.x), std::min(lo.y, p.y)};
            hi = {std::max(hi.x, p.x), std::max(hi.y, p.y)};
        }
    }
    std::vector<Dimension> out;
    if (walls.empty() || hi.x - lo.x < 12.0 || hi.y - lo.y < 12.0) return out;

    struct Side {
        Vec2 origin, along, out; // side line from origin along `along`; `out` points away from the building
    };
    const Side sides[] = {
        {lo, {1, 0}, {0, -1}},
        {{hi.x, lo.y}, {0, 1}, {1, 0}},
        {{lo.x, hi.y}, {1, 0}, {0, 1}},
        {lo, {0, 1}, {-1, 0}},
    };
    for (const Side& side : sides) {
        auto across = [&](Vec2 p) { return dot(p - side.origin, side.out); };
        auto at = [&](Vec2 p) { return dot(p - side.origin, side.along); };
        // The building's extent along this side, then the corners and
        // openings of the walls standing on it.
        std::vector<double> ends, corners, openings;
        for (const auto& [w, line] : walls) {
            ends.push_back(at(line.a));
            ends.push_back(at(line.b));
            const bool aOn = std::abs(across(line.a)) <= kOnSide, bOn = std::abs(across(line.b)) <= kOnSide;
            if (aOn) corners.push_back(at(line.a));
            if (bOn) corners.push_back(at(line.b));
            if (!aOn || !bOn) continue;
            const Vec2 dir = (w->b - w->a).normalized();
            for (const Opening* o : doc.openingsIn(w->id)) openings.push_back(at(w->a + dir * o->offset));
        }
        if (corners.empty()) continue;
        const auto [mn, mx] = std::minmax_element(ends.begin(), ends.end());
        corners.push_back(*mn);
        corners.push_back(*mx);
        corners = unique(corners);
        std::vector<double> nearest = corners;
        nearest.insert(nearest.end(), openings.begin(), openings.end());
        nearest = unique(nearest);

        // Strings go past anything else on the level that sticks out on this
        // side, like a porch beam and its posts.
        double clear = 0.0;
        auto pushOut = [&](Vec2 p, double halfWidth) {
            const double s = at(p);
            if (s >= corners.front() - 24.0 && s <= corners.back() + 24.0)
                clear = std::max(clear, across(p) + halfWidth);
        };
        for (const auto& w : doc.walls)
            if (w.levelId == levelId)
                for (Vec2 p : {w.a, w.b}) pushOut(p, w.thickness / 2.0);
        for (const auto& b : doc.beams)
            if (b.levelId == levelId)
                for (Vec2 p : {b.a, b.b}) pushOut(p, 3.0);
        for (const auto& p : doc.posts)
            if (p.levelId == levelId) pushOut(p.position, 3.0);

        std::vector<std::vector<double>> rows;
        if (!openings.empty()) rows.push_back(nearest);
        if (corners.size() > 2) rows.push_back(corners);
        rows.push_back({corners.front(), corners.back()});

        for (size_t r = 0; r < rows.size(); ++r) {
            const double distanceOut = clear + kRowSpacing * static_cast<double>(r + 1);
            for (size_t k = 0; k + 1 < rows[r].size(); ++k) {
                Dimension d;
                d.levelId = levelId;
                d.a = side.origin + side.along * rows[r][k];
                d.b = side.origin + side.along * rows[r][k + 1];
                // Offsets are to the left of a to b; put the string outside.
                const double sign = dot(side.along.perp(), side.out) > 0 ? 1.0 : -1.0;
                d.offset = sign * distanceOut;
                d.automatic = true;
                out.push_back(d);
            }
        }
    }
    return out;
}

void setDimensionTo(Document& doc, DimensionTo to) {
    doc.dimensionTo = to;
    std::vector<ObjectId> levels;
    for (const auto& x : doc.dimensions)
        if (x.automatic && std::find(levels.begin(), levels.end(), x.levelId) == levels.end()) levels.push_back(x.levelId);
    for (ObjectId level : levels) {
        std::erase_if(doc.dimensions, [level](const Dimension& x) { return x.automatic && x.levelId == level; });
        for (Dimension x : exteriorDimensions(doc, level)) {
            x.id = doc.newId();
            doc.dimensions.push_back(x);
        }
    }
}

} // namespace oa
