#pragma once

#include "oa/core/document.h"
#include "oa/structural/beam_analysis.h"
#include "oa/structural/loads.h"
#include "oa/structural/sizing.h"

#include <string>
#include <vector>

namespace oa::structural {

enum class SupportKind { Wall, Beam, RidgeBeam };

// Everything that lands on one wall or beam. Positions are inches from
// `line.a`; line loads are plf and point loads are pounds, unfactored per case.
struct SupportLoads {
    SupportKind kind = SupportKind::Wall;
    ObjectId objectId = kNoId;  // the wall or beam, or the roof for a ridge beam
    ObjectId levelId = kNoId;
    std::string label;
    Segment line;
    std::vector<LineLoad> lineLoads;   // applied on top of the member
    std::vector<PointLoad> pointLoads; // applied on top of the member
    LineLoad selfWeight;               // walls only
    Reactions reactions;               // beams only: what each end delivers below

    double length() const { return line.length(); }
    // Peak plf per case on top of the member, and the governing combination.
    LineLoadSummary topLoads() const { return summarize(lineLoads); }
    bool carriesRoofOnly() const;
};

struct TakedownResult {
    std::vector<SupportLoads> supports;
    std::vector<std::string> warnings;

    const SupportLoads* find(SupportKind kind, ObjectId objectId) const;
    const SupportLoads* wall(ObjectId id) const { return find(SupportKind::Wall, id); }
    const SupportLoads* beam(ObjectId id) const { return find(SupportKind::Beam, id); }
    const SupportLoads* ridgeBeam(ObjectId roofId) const { return find(SupportKind::RidgeBeam, roofId); }
};

// Gravity load takedown, US residential practice:
//  - roofs: dead load on the surface converted to horizontal projection,
//    20 psf roof live load (IRC R301.6), balanced snow from the site (ASCE 7);
//    gable eaves take half the rafter run plus overhang, hip roofs split the
//    footprint by its straight skeleton (45 degree hips in plan);
//  - ceilings and floors: one-way framing, each support taking half the
//    distance to its neighbors (or the cantilever to the edge);
//  - walls add their self weight and pass everything to the wall or beam
//    directly beneath; beams pass their end reactions as point loads.
// Anything with no support beneath produces a warning naming it.
TakedownResult runTakedown(const Document& doc);

// Loads for sizing a beam or ridge beam, spanning the full member length.
BeamLoads beamLoads(const SupportLoads& support);

// Loads for sizing the header over an opening: the wall's loads over the
// opening plus 1-1/2" bearing each side, and the wall weight above the header.
BeamLoads headerLoads(const Document& doc, const TakedownResult& result, ObjectId openingId);

// Deflection limits that suit what the member carries.
DesignCriteria criteriaFor(const BeamLoads& loads);

} // namespace oa::structural
