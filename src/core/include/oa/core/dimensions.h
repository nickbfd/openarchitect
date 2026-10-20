#pragma once

#include "oa/core/document.h"

#include <vector>

namespace oa {

// Dimension strings around the outside of a level's exterior walls, the way
// a drafter lays them out: opening centers and corners nearest the walls,
// then corners, then the overall length, on each of the four sides of the
// walls' bounding box. The dimensions come back without ids, marked
// automatic.
std::vector<Dimension> exteriorDimensions(const Document& doc, ObjectId levelId);

} // namespace oa
