#include "oa/structural/snow.h"

#include <algorithm>

namespace oa::structural {

double flatRoofSnowPsf(const Site& site) {
    return 0.7 * site.exposureCe * site.thermalCt * site.importanceIs * site.groundSnowPsf;
}

double slopeFactorCs(double slopeRadians, double thermalCt) {
    const double deg = slopeRadians * 180.0 / kPi;
    // ASCE 7-16 Figure 7.4-1, solid lines (other surfaces).
    double flatUntil = 30.0;
    if (thermalCt > 1.15) flatUntil = 45.0;
    else if (thermalCt > 1.05) flatUntil = 37.5;
    if (deg <= flatUntil) return 1.0;
    if (deg >= 70.0) return 0.0;
    return (70.0 - deg) / (70.0 - flatUntil);
}

double roofSnowPsf(const Site& site, double slopeRadians) {
    double ps = slopeFactorCs(slopeRadians, site.thermalCt) * flatRoofSnowPsf(site);
    // Minimum snow load for low-slope roofs (7.3.4), slopes under 15 degrees.
    if (slopeRadians * 180.0 / kPi < 15.0) {
        const double pm = site.groundSnowPsf <= 20.0 ? site.importanceIs * site.groundSnowPsf
                                                     : 20.0 * site.importanceIs;
        ps = std::max(ps, pm);
    }
    return ps;
}

} // namespace oa::structural
