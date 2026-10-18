#pragma once

#include "oa/core/document.h"

#include <vector>

// Triangle mesh of the building for the 3D view, built straight from the
// document so the 3D model always matches the plan. Units are inches, z up.
struct MeshVertex {
    float position[3];
    float normal[3];
    float color[3];
};

struct Mesh {
    std::vector<MeshVertex> vertices; // every three form a triangle
    oa::Vec3 min{1e30, 1e30, 1e30};
    oa::Vec3 max{-1e30, -1e30, -1e30};

    bool empty() const { return vertices.empty(); }
};

Mesh buildMesh(const oa::Document& doc);
