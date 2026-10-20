#pragma once

#include "oa/core/document.h"
#include "oa/structural/sizing.h"

#include <string>
#include <vector>

namespace oa::structural {

// A check of one roof plane's rafters: the longest rafter in it, as a simple
// span from its bearing to the ridge, hip or break (measured level), carrying
// its share of the roof at its spacing. The overhang is left off the span.
// Trusses are designed by their maker and aren't checked here.
struct RafterCheck {
    bool checked = false;
    double span = 0.0;     // inches, horizontal
    double plf = 0.0;      // governing load on one rafter, on the horizontal
    MemberCheck result;
    std::string note;      // why it wasn't checked
};

RafterCheck checkRafters(const Document& doc, const RoofPlane& plane);

// Rafter sizes a roof can pick from: single 2x4 to 2x12 of every species.
std::vector<const Member*> rafterChoices();

} // namespace oa::structural
