#pragma once

#include "oa/core/document.h"

#include <vector>

namespace oa {

// Dimension strings around the outside of a level's exterior walls, the way
// a drafter lays them out: opening centers and corners nearest the walls,
// then corners, then the overall length, on each of the four sides of the
// walls' bounding box. Walls are measured to the outside face of their
// framing or to its middle, as the document's dimensionTo says. The
// dimensions come back without ids, marked automatic.
std::vector<Dimension> exteriorDimensions(const Document& doc, ObjectId levelId);

// Changes where dimensions to walls land, and redoes the automatic
// exterior dimensions on every level that has them.
void setDimensionTo(Document& doc, DimensionTo to);

// Unit normal of a wall pointing out of the building, found by which side
// lies inside the loop of the level's exterior walls. A wall that closes no
// loop faces out to the right of a to b.
Vec2 wallOutside(const Document& doc, const Wall& w);

// The line a dimension to this wall measures to: its centerline moved to
// the framing's outside face or middle, with its ends carried to where it
// meets the same line of the walls joined to it. Interior walls and room
// dividers keep their centerlines.
Segment wallMeasureLine(const Document& doc, const Wall& w, DimensionTo to);

// Points the dimension tool and dimension nodes snap to on a level: wall
// measure line ends, both framing faces at each wall end, opening centers
// and edges, post centers and beam ends.
std::vector<Vec2> dimensionSnapPoints(const Document& doc, ObjectId levelId);

} // namespace oa
