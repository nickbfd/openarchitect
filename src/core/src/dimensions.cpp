#include "oa/core/dimensions.h"

#include <algorithm>
#include <cmath>

namespace oa {
namespace {

constexpr double kOnSide = 1.0;        // inches a wall end may be off the side line
constexpr double kRowSpacing = 24.0;   // between dimension strings

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

} // namespace

std::vector<Dimension> exteriorDimensions(const Document& doc, ObjectId levelId) {
    std::vector<const Wall*> walls;
    Vec2 lo{1e30, 1e30}, hi{-1e30, -1e30};
    for (const auto& w : doc.walls) {
        if (w.levelId != levelId || !isExterior(doc, w) || w.length() < 1.0) continue;
        walls.push_back(&w);
        for (Vec2 p : {w.a, w.b}) {
            lo = {std::min(lo.x, p.x), std::min(lo.y, p.y)};
            hi = {std::max(hi.x, p.x), std::max(hi.y, p.y)};
        }
    }
    std::vector<Dimension> out;
    if (walls.empty() || hi.x - lo.x < 12.0 || hi.y - lo.y < 12.0) return out;
    double outside = 0.0;
    for (const Wall* w : walls) outside = std::max(outside, w->thickness / 2.0);

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
        for (const Wall* w : walls) {
            ends.push_back(at(w->a));
            ends.push_back(at(w->b));
            const bool aOn = std::abs(across(w->a)) <= kOnSide, bOn = std::abs(across(w->b)) <= kOnSide;
            if (aOn) corners.push_back(at(w->a));
            if (bOn) corners.push_back(at(w->b));
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
        double clear = outside;
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

} // namespace oa
