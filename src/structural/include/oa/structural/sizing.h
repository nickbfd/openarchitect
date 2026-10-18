#pragma once

#include "oa/structural/beam_analysis.h"
#include "oa/structural/members.h"

#include <string>
#include <vector>

namespace oa::structural {

struct DesignCriteria {
    double liveDeflectionLimit = 360.0;  // span / this, under live (L, Lr or S) load
    double totalDeflectionLimit = 240.0; // span / this, under total load
    bool includeSelfWeight = true;
    double studWidth = 1.5;              // inches per jack or king stud, for bearing

    static DesignCriteria floorMember() { return {360.0, 240.0, true, 1.5}; }
    static DesignCriteria roofMember() { return {240.0, 180.0, true, 1.5}; }
};

// Utilization ratios are demand over capacity: 1.0 is exactly at the limit.
//
// Assumptions stated on every report: dry service, normal temperatures,
// the compression edge braced along its length (CL = 1), simple span,
// deflection from short-term E. NDS sections: 3.3 bending, 3.4 shear,
// 3.5 deflection, 3.10 bearing; adjustment factors from 4.3, 5.3 and 8.3.
struct MemberCheck {
    const Member* member = nullptr;
    double bendingRatio = 0.0;
    double shearRatio = 0.0;
    double liveDeflectionRatio = 0.0;
    double totalDeflectionRatio = 0.0;
    double liveDeflection = 0.0;     // inches
    double totalDeflection = 0.0;    // inches
    double maxReaction = 0.0;        // lb, worst support under the worst combination
    double requiredBearing = 0.0;    // inches of bearing length at that support
    int jackStuds = 1;               // studs needed under each end to provide that bearing
    std::string bendingCombo;
    std::string shearCombo;
    std::string governingCheck;
    double maxRatio = 0.0;
    bool passes = false;
};

MemberCheck checkMember(const Member& member, const BeamLoads& loads, const DesignCriteria& criteria);

// Every catalog member that passes, ranked by depth, then width, then
// weight, so the first entry is the shallowest workable choice.
std::vector<MemberCheck> recommendMembers(const BeamLoads& loads, const DesignCriteria& criteria,
                                          const std::vector<Member>& catalog = defaultCatalog(),
                                          size_t maxResults = 25);

} // namespace oa::structural
