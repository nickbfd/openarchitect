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

// An edge of the skeleton's outline that can change speed partway up: from
// `changeHeight` (when positive) it moves at `upperSpeed` instead, like the
// break of a gambrel or the top of a Dutch gable.
struct SkeletonEdge {
    double speed = 1.0;
    double changeHeight = -1.0;
    double upperSpeed = 0.0;
};

// One face of a skeleton whose edges change speed: the part of edge `edge`'s
// roof from `startHeight` up (phase 0 from the outline, phase 1 above the change).
struct SkeletonFace {
    size_t edge = 0;
    int phase = 0;
    double startHeight = 0.0;
    Polygon outline; // empty when it has no area
};

// The faces of the skeleton, the first outline.size() of them phase 0 in edge order.
std::vector<SkeletonFace> skeletonFaces(const Polygon& outline, const std::vector<SkeletonEdge>& edges);

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
// keeping their ids where it can. With `keepSettings`, each plane keeps the
// framing, sheathing and loads set on the plane it replaces (an automatic
// rebuild); otherwise they all come from the roof defaults (Build Roof).
// Returns how many planes were made; 0 when the level's exterior walls don't close.
int buildRoof(Document& doc, ObjectId levelId, bool keepSettings = false);

// Rebuilds the automatic roof on every level that has one, keeping each
// plane's own settings. Document::refresh calls it when the roof defaults
// say to rebuild automatically.
void rebuildAutomaticRoofs(Document& doc);

// Whether an edit changed automatic roof planes by hand: they differ from
// `before` and from what Build Roof would make now. Chief turns off
// automatic rebuilding at that point so the edit isn't lost.
bool roofEditedByHand(const Document& before, const Document& after);

} // namespace oa
