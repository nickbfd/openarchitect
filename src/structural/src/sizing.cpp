#include "oa/structural/sizing.h"

#include <algorithm>
#include <cmath>

namespace oa::structural {
namespace {

// Bending adjustment that depends on the member type, before CD.
double bendingFactors(const Member& m, double spanInches) {
    switch (m.kind) {
    case MemberKind::Sawn: {
        // Repetitive member factor for built-up beams of three or more plies.
        const double Cr = m.plies >= 3 ? 1.15 : 1.0;
        return m.sizeFactor * Cr;
    }
    case MemberKind::Timber:
        return m.sizeFactor;
    case MemberKind::Glulam: {
        // Volume factor, NDS 5.3.6, Douglas Fir (x = 10). CL = 1 so CV governs.
        const double Lft = std::max(spanInches / 12.0, 1.0);
        const double Cv = std::pow(21.0 / Lft, 0.1) * std::pow(12.0 / m.d, 0.1) * std::pow(5.125 / m.b, 0.1);
        return std::min(Cv, 1.0);
    }
    case MemberKind::Lvl:
        // Generic depth factor, never taken above 1.0.
        return std::min(std::pow(12.0 / m.d, 0.136), 1.0);
    }
    return 1.0;
}

} // namespace

MemberCheck checkMember(const Member& member, const BeamLoads& input, const DesignCriteria& criteria) {
    MemberCheck out;
    out.member = &member;

    BeamLoads loads = input;
    if (criteria.includeSelfWeight) {
        const double w = member.selfWeightPlf();
        loads.line.push_back({0.0, loads.span, CaseLoads::of(w), CaseLoads::of(w), "beam self weight"});
    }

    const double S = member.sectionModulus();
    const double A = member.area();
    const double EI = member.E * member.momentOfInertia();
    const double fbBase = member.Fb * bendingFactors(member, loads.span);

    for (const auto& combo : asdGravityCombinations()) {
        const SpanResponse r = analyzeSimpleSpan(loads, combo.factors, member.d);
        const double FbPrime = fbBase * combo.durationFactor;
        const double FvPrime = member.Fv * combo.durationFactor;

        const double bending = (r.maxMoment / S) / FbPrime;
        const double shear = (1.5 * r.maxShearBeyondD / A) / FvPrime;
        if (bending > out.bendingRatio) {
            out.bendingRatio = bending;
            out.bendingCombo = combo.name;
        }
        if (shear > out.shearRatio) {
            out.shearRatio = shear;
            out.shearCombo = combo.name;
        }
        out.maxReaction = std::max({out.maxReaction, r.reactionLeft, r.reactionRight});

        out.totalDeflection = std::max(out.totalDeflection, r.maxEIDeflection / EI);

        auto liveFactors = combo.factors;
        liveFactors[0] = 0.0;
        const SpanResponse live = analyzeSimpleSpan(loads, liveFactors, 0.0, 400);
        out.liveDeflection = std::max(out.liveDeflection, live.maxEIDeflection / EI);
    }

    out.liveDeflectionRatio = out.liveDeflection / (loads.span / criteria.liveDeflectionLimit);
    out.totalDeflectionRatio = out.totalDeflection / (loads.span / criteria.totalDeflectionLimit);

    // Bearing perpendicular to grain; CD does not apply (NDS 3.10.2).
    out.requiredBearing = out.maxReaction / (member.b * member.FcPerp);
    out.jackStuds = std::max(1, static_cast<int>(std::ceil(out.requiredBearing / criteria.studWidth - 1e-9)));

    const std::pair<double, const char*> checks[] = {
        {out.bendingRatio, "Bending"},
        {out.shearRatio, "Shear"},
        {out.liveDeflectionRatio, "Live load deflection"},
        {out.totalDeflectionRatio, "Total load deflection"},
    };
    for (const auto& [ratio, name] : checks) {
        if (ratio > out.maxRatio) {
            out.maxRatio = ratio;
            out.governingCheck = name;
        }
    }
    out.passes = out.maxRatio <= 1.0;
    return out;
}

std::vector<MemberCheck> recommendMembers(const BeamLoads& loads, const DesignCriteria& criteria,
                                          const std::vector<Member>& catalog, size_t maxResults) {
    std::vector<MemberCheck> passing;
    for (const auto& m : catalog) {
        MemberCheck c = checkMember(m, loads, criteria);
        if (c.passes) passing.push_back(c);
    }
    std::sort(passing.begin(), passing.end(), [](const MemberCheck& a, const MemberCheck& b) {
        if (std::abs(a.member->d - b.member->d) > 1e-6) return a.member->d < b.member->d;
        if (std::abs(a.member->b - b.member->b) > 1e-6) return a.member->b < b.member->b;
        return a.member->selfWeightPlf() < b.member->selfWeightPlf();
    });
    if (passing.size() > maxResults) passing.resize(maxResults);
    return passing;
}

} // namespace oa::structural
