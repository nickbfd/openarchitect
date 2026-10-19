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
    std::vector<oa::ObjectId> owners; // the object each triangle belongs to
    oa::Vec3 min{1e30, 1e30, 1e30};
    oa::Vec3 max{-1e30, -1e30, -1e30};

    bool empty() const { return vertices.empty(); }
};

// The object `selected` is drawn in the selection color.
Mesh buildMesh(const oa::Document& doc, oa::ObjectId selected = oa::kNoId);

// The object whose triangle a ray from `origin` along `dir` hits first, or kNoId.
oa::ObjectId pickMesh(const Mesh& mesh, oa::Vec3 origin, oa::Vec3 dir);
