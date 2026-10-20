#pragma once

#include "oa/core/document.h"

#include <array>
#include <optional>
#include <vector>

namespace oa {

// Delaunay triangulation of plan points (Bowyer-Watson). Triangles are
// counter-clockwise index triples into `points`; duplicate points are skipped.
std::vector<std::array<int, 3>> delaunay(const std::vector<Vec2>& points);

// The terrain surface as triangles between its elevation points.
struct TerrainMesh {
    std::vector<Vec2> points;
    std::vector<double> elevations;
    std::vector<std::array<int, 3>> triangles;
};
TerrainMesh terrainMesh(const Terrain& terrain);

// One piece of a contour line, inside one triangle of the terrain.
struct ContourSegment {
    double elevation;
    Vec2 a, b;
    bool major; // every fifth contour, drawn heavier and labeled
};
std::vector<ContourSegment> contourLines(const Terrain& terrain);

// Height of the terrain at a plan point, or nothing outside it.
std::optional<double> terrainHeightAt(const Terrain& terrain, Vec2 p);

} // namespace oa
