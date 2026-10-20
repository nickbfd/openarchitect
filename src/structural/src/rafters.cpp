#include "oa/structural/rafters.h"

#include "oa/structural/snow.h"

#include <algorithm>
#include <cmath>

namespace oa::structural {
namespace {

constexpr double kRoofLivePsf = 20.0; // IRC R301.6

// The longest rafter in the plane, from the baseline (or where the plane
// starts above it) to its top.
double longestRun(const RoofPlane& r) {
    const Polygon& poly = r.outline;
    double t0 = 1e30, t1 = -1e30;
    for (Vec2 p : poly) {
        t0 = std::min(t0, r.alongFrom(p));
        t1 = std::max(t1, r.alongFrom(p));
    }
    double best = 0.0;
    for (double t = t0; t <= t1 + 1e-6; t += std::max((t1 - t0) / 200.0, 0.5)) {
        std::vector<double> cuts;
        for (size_t k = 0; k < poly.size(); ++k) {
            const Vec2 p = poly[k], q = poly[(k + 1) % poly.size()];
            const double tp = r.alongFrom(p), tq = r.alongFrom(q);
            if (std::abs(tq - tp) < 1e-9) continue;
            const double f = (t - tp) / (tq - tp);
            if (f < -1e-9 || f > 1 + 1e-9) continue;
            cuts.push_back(r.upFrom(p) + (r.upFrom(q) - r.upFrom(p)) * f);
        }
        if (cuts.size() < 2) continue;
        const auto [lo, hi] = std::minmax_element(cuts.begin(), cuts.end());
        best = std::max(best, *hi - std::max(*lo, 0.0));
    }
    return best;
}

} // namespace

RafterCheck checkRafters(const Document& doc, const RoofPlane& plane) {
    RafterCheck out;
    if (plane.framing == RoofFraming::Trusses) {
        out.note = "Trusses are designed by their manufacturer.";
        return out;
    }
    const Member* m = findMember(plane.rafterMemberId);
    if (!m) {
        out.note = "Pick a rafter size to check it.";
        return out;
    }
    out.span = longestRun(plane);
    if (out.span < 1.0) {
        out.note = "The plane has no rafters to check.";
        return out;
    }
    const double theta = plane.slopeRadians();
    const double feet = std::max(plane.rafterSpacing, 1.0) / 12.0;
    const CaseLoads w = CaseLoads::of(plane.deadPsf / std::cos(theta), 0.0, kRoofLivePsf, designSnowPsf(doc.site, plane)) * feet;
    BeamLoads loads;
    loads.span = out.span;
    loads.line.push_back({0.0, out.span, w, w, "Roof", LoadOrigin::Roof});
    out.result = checkMember(*m, loads, DesignCriteria::roofMember());
    out.plf = std::max(w[LoadCase::Dead] + std::max(w[LoadCase::RoofLive], w[LoadCase::Snow]), 0.0);
    out.checked = true;
    return out;
}

std::vector<const Member*> rafterChoices() {
    std::vector<const Member*> out;
    for (const auto& m : defaultCatalog())
        if (m.kind == MemberKind::Sawn && m.plies == 1 && m.id.find("-1-2x") != std::string::npos) out.push_back(&m);
    return out;
}

} // namespace oa::structural
