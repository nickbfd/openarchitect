#include "oa/core/document.h"
#include "oa/structural/posts.h"
#include "oa/structural/takedown.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using Catch::Approx;
using namespace oa;
using namespace oa::structural;

TEST_CASE("a post check follows the NDS column stability factor") {
    const PostMember* post = findPost("dfl-no2-4x4");
    REQUIRE(post != nullptr);
    // 8 ft 4x4 with 2000 lb dead and 2000 lb snow: D+S governs with CD = 1.15.
    const PostCheck check = checkPost(*post, CaseLoads::of(2000, 0, 0, 2000), 96);
    CHECK(check.slenderness == Approx(96 / 3.5));
    const double FcE = 0.822 * 580e3 / std::pow(96 / 3.5, 2);
    const double FcStar = 1350 * 1.15 * 1.15;
    const double alpha = FcE / FcStar;
    const double k = (1 + alpha) / 1.6;
    const double Cp = k - std::sqrt(k * k - alpha / 0.8);
    CHECK(check.capacity == Approx(FcStar * Cp * 3.5 * 3.5));
    CHECK(check.demand == Approx(4000));
    CHECK(check.passes);

    // Too much load moves the recommendation up a size.
    const CaseLoads heavy = CaseLoads::of(6000, 0, 0, 8000);
    CHECK_FALSE(checkPost(*post, heavy, 96).passes);
    const PostMember* bigger = recommendPost(heavy, 96);
    REQUIRE(bigger != nullptr);
    CHECK(bigger->area() > post->area());
}

TEST_CASE("posts carry porch beam ends down to the foundation") {
    Document doc = Document::createDefault();
    const ObjectId l1 = doc.levels[0].id;
    Wall house;
    house.id = doc.newId();
    house.levelId = l1;
    house.a = {240, 0};
    house.b = {0, 0};
    doc.walls.push_back(house);
    Beam beam;
    beam.id = doc.newId();
    beam.levelId = l1;
    beam.a = {0, -96};
    beam.b = {240, -96};
    doc.beams.push_back(beam);
    for (Vec2 at : {beam.a, beam.b}) {
        Post p;
        p.id = doc.newId();
        p.levelId = l1;
        p.position = at;
        doc.posts.push_back(p);
    }
    RoofPlane porch;
    porch.id = doc.newId();
    porch.levelId = l1;
    porch.a = beam.a;
    porch.b = beam.b;
    porch.outline = RoofPlane::rectangle(porch.a, porch.b, 96, 12);
    doc.roofPlanes.push_back(porch);
    doc.refresh();

    const auto result = runTakedown(doc);
    CHECK(result.warnings.empty());
    const SupportLoads* onBeam = result.beam(beam.id);
    REQUIRE(onBeam != nullptr);
    const double beamLive = onBeam->topLoads().peakPlf[LoadCase::RoofLive] * 20;
    CHECK(beamLive == Approx(20 * (108.0 * 108 / (2 * 96)) / 12 * 20));
    for (const auto& p : doc.posts) {
        const PostLoads* carried = result.post(p.id);
        REQUIRE(carried != nullptr);
        CHECK(carried->load[LoadCase::RoofLive] == Approx(beamLive / 2));
        CHECK(carried->load[LoadCase::Dead] > 0);
    }

    // Without the posts the beam ends have nothing under them.
    doc.posts.clear();
    const auto bare = runTakedown(doc);
    REQUIRE(bare.warnings.size() == 2);
    CHECK(bare.warnings[0].find("Add a post there") != std::string::npos);
}

TEST_CASE("a plane leaning on a ridge puts its whole load on the eave") {
    Document doc = Document::createDefault();
    const ObjectId l1 = doc.levels[0].id;
    Wall eave;
    eave.id = doc.newId();
    eave.levelId = l1;
    eave.a = {0, 0};
    eave.b = {240, 0};
    doc.walls.push_back(eave);
    doc.wallTypes.clear();
    RoofPlane p;
    p.id = doc.newId();
    p.levelId = l1;
    p.a = eave.a;
    p.b = eave.b;
    p.highBearing = false;
    // A trapezoid like one side of a hip roof: 240" at the eave, 0 at the top.
    p.outline = {{-12, -12}, {252, -12}, {120, 120}};
    doc.roofPlanes.push_back(p);
    const auto result = runTakedown(doc);
    const SupportLoads* wall = result.wall(eave.id);
    REQUIRE(wall != nullptr);
    CHECK(result.warnings.empty());
    // The rake overhangs past the wall ends come down as point loads at its ends.
    double total = 0;
    for (const auto& l : wall->lineLoads)
        if (l.origin == LoadOrigin::Roof) total += l.total()[LoadCase::RoofLive];
    REQUIRE(wall->pointLoads.size() == 2);
    for (const auto& pl : wall->pointLoads) total += pl.p[LoadCase::RoofLive];
    CHECK(total == Approx(20 * area(p.outline) / 144).epsilon(0.01));
}
