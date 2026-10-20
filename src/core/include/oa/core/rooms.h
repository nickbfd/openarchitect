#pragma once

#include "oa/core/document.h"

#include <vector>

namespace oa {

// The closed areas the walls of a level (room dividers included) enclose,
// each traced inside the wall faces, counter-clockwise. Walls are joined
// where they meet or cross within an inch; walls that dead-end are ignored.
std::vector<Polygon> findRoomOutlines(const Document& doc, ObjectId levelId);

// Matches the rooms of every level to the areas the walls enclose now. A
// room keeps its name and type while its middle stays inside one area; new
// areas get a new room, and rooms whose area is gone are removed.
void updateRooms(Document& doc);

} // namespace oa
