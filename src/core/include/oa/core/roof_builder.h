#pragma once

#include "oa/core/document.h"

#include <optional>
#include <vector>

namespace oa {

// Faces of a weighted straight skeleton: the roof that rises from every
// edge of `outline` (counterclockwise) at once, edge i moving inward
// speed[i] inches for every inch it rises. A speed of 0 is a vertical edge,
// like a gable wall. Returns one plan polygon per edge (counterclockwise,
// starting with the edge itself), empty for edges whose face has no area.
std::vector<Polygon> skeletonFaces(const Polygon& outline, const std::vector<double>& speed);

// One side of the loop of a level's exterior walls, counterclockwise.
struct LoopEdge {
    Vec2 a, b;
    ObjectId wallId = kNoId;
};

// The closed loop of a level's exterior wall centerlines, counterclockwise,
// with walls that continue in a straight line with the same roof settings
// joined. Empty when the exterior walls don't close.
std::vector<LoopEdge> exteriorWallLoop(const Document& doc, ObjectId levelId);

// The roof planes Build Roof makes over a level's exterior walls: one plane
// rising from each sloped wall at its pitch, meeting the others at hips,
// valleys and ridges, with gable and high shed walls going up vertically.
// Overhangs come from each wall or the defaults. Planes come back without
// ids and marked automatic.
std::vector<RoofPlane> buildRoofPlanes(const Document& doc, ObjectId levelId);

// Replaces the automatic roof planes on a level with freshly built ones,
// keeping their ids where it can. Returns how many planes were made; 0 when
// the level's exterior walls don't close.
int buildRoof(Document& doc, ObjectId levelId);

// Rebuilds the automatic roof on every level that has one. Document::refresh
// calls it when the roof defaults say to rebuild automatically.
void rebuildAutomaticRoofs(Document& doc);

// Whether an edit changed automatic roof planes by hand: they differ from
// `before` and from what Build Roof would make now. Chief turns off
// automatic rebuilding at that point so the edit isn't lost.
bool roofEditedByHand(const Document& before, const Document& after);

} // namespace oa
