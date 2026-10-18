#include "oa/structural/beam_analysis.h"
#include "oa/structural/members.h"
#include "oa/structural/sizing.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <set>

using Catch::Approx;
using namespace oa::structural;

namespace {
constexpr std::array<double, kCaseCount> kDeadOnly = {1, 0, 0, 0};

BeamLoads uniform(double span, double d, double l = 0) {
    BeamLoads b;
    b.span = span;
    b.line.push_back({0, span, CaseLoads::of(d, l), CaseLoads::of(d, l), "test"});
    return b;
}
} // namespace

TEST_CASE("uniform load on a simple span matches closed form") {
    const auto r = analyzeSimpleSpan(uniform(144, 100), kDeadOnly);
    const double w = 100.0 / 12.0;
    CHECK(r.reactionLeft == Approx(600));
    CHECK(r.reactionRight == Approx(600));
    CHECK(r.maxMoment == Approx(w * 144 * 144 / 8).epsilon(1e-4));
    CHECK(r.maxShear == Approx(600).epsilon(1e-3));
    CHECK(r.maxEIDeflection == Approx(5 * w * std::pow(144.0, 4) / 384).epsilon(1e-3));
}

TEST_CASE("center point load matches closed form") {
    BeamLoads b;
    b.span = 144;
    b.point.push_back({72, CaseLoads::of(1000), "post"});
    const auto r = analyzeSimpleSpan(b, kDeadOnly);
    CHECK(r.reactionLeft == Approx(500));
    CHECK(r.maxMoment == Approx(1000 * 144 / 4.0));
    CHECK(r.maxEIDeflection == Approx(1000 * std::pow(144.0, 3) / 48).epsilon(1e-3));
}

TEST_CASE("triangular load reactions") {
    BeamLoads b;
    b.span = 120;
    b.line.push_back({0, 120, CaseLoads::of(0), CaseLoads::of(120), "tri"});
    const auto r = reactionsByCase(b);
    // Total 600 lb: one third at the low end, two thirds at the high end.
    CHECK(r.left[LoadCase::Dead] == Approx(200));
    CHECK(r.right[LoadCase::Dead] == Approx(400));
}

TEST_CASE("2x10 DF-L No.2 hand calculation") {
    const Member* m = findMember("dfl-no2-1-2x10");
    REQUIRE(m);
    DesignCriteria criteria = DesignCriteria::floorMember();
    criteria.includeSelfWeight = false;
    const auto c = checkMember(*m, uniform(144, 100, 200), criteria);

    // D + L = 300 plf: M = 5400 lb-ft, S = 21.39 in^3, Fb' = 900 x CF 1.1 x CD 1.0.
    const double S = 1.5 * 9.25 * 9.25 / 6.0;
    CHECK(c.bendingRatio == Approx(64800.0 / S / 990.0).epsilon(1e-3));
    CHECK(c.bendingCombo == "D + L");
    // Shear at d from the support: (1800 - 25 x 9.25) lb.
    CHECK(c.shearRatio == Approx(1.5 * (1800 - 25 * 9.25) / (1.5 * 9.25) / 180.0).epsilon(0.01));
    // Live deflection 5wL^4 / 384EI against L/360.
    const double I = 1.5 * std::pow(9.25, 3) / 12.0;
    const double delta = 5 * (200.0 / 12) * std::pow(144.0, 4) / (384 * 1.6e6 * I);
    CHECK(c.liveDeflection == Approx(delta).epsilon(1e-3));
    CHECK(c.liveDeflectionRatio == Approx(delta / 0.4).epsilon(1e-3));
    CHECK_FALSE(c.passes);
    CHECK(c.governingCheck == "Bending");
}

TEST_CASE("recommendations all pass and come shallowest first") {
    const auto loads = uniform(144, 100, 200);
    const auto list = recommendMembers(loads, DesignCriteria::floorMember());
    REQUIRE_FALSE(list.empty());
    for (size_t i = 0; i < list.size(); ++i) {
        CHECK(list[i].passes);
        CHECK(list[i].maxRatio <= 1.0);
        if (i > 0) CHECK(list[i - 1].member->d <= list[i].member->d);
    }
}

TEST_CASE("the catalog has stable unique ids") {
    const auto& cat = defaultCatalog();
    CHECK(cat.size() > 150);
    std::set<std::string> ids;
    for (const auto& m : cat) CHECK(ids.insert(m.id).second);
    CHECK(findMember("lvl-2.0e-2-11-7/8"));
    CHECK(findMember("glulam-24fv4-5-1/8x12"));
    CHECK_FALSE(findMember("nope"));
}
