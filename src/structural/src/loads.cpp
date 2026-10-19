#include "oa/structural/loads.h"

#include <algorithm>
#include <cmath>

namespace oa::structural {

const char* shortName(LoadCase c) {
    switch (c) {
    case LoadCase::Dead: return "D";
    case LoadCase::Live: return "L";
    case LoadCase::RoofLive: return "Lr";
    case LoadCase::Snow: return "S";
    }
    return "?";
}

const char* toString(LoadOrigin o) {
    switch (o) {
    case LoadOrigin::Roof: return "Roof";
    case LoadOrigin::Ceiling: return "Ceiling and attic";
    case LoadOrigin::Floor: return "Floor";
    case LoadOrigin::Wall: return "Walls above";
    case LoadOrigin::Beam: return "Beams and posts";
    case LoadOrigin::Other: return "Other";
    }
    return "Other";
}

CaseLoads CaseLoads::operator+(const CaseLoads& o) const {
    CaseLoads r = *this;
    return r += o;
}
CaseLoads CaseLoads::operator-(const CaseLoads& o) const {
    CaseLoads r;
    for (int i = 0; i < kCaseCount; ++i) r.v[i] = v[i] - o.v[i];
    return r;
}
CaseLoads CaseLoads::operator*(double s) const {
    CaseLoads r;
    for (int i = 0; i < kCaseCount; ++i) r.v[i] = v[i] * s;
    return r;
}
CaseLoads& CaseLoads::operator+=(const CaseLoads& o) {
    for (int i = 0; i < kCaseCount; ++i) v[i] += o.v[i];
    return *this;
}
bool CaseLoads::isZero() const {
    return std::all_of(v.begin(), v.end(), [](double x) { return std::abs(x) < 1e-9; });
}

double Combination::apply(const CaseLoads& c) const {
    double sum = 0.0;
    for (int i = 0; i < kCaseCount; ++i) sum += factors[i] * c.v[i];
    return sum;
}

double Combination::applyLiveOnly(const CaseLoads& c) const {
    return apply(c) - factors[0] * c.v[0];
}

const std::vector<Combination>& asdGravityCombinations() {
    // Factors in case order D, L, Lr, S.
    static const std::vector<Combination> combos = {
        {"D", {1.0, 0.0, 0.0, 0.0}, 0.9},
        {"D + L", {1.0, 1.0, 0.0, 0.0}, 1.0},
        {"D + Lr", {1.0, 0.0, 1.0, 0.0}, 1.25},
        {"D + S", {1.0, 0.0, 0.0, 1.0}, 1.15},
        {"D + 0.75L + 0.75Lr", {1.0, 0.75, 0.75, 0.0}, 1.25},
        {"D + 0.75L + 0.75S", {1.0, 0.75, 0.0, 0.75}, 1.15},
    };
    return combos;
}

CaseLoads LineLoad::at(double s) const {
    if (s < start - 1e-9 || s > end + 1e-9 || end - start < 1e-9) return {};
    const double t = (s - start) / (end - start);
    return wStart * (1.0 - t) + wEnd * t;
}

LineLoad LineLoad::clipped(double a, double b) const {
    LineLoad r = *this;
    const double lo = std::max(a, start);
    const double hi = std::min(b, end);
    if (hi <= lo) {
        r.start = r.end = lo;
        r.wStart = r.wEnd = {};
        return r;
    }
    r.wStart = at(lo);
    r.wEnd = at(hi);
    r.start = lo;
    r.end = hi;
    return r;
}

CaseLoads LineLoad::total() const {
    return (wStart + wEnd) * (0.5 * length() / 12.0);
}

LineLoadSummary summarize(const std::vector<LineLoad>& loads) {
    // Line loads are piecewise linear, so peaks occur at segment ends.
    std::vector<double> stations;
    for (const auto& l : loads) {
        stations.push_back(l.start);
        stations.push_back(l.end);
    }
    LineLoadSummary out;
    auto sumAt = [&loads](double s, bool fromLeft) {
        CaseLoads c;
        const double eps = 1e-6;
        for (const auto& l : loads) {
            // Evaluate just inside the load so step changes are captured on both sides.
            const double probe = fromLeft ? s - eps : s + eps;
            if (probe >= l.start && probe <= l.end) c += l.at(probe);
        }
        return c;
    };
    for (double s : stations) {
        for (bool side : {true, false}) {
            const CaseLoads c = sumAt(s, side);
            for (int i = 0; i < kCaseCount; ++i) out.peakPlf.v[i] = std::max(out.peakPlf.v[i], c.v[i]);
            for (const auto& combo : asdGravityCombinations()) {
                const double w = combo.apply(c);
                if (w > out.governingPlf) {
                    out.governingPlf = w;
                    out.governingCombo = combo.name;
                }
            }
        }
    }
    return out;
}

std::vector<OriginSummary> summarizeByOrigin(const std::vector<LineLoad>& loads) {
    std::vector<OriginSummary> out;
    for (int o = 0; o < kOriginCount; ++o) {
        std::vector<LineLoad> group;
        for (const auto& l : loads)
            if (static_cast<int>(l.origin) == o) group.push_back(l);
        if (!group.empty()) out.push_back({static_cast<LoadOrigin>(o), summarize(group)});
    }
    return out;
}

} // namespace oa::structural
