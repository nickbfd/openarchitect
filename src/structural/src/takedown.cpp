#include "oa/structural/takedown.h"

#include "oa/core/units.h"
#include "oa/structural/members.h"
#include "oa/structural/snow.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <sstream>

namespace oa::structural {
namespace {

constexpr double kRoofLivePsf = 20.0;       // IRC R301.6
constexpr double kRoofEdgeTolerance = 8.0;  // footprint edge to wall centerline, inches
constexpr double kAreaEdgeTolerance = 8.0;  // floor area edge to support, inches
constexpr double kBearingTolerance = 3.0;   // beam end to supporting wall face, inches

std::string feetText(double inches) {
    std::ostringstream ss;
    ss.setf(std::ios::fixed);
    ss.precision(1);
    ss << toFeet(inches) << " ft";
    return ss.str();
}

struct Interval {
    double lo, hi;
};

// Parts of [lo, hi] not covered by `covered`.
std::vector<Interval> uncovered(double lo, double hi, std::vector<Interval> covered) {
    std::sort(covered.begin(), covered.end(), [](auto& a, auto& b) { return a.lo < b.lo; });
    std::vector<Interval> out;
    double cursor = lo;
    for (const auto& c : covered) {
        if (c.hi <= cursor) continue;
        if (c.lo >= hi) break;
        if (c.lo > cursor + 1e-6) out.push_back({cursor, std::min(c.lo, hi)});
        cursor = std::max(cursor, c.hi);
    }
    if (cursor < hi - 1e-6) out.push_back({cursor, hi});
    return out;
}

class Takedown {
public:
    explicit Takedown(const Document& doc) : doc_(doc) {}

    TakedownResult run() {
        createSupports();
        for (const auto& roof : doc_.roofs) applyRoof(roof);
        for (const auto& p : doc_.platforms) applyPlatform(p);

        auto levels = doc_.levelsByElevation();
        for (auto it = levels.rbegin(); it != levels.rend(); ++it) {
            processBeams((*it)->id);
            processWalls((*it)->id);
        }
        return std::move(result_);
    }

private:
    const Document& doc_;
    TakedownResult result_;

    std::vector<SupportLoads>& supports() { return result_.supports; }
    void warn(std::string text) { result_.warnings.push_back(std::move(text)); }

    std::string levelName(ObjectId levelId) const {
        const Level* l = doc_.level(levelId);
        return l ? l->name : "unknown level";
    }

    void createSupports() {
        for (const auto& w : doc_.walls) {
            if (!w.bearing || w.length() < kEps) continue;
            SupportLoads s;
            s.kind = SupportKind::Wall;
            s.objectId = w.id;
            s.levelId = w.levelId;
            s.label = "Wall " + std::to_string(w.id) + " (" + levelName(w.levelId) + ")";
            s.line = w.centerline();
            supports().push_back(std::move(s));
        }
        for (const auto& b : doc_.beams) {
            if (b.length() < kEps) continue;
            SupportLoads s;
            s.kind = SupportKind::Beam;
            s.objectId = b.id;
            s.levelId = b.levelId;
            s.label = "Beam " + std::to_string(b.id) + " (" + levelName(b.levelId) + ")";
            s.line = b.centerline();
            supports().push_back(std::move(s));
        }
    }

    // Indices of supports on a level, optionally walls or beams only.
    std::vector<size_t> supportsOn(ObjectId levelId) {
        std::vector<size_t> out;
        for (size_t i = 0; i < supports().size(); ++i)
            if (supports()[i].levelId == levelId) out.push_back(i);
        return out;
    }

    double wallThickness(const SupportLoads& s) const {
        if (s.kind == SupportKind::Wall)
            if (const Wall* w = doc_.wall(s.objectId)) return w->thickness;
        return 3.5;
    }

    // Moves loads given along `from` onto the parallel supports `targets`
    // that overlap it. Returns the parts of `from` that found no support.
    std::vector<Interval> transfer(const Segment& from, const std::vector<LineLoad>& lines,
                                   const std::vector<PointLoad>& points, const std::vector<size_t>& targets,
                                   double tolerance, const std::string& source) {
        const double len = from.length();
        const Vec2 u = from.direction();
        std::vector<Interval> covered;
        std::vector<PointLoad> pending = points;

        for (size_t idx : targets) {
            SupportLoads& t = supports()[idx];
            auto ov = collinearOverlap(from, t.line, tolerance + wallThickness(t) / 2.0);
            if (!ov) continue;
            for (const auto& piece : uncovered(ov->start, ov->end, covered)) {
                const Vec2 tu = t.line.direction();
                auto map = [&](double s) { return dot(from.a + u * s - t.line.a, tu); };
                for (const auto& l : lines) {
                    LineLoad c = l.clipped(piece.lo, piece.hi);
                    if (c.length() < 1e-6) continue;
                    double s0 = map(c.start), s1 = map(c.end);
                    if (s0 > s1) {
                        std::swap(s0, s1);
                        std::swap(c.wStart, c.wEnd);
                    }
                    c.start = s0;
                    c.end = s1;
                    c.source = l.source.empty() ? source : l.source;
                    t.lineLoads.push_back(std::move(c));
                }
                for (auto it = pending.begin(); it != pending.end();) {
                    if (it->position >= piece.lo - 1e-6 && it->position <= piece.hi + 1e-6) {
                        PointLoad p = *it;
                        p.position = map(it->position);
                        t.pointLoads.push_back(std::move(p));
                        it = pending.erase(it);
                    } else {
                        ++it;
                    }
                }
                covered.push_back(piece);
            }
        }
        for (const auto& p : pending) {
            if (!placePointLoad(from.a + u * p.position, p, targets))
                warn(source + ": a point load of " + std::to_string(static_cast<long>(p.p[LoadCase::Dead] +
                     p.p[LoadCase::Live] + p.p[LoadCase::Snow])) + " lb has no support beneath it.");
        }
        auto gaps = uncovered(0.0, len, covered);
        // Ignore slivers smaller than a stud.
        std::erase_if(gaps, [](const Interval& g) { return g.hi - g.lo < 1.5; });
        return gaps;
    }

    // Puts a point load on whichever target passes under `where`.
    bool placePointLoad(Vec2 where, PointLoad p, const std::vector<size_t>& targets) {
        for (size_t idx : targets) {
            SupportLoads& t = supports()[idx];
            if (t.line.distanceTo(where) <= wallThickness(t) / 2.0 + kBearingTolerance) {
                p.position = std::clamp(t.line.project(where), 0.0, 1.0) * t.length();
                t.pointLoads.push_back(std::move(p));
                return true;
            }
        }
        return false;
    }

    // ---- Roofs -----------------------------------------------------------

    void applyRoof(const Roof& roof) {
        const Rect& fp = roof.footprint;
        if (fp.width() < 1.0 || fp.height() < 1.0) return;

        const double theta = roof.slopeRadians();
        const CaseLoads psf = CaseLoads::of(roof.deadPsf / std::cos(theta), 0.0, kRoofLivePsf,
                                            roofSnowPsf(doc_.site, theta));
        const std::string name = "Roof " + std::to_string(roof.id);
        const auto targets = supportsOn(roof.levelId);

        // Footprint edges, counter-clockwise from the lower left corner.
        const Vec2 c0 = fp.min, c1{fp.max.x, fp.min.y}, c2 = fp.max, c3{fp.min.x, fp.max.y};
        const Segment edges[4] = {{c0, c1}, {c1, c2}, {c2, c3}, {c3, c0}};
        auto isEave = [&](int i) {
            const bool alongX = i == 0 || i == 2;
            return (roof.ridgeAxis == Axis::X) == alongX;
        };

        const double width = roof.ridgeAxis == Axis::X ? fp.height() : fp.width(); // across the ridge
        const double run = width / 2.0;
        const double o = roof.overhang;

        if (roof.style == RoofStyle::Gable) {
            double eaveDepth = run + o;
            if (roof.structuralRidge) {
                // Rafters span from the wall to the ridge beam with an overhang.
                eaveDepth = (run + o) * (run + o) / (2.0 * run);
                applyRidgeBeam(roof, psf, (run * run - o * o) / run);
            }
            for (int i = 0; i < 4; ++i) {
                if (!isEave(i)) continue;
                const double len = edges[i].length();
                const CaseLoads w = psf * (eaveDepth / 12.0);
                applyEdge(edges[i], {{0.0, len, w, w, name}}, targets, name);
            }
        } else {
            if (roof.structuralRidge)
                warn(name + ": a structural ridge on a hip roof is not modeled yet; loads go to the walls.");
            for (int i = 0; i < 4; ++i) {
                const double len = edges[i].length();
                const double across = (i == 0 || i == 2) ? fp.height() : fp.width();
                const double half = across / 2.0;
                std::vector<LineLoad> profile;
                auto add = [&](double s0, double d0, double s1, double d1) {
                    if (s1 - s0 > 1e-6) profile.push_back({s0, s1, psf * (d0 / 12.0), psf * (d1 / 12.0), name});
                };
                // Straight skeleton of a rectangle: depth = min(s, len - s, across / 2).
                if (len / 2.0 <= half) {
                    add(0.0, 0.0, len / 2.0, len / 2.0);
                    add(len / 2.0, len / 2.0, len, 0.0);
                } else {
                    add(0.0, 0.0, half, half);
                    add(half, half, len - half, half);
                    add(len - half, half, len, 0.0);
                }
                add(0.0, o, len, o); // overhang strip
                applyEdge(edges[i], profile, targets, name);
            }
        }

        if (roof.hasCeiling) {
            const Axis joists = otherAxis(roof.ridgeAxis);
            applyAreaLoad(fp, joists, CaseLoads::of(roof.ceilingDeadPsf, roof.atticLivePsf), roof.levelId,
                          name + " ceiling");
        }
    }

    void applyEdge(const Segment& edge, const std::vector<LineLoad>& profile, const std::vector<size_t>& targets,
                   const std::string& name) {
        std::vector<size_t> walls;
        for (size_t i : targets)
            if (supports()[i].kind != SupportKind::RidgeBeam) walls.push_back(i);
        for (const auto& gap : transfer(edge, profile, {}, walls, kRoofEdgeTolerance, name))
            warn(name + ": " + feetText(gap.hi - gap.lo) + " of eave has no bearing wall or beam beneath it.");
    }

    void applyRidgeBeam(const Roof& roof, const CaseLoads& psf, double tributary) {
        const Rect& fp = roof.footprint;
        SupportLoads s;
        s.kind = SupportKind::RidgeBeam;
        s.objectId = roof.id;
        s.levelId = roof.levelId;
        s.label = "Ridge beam (Roof " + std::to_string(roof.id) + ")";
        const Vec2 c = fp.center();
        s.line = roof.ridgeAxis == Axis::X ? Segment{{fp.min.x, c.y}, {fp.max.x, c.y}}
                                           : Segment{{c.x, fp.min.y}, {c.x, fp.max.y}};
        const CaseLoads w = psf * (tributary / 12.0);
        s.lineLoads.push_back({0.0, s.length(), w, w, "Roof " + std::to_string(roof.id)});
        supports().push_back(std::move(s));
    }

    // ---- Floors and ceilings -----------------------------------------------

    void applyPlatform(const FloorPlatform& p) {
        const Level* below = doc_.levelBelow(p.levelId);
        if (!below) return; // the lowest floor bears on the foundation or slab
        applyAreaLoad(p.area, p.joistAxis, CaseLoads::of(p.deadPsf, p.livePsf), below->id,
                      "Floor " + std::to_string(p.id) + " (" + levelName(p.levelId) + ")");
    }

    // One-way framing over a rectangle. Joists run along `joistAxis`; the
    // supports are walls and beams on `supportLevel` running across them.
    void applyAreaLoad(const Rect& area, Axis joistAxis, const CaseLoads& psf, ObjectId supportLevel,
                       const std::string& name) {
        const bool joistsAlongY = joistAxis == Axis::Y;
        auto spanCoord = [&](Vec2 p) { return joistsAlongY ? p.y : p.x; };
        auto crossCoord = [&](Vec2 p) { return joistsAlongY ? p.x : p.y; };
        const double spanMin = joistsAlongY ? area.min.y : area.min.x;
        const double spanMax = joistsAlongY ? area.max.y : area.max.x;
        const double crossMin = joistsAlongY ? area.min.x : area.min.y;
        const double crossMax = joistsAlongY ? area.max.x : area.max.y;

        struct Candidate {
            size_t index;
            double at;         // span coordinate of the support line
            double lo, hi;     // cross extent, clipped to the area
        };
        std::vector<Candidate> candidates;
        for (size_t i : supportsOn(supportLevel)) {
            const SupportLoads& s = supports()[i];
            if (s.kind == SupportKind::RidgeBeam) continue;
            const Vec2 d = s.line.direction();
            const double alongCross = std::abs(joistsAlongY ? d.x : d.y);
            if (alongCross < 0.9999) continue; // not perpendicular to the joists
            const double at = spanCoord((s.line.a + s.line.b) * 0.5);
            if (at < spanMin - kAreaEdgeTolerance || at > spanMax + kAreaEdgeTolerance) continue;
            double lo = std::min(crossCoord(s.line.a), crossCoord(s.line.b));
            double hi = std::max(crossCoord(s.line.a), crossCoord(s.line.b));
            lo = std::max(lo, crossMin);
            hi = std::min(hi, crossMax);
            if (hi - lo < 1.0) continue;
            candidates.push_back({i, at, lo, hi});
        }

        std::vector<double> breaks = {crossMin, crossMax};
        for (const auto& c : candidates) {
            breaks.push_back(c.lo);
            breaks.push_back(c.hi);
        }
        std::sort(breaks.begin(), breaks.end());
        breaks.erase(std::unique(breaks.begin(), breaks.end(),
                                 [](double a, double b) { return std::abs(a - b) < 0.5; }),
                     breaks.end());

        double unsupported = 0.0;
        for (size_t k = 0; k + 1 < breaks.size(); ++k) {
            const double x0 = breaks[k], x1 = breaks[k + 1];
            if (x1 - x0 < 0.5) continue;
            std::vector<const Candidate*> row;
            for (const auto& c : candidates)
                if (c.lo <= x0 + 0.25 && c.hi >= x1 - 0.25) row.push_back(&c);
            std::sort(row.begin(), row.end(), [](auto* a, auto* b) { return a->at < b->at; });
            if (row.empty()) {
                unsupported += x1 - x0;
                continue;
            }
            if (row.size() == 1 && row.front()->at > spanMin + kAreaEdgeTolerance &&
                row.front()->at < spanMax - kAreaEdgeTolerance)
                warn(name + ": only one support across part of the framing; it would act as a cantilever.");

            for (size_t i = 0; i < row.size(); ++i) {
                const double at = row[i]->at;
                const double left = i == 0 ? std::max(0.0, at - spanMin) : (at - row[i - 1]->at) / 2.0;
                const double right = i + 1 == row.size() ? std::max(0.0, spanMax - at) : (row[i + 1]->at - at) / 2.0;
                const CaseLoads w = psf * ((left + right) / 12.0);
                SupportLoads& s = supports()[row[i]->index];
                // Map the strip's cross coordinates onto the support's own axis.
                auto toS = [&](double cross) {
                    const Vec2 p = joistsAlongY ? Vec2{cross, at} : Vec2{at, cross};
                    return dot(p - s.line.a, s.line.direction());
                };
                double s0 = toS(x0), s1 = toS(x1);
                if (s0 > s1) std::swap(s0, s1);
                s.lineLoads.push_back({s0, s1, w, w, name});
            }
        }
        if (unsupported > 1.5)
            warn(name + ": " + feetText(unsupported) + " of framing has no bearing wall or beam beneath it.");
    }

    // ---- Beams and walls, top down ------------------------------------------

    struct EndSupport {
        Vec2 point;
        long target = -1; // support index on the same level, or -1
    };

    void processBeams(ObjectId levelId) {
        std::vector<size_t> beams, walls;
        for (size_t i : supportsOn(levelId))
            (supports()[i].kind == SupportKind::Wall ? walls : beams).push_back(i);
        if (beams.empty()) return;

        // Where each beam end bears: a wall or another beam on the same level.
        std::map<size_t, std::array<EndSupport, 2>> ends;
        for (size_t b : beams) {
            const Segment line = supports()[b].line;
            std::array<EndSupport, 2> e{EndSupport{line.a}, EndSupport{line.b}};
            for (auto& end : e) {
                for (size_t w : walls) {
                    if (supports()[w].line.distanceTo(end.point) <= wallThickness(supports()[w]) / 2.0 + kBearingTolerance) {
                        end.target = static_cast<long>(w);
                        break;
                    }
                }
                if (end.target >= 0) continue;
                for (size_t other : beams) {
                    if (other == b) continue;
                    const Segment& ol = supports()[other].line;
                    const double t = ol.project(end.point);
                    if (t > 0.01 && t < 0.99 && ol.distanceTo(end.point) <= kBearingTolerance) {
                        end.target = static_cast<long>(other);
                        break;
                    }
                }
            }
            ends[b] = e;
        }

        // A beam that lands on another must be solved first.
        std::map<size_t, int> carriedCount;
        for (size_t b : beams) carriedCount[b] = 0;
        for (auto& [b, e] : ends)
            for (auto& end : e)
                if (end.target >= 0 && carriedCount.count(static_cast<size_t>(end.target)))
                    ++carriedCount[static_cast<size_t>(end.target)];

        std::vector<size_t> ready;
        for (auto& [b, n] : carriedCount)
            if (n == 0) ready.push_back(b);
        size_t solved = 0;
        while (!ready.empty()) {
            const size_t b = ready.back();
            ready.pop_back();
            ++solved;
            solveBeam(b, ends[b], levelId);
            for (auto& end : ends[b]) {
                if (end.target < 0) continue;
                auto it = carriedCount.find(static_cast<size_t>(end.target));
                if (it != carriedCount.end() && --it->second == 0) ready.push_back(it->first);
            }
        }
        if (solved < beams.size())
            warn("Beams on " + levelName(levelId) + " bear on each other in a loop; some were not solved.");
    }

    void solveBeam(size_t index, const std::array<EndSupport, 2>& ends, ObjectId levelId) {
        SupportLoads& s = supports()[index];
        BeamLoads loads = beamLoads(s);
        if (s.kind == SupportKind::Beam)
            if (const Beam* beam = doc_.beam(s.objectId); beam && !beam->memberId.empty())
                if (const Member* m = findMember(beam->memberId)) {
                    const double w = m->selfWeightPlf();
                    loads.line.push_back({0.0, loads.span, CaseLoads::of(w), CaseLoads::of(w), "beam self weight"});
                }
        s.reactions = reactionsByCase(loads);
        const std::string label = s.label;

        const CaseLoads reaction[2] = {s.reactions.left, s.reactions.right};
        for (int i = 0; i < 2; ++i) {
            PointLoad p{0.0, reaction[i], label};
            if (ends[i].target >= 0) {
                SupportLoads& t = supports()[static_cast<size_t>(ends[i].target)];
                p.position = std::clamp(t.line.project(ends[i].point), 0.0, 1.0) * t.length();
                t.pointLoads.push_back(std::move(p));
                continue;
            }
            // No wall at this end: a post carries it to whatever is below.
            const Level* below = doc_.levelBelow(levelId);
            if (!below) continue; // post to the foundation
            if (!placePointLoad(ends[i].point, p, supportsOn(below->id)))
                warn(label + ": the post at one end has nothing beneath it on " + below->name + ".");
        }
    }

    void processWalls(ObjectId levelId) {
        const Level* below = doc_.levelBelow(levelId);
        std::vector<size_t> targets;
        if (below) targets = supportsOn(below->id);

        for (size_t i : supportsOn(levelId)) {
            if (supports()[i].kind != SupportKind::Wall) continue;
            const Wall* wall = doc_.wall(supports()[i].objectId);
            if (!wall) continue;
            {
                SupportLoads& s = supports()[i];
                const double plf = wall->selfWeightPsf * toFeet(doc_.wallHeight(*wall));
                s.selfWeight = {0.0, s.length(), CaseLoads::of(plf), CaseLoads::of(plf), "wall self weight"};
            }
            if (!below) continue; // bears on the foundation

            // Copy first: transfer() may grow the supports vector's contents.
            const Segment line = supports()[i].line;
            std::vector<LineLoad> lines = supports()[i].lineLoads;
            lines.push_back(supports()[i].selfWeight);
            const std::vector<PointLoad> points = supports()[i].pointLoads;
            const std::string label = supports()[i].label;

            for (const auto& gap : transfer(line, lines, points, targets, 0.0, label)) {
                bool loaded = false;
                for (const auto& l : supports()[i].lineLoads)
                    if (l.clipped(gap.lo, gap.hi).length() > 0.0) loaded = true;
                warn(label + ": " + feetText(gap.hi - gap.lo) + " of wall has no bearing wall or beam beneath it on " +
                     below->name + (loaded ? " and it carries load from above." : "."));
            }
        }
    }
};

} // namespace

bool SupportLoads::carriesRoofOnly() const {
    for (const auto& l : lineLoads)
        if (l.wStart[LoadCase::Live] > 0.0 || l.wEnd[LoadCase::Live] > 0.0) return false;
    for (const auto& p : pointLoads)
        if (p.p[LoadCase::Live] > 0.0) return false;
    return true;
}

const SupportLoads* TakedownResult::find(SupportKind kind, ObjectId objectId) const {
    for (const auto& s : supports)
        if (s.kind == kind && s.objectId == objectId) return &s;
    return nullptr;
}

TakedownResult runTakedown(const Document& doc) { return Takedown(doc).run(); }

BeamLoads beamLoads(const SupportLoads& support) {
    BeamLoads b;
    b.span = support.length();
    b.line = support.lineLoads;
    b.point = support.pointLoads;
    return b;
}

BeamLoads headerLoads(const Document& doc, const TakedownResult& result, ObjectId openingId) {
    BeamLoads b;
    const Opening* o = doc.opening(openingId);
    if (!o) return b;
    const Wall* wall = doc.wall(o->wallId);
    if (!wall) return b;

    constexpr double kBearing = 1.5;
    b.span = o->width + kBearing * 2.0;
    const double start = o->offset - b.span / 2.0;
    const double end = o->offset + b.span / 2.0;

    if (const SupportLoads* s = result.wall(wall->id)) {
        for (const auto& l : s->lineLoads) {
            LineLoad c = l.clipped(start, end);
            if (c.length() < 1e-6) continue;
            c.start -= start;
            c.end -= start;
            b.line.push_back(std::move(c));
        }
        for (const auto& p : s->pointLoads) {
            if (p.position < start || p.position > end) continue;
            PointLoad q = p;
            q.position -= start;
            b.point.push_back(std::move(q));
        }
    }

    const double above = doc.wallHeight(*wall) - (o->sillHeight + o->height);
    if (above > 0.0) {
        const double plf = wall->selfWeightPsf * toFeet(above);
        b.line.push_back({0.0, b.span, CaseLoads::of(plf), CaseLoads::of(plf), "wall above header"});
    }
    return b;
}

DesignCriteria criteriaFor(const BeamLoads& loads) {
    for (const auto& l : loads.line)
        if (l.wStart[LoadCase::Live] > 0.0 || l.wEnd[LoadCase::Live] > 0.0) return DesignCriteria::floorMember();
    for (const auto& p : loads.point)
        if (p.p[LoadCase::Live] > 0.0) return DesignCriteria::floorMember();
    return DesignCriteria::roofMember();
}

} // namespace oa::structural
