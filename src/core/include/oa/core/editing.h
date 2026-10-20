#pragma once

#include "oa/core/document.h"

namespace oa {

// Edits that keep the plan joined up, the way Chief Architect's walls stay
// connected: moving or stretching a wall carries the ends of the walls
// joined to it.

// Moves wall `id` to run from `a` to `b`. Other walls on its level whose
// ends met its old ends (within half an inch) are stretched to meet the new ones.
void setWallEnds(Document& doc, ObjectId id, Vec2 a, Vec2 b);

// Slides a wall square to itself by `distance` (positive to the left of a
// to b), stretching the walls joined to it.
void slideWall(Document& doc, ObjectId id, double distance);

// Sets a wall's length by moving its end (`anchor` 0), its start (1), or
// both ends equally (2).
void setWallLength(Document& doc, ObjectId id, double length, int anchor);

// Starts the next floor from the one below, the way Chief's Build New
// Floor does: copies the highest walled level's exterior walls onto the
// empty level above it, adding that level first when there is none.
// Returns the level the walls went to.
ObjectId buildNewFloor(Document& doc);

} // namespace oa
