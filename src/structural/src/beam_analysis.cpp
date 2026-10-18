#include "oa/structural/beam_analysis.h"

#include <algorithm>
#include <cmath>

namespace oa::structural {
namespace {

struct Linear {
    double x1, x2, w1, w2; // inches, lb/in
};

// Load and first moment about x = 0 of the part of a linear load left of x.
void partLeftOf(const Linear& l, double x, double& force, double& centroid) {
    force = 0.0;
    centroid = l.x1;
    const double hi = std::min(x, l.x2);
    if (hi <= l.x1) return;
    const double t = (l.x2 > l.x1) ? (hi - l.x1) / (l.x2 - l.x1) : 1.0;
    const double wHi = l.w1 + (l.w2 - l.w1) * t;
    const double len = hi - l.x1;
    force = 0.5 * (l.w1 + wHi) * len;
    const double sum = l.w1 + wHi;
    centroid = std::abs(sum) > 1e-12 ? l.x1 + len * (l.w1 + 2.0 * wHi) / (3.0 * sum) : l.x1 + len / 2.0;
}

} // namespace

CaseLoads BeamLoads::totalLoad() const {
    CaseLoads t;
    for (const auto& l : line) t += l.total();
    for (const auto& p : point) t += p.p;
    return t;
}

SpanResponse analyzeSimpleSpan(const BeamLoads& loads, const std::array<double, kCaseCount>& factors,
                               double shearCheckDistance, int stations) {
    SpanResponse r;
    const double L = loads.span;
    if (L <= 0.0) return r;

    CaseLoads f;
    f.v = factors;
    auto combine = [&f](const CaseLoads& c) {
        double s = 0.0;
        for (int i = 0; i < kCaseCount; ++i) s += f.v[i] * c.v[i];
        return s;
    };

    std::vector<Linear> lines;
    for (const auto& l : loads.line) {
        const double a = std::clamp(l.start, 0.0, L), b = std::clamp(l.end, 0.0, L);
        if (b - a < 1e-9) continue;
        const LineLoad c = l.clipped(a, b);
        lines.push_back({a, b, combine(c.wStart) / 12.0, combine(c.wEnd) / 12.0});
    }
    std::vector<std::pair<double, double>> points; // position, lb
    for (const auto& p : loads.point)
        points.emplace_back(std::clamp(p.position, 0.0, L), combine(p.p));

    // Reactions from equilibrium.
    double total = 0.0, momentAboutLeft = 0.0;
    for (const auto& l : lines) {
        double F, c;
        partLeftOf(l, L, F, c);
        total += F;
        momentAboutLeft += F * c;
    }
    for (auto [x, P] : points) {
        total += P;
        momentAboutLeft += P * x;
    }
    r.reactionRight = momentAboutLeft / L;
    r.reactionLeft = total - r.reactionRight;

    auto shearAt = [&](double x) {
        double V = r.reactionLeft;
        for (const auto& l : lines) {
            double F, c;
            partLeftOf(l, x, F, c);
            V -= F;
        }
        for (auto [xp, P] : points)
            if (xp < x) V -= P;
        return V;
    };
    auto momentAt = [&](double x) {
        double M = r.reactionLeft * x;
        for (const auto& l : lines) {
            double F, c;
            partLeftOf(l, x, F, c);
            M -= F * (x - c);
        }
        for (auto [xp, P] : points)
            if (xp < x) M -= P * (x - xp);
        return M;
    };

    const int n = std::max(stations, 10);
    const double dx = L / n;
    std::vector<double> M(n + 1);
    for (int i = 0; i <= n; ++i) {
        const double x = i * dx;
        M[i] = momentAt(x);
        r.maxMoment = std::max(r.maxMoment, std::abs(M[i]));
        // Shear just right and just left of the station catches point load steps.
        for (double probe : {x + 1e-7, x - 1e-7}) {
            if (probe < 0.0 || probe > L) continue;
            const double V = std::abs(shearAt(probe));
            r.maxShear = std::max(r.maxShear, V);
            if (probe >= shearCheckDistance && probe <= L - shearCheckDistance)
                r.maxShearBeyondD = std::max(r.maxShearBeyondD, V);
        }
    }
    if (shearCheckDistance * 2.0 >= L) r.maxShearBeyondD = r.maxShear;
    for (auto [xp, P] : points)
        r.maxMoment = std::max(r.maxMoment, std::abs(momentAt(xp)));

    // EI y'' = -M, y(0) = y(L) = 0, integrated with the trapezoid rule.
    std::vector<double> slope(n + 1, 0.0), y(n + 1, 0.0);
    for (int i = 1; i <= n; ++i) slope[i] = slope[i - 1] - 0.5 * (M[i - 1] + M[i]) * dx;
    for (int i = 1; i <= n; ++i) y[i] = y[i - 1] + 0.5 * (slope[i - 1] + slope[i]) * dx;
    for (int i = 0; i <= n; ++i) {
        const double corrected = y[i] - y[n] * (i * dx) / L;
        r.maxEIDeflection = std::max(r.maxEIDeflection, std::abs(corrected));
    }
    return r;
}

Reactions reactionsByCase(const BeamLoads& loads) {
    Reactions out;
    for (int i = 0; i < kCaseCount; ++i) {
        std::array<double, kCaseCount> f{};
        f[i] = 1.0;
        const SpanResponse r = analyzeSimpleSpan(loads, f, 0.0, 10);
        out.left.v[i] = r.reactionLeft;
        out.right.v[i] = r.reactionRight;
    }
    return out;
}

} // namespace oa::structural
