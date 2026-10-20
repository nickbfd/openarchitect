#pragma once

#include "oa/core/document.h"

namespace oa::structural {

// Balanced roof snow load on the horizontal projection, psf, per ASCE 7-16
// Chapter 7: flat roof snow pf = 0.7 Ce Ct Is pg, the low-slope minimum,
// and the slope factor Cs for roofs that are not slippery.
// Drifts, unbalanced loads and rain-on-snow are not included yet.
double flatRoofSnowPsf(const Site& site);
double slopeFactorCs(double slopeRadians, double thermalCt);
double roofSnowPsf(const Site& site, double slopeRadians);
// The snow a roof is designed for: its custom value if it has one,
// otherwise the balanced snow calculated from the site and its slope.
double designSnowPsf(const Site& site, const RoofSettings& roof);

} // namespace oa::structural
