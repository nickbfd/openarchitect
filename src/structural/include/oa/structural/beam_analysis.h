#pragma once

#include "oa/structural/loads.h"

#include <vector>

namespace oa::structural {

// Everything a simply supported member carries, unfactored per case.
// Positions are inches from the left support; line loads are plf.
struct BeamLoads {
    double span = 0.0; // inches, center to center of bearing
    std::vector<LineLoad> line;
    std::vector<PointLoad> point;

    CaseLoads totalLoad() const; // pounds per case
};

// Response of a simple span to one set of loads (already combined into a
// single number per load: lb/in for line loads, lb for point loads).
struct SpanResponse {
    double reactionLeft = 0.0;   // lb
    double reactionRight = 0.0;  // lb
    double maxMoment = 0.0;      // lb-in, absolute
    double maxShear = 0.0;       // lb, absolute, anywhere on the span
    double maxShearBeyondD = 0.0;// lb, absolute, at least `shearCheckDistance` from supports
    double maxEIDeflection = 0.0;// lb-in^3; divide by E*I for inches
};

// Simple-span statics with exact shear and moment at stations and numerical
// double integration for deflection. `factors` combines the cases.
SpanResponse analyzeSimpleSpan(const BeamLoads& loads, const std::array<double, kCaseCount>& factors,
                               double shearCheckDistance = 0.0, int stations = 1000);

// Unfactored support reactions per case, in pounds.
struct Reactions {
    CaseLoads left;
    CaseLoads right;
};
Reactions reactionsByCase(const BeamLoads& loads);

} // namespace oa::structural
