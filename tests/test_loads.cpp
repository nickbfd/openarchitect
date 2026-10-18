#include "oa/structural/loads.h"
#include "oa/structural/snow.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;
using namespace oa;
using namespace oa::structural;

TEST_CASE("ASCE 7 balanced snow") {
    Site site; // pg 30, Ce 1.0, Ct 1.1, Is 1.0
    CHECK(flatRoofSnowPsf(site) == Approx(23.1));
    // 6/12 is 26.6 degrees, below the 37.5 degree knee for Ct = 1.1.
    CHECK(roofSnowPsf(site, std::atan(0.5)) == Approx(23.1));
    // 12/12 is 45 degrees: Cs = (70 - 45) / 32.5.
    CHECK(slopeFactorCs(kPi / 4, 1.1) == Approx(25.0 / 32.5));
    CHECK(slopeFactorCs(kPi / 4, 1.0) == Approx(25.0 / 40.0));
    // Low-slope minimum: 20 Is when pg > 20.
    site.groundSnowPsf = 25;
    site.thermalCt = 1.0;
    site.exposureCe = 0.7;
    CHECK(roofSnowPsf(site, std::atan(1.0 / 12.0)) == Approx(20.0));
}

TEST_CASE("line load clipping and totals") {
    const LineLoad l{0, 120, CaseLoads::of(0), CaseLoads::of(120), "test"};
    CHECK(l.at(60)[LoadCase::Dead] == Approx(60));
    CHECK(l.total()[LoadCase::Dead] == Approx(600)); // 0.5 * 120 plf * 10 ft
    const LineLoad c = l.clipped(60, 200);
    CHECK(c.start == Approx(60));
    CHECK(c.end == Approx(120));
    CHECK(c.wStart[LoadCase::Dead] == Approx(60));
    CHECK(l.clipped(130, 140).length() == 0.0);
}

TEST_CASE("summary finds the peak and the governing combination") {
    std::vector<LineLoad> loads = {
        {0, 120, CaseLoads::of(100, 0, 0, 200), CaseLoads::of(100, 0, 0, 200), "roof"},
        {60, 120, CaseLoads::of(50, 300), CaseLoads::of(50, 300), "floor"},
    };
    const auto s = summarize(loads);
    CHECK(s.peakPlf[LoadCase::Dead] == Approx(150));
    CHECK(s.peakPlf[LoadCase::Live] == Approx(300));
    CHECK(s.peakPlf[LoadCase::Snow] == Approx(200));
    // D + 0.75L + 0.75S = 150 + 225 + 150 = 525 beats D + L = 450.
    CHECK(s.governingPlf == Approx(525));
    CHECK(s.governingCombo == "D + 0.75L + 0.75S");
}
