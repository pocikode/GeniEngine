#pragma once

#include "assets/CookedFormats.h"

#include <cstddef>

// Mesh optimisation and simplification for the cooker (docs/design/0007). meshoptimizer is
// linked here only; the runtime reads the LOD table the cooker writes into the .gmesh.
namespace Geni::cook
{

// Reorders the triangles for the vertex cache and the vertices for fetch locality, then
// appends up to MaxMeshLods - 1 simplified index ranges to the index buffer and fills
// `mesh.lods`. A mesh that is too small to simplify, or does not shrink, keeps one LOD.
// Only indexed triangle lists are changed.
void OptimizeAndBuildLods(cooked::CookedMeshData &mesh);

// Simplifies an indexed triangle list in place towards `targetRatio` of its triangles,
// allowing an error of `targetError` (a fraction of the mesh's extent). Topology-
// preserving simplification first; when that stops above the target (a mesh made of many
// separate boxes cannot collapse), a sloppy pass that may merge them. Unreferenced
// vertices are removed. Returns the absolute error in model units.
float SimplifyInPlace(cooked::CookedMeshData &mesh, float targetRatio, float targetError);

// Smallest number of triangles worth building LODs for.
constexpr size_t MinTrianglesForLods = 64;

} // namespace Geni::cook
