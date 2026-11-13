#pragma once

// Building navigation tiles (docs/design/0014): Recast and Detour only, no engine state, so
// the asset cooker builds the same tiles offline.

#include <glm/ext/vector_double3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace Geni
{

class GameObject;

// Recast build settings and the tile grid (docs/design/0014). Distances in metres.
struct NavMeshSettings
{
    float cellSize = 0.3f;
    float cellHeight = 0.2f;
    float agentRadius = 0.4f;
    float agentHeight = 1.8f;
    float agentClimb = 0.4f;
    float agentMaxSlope = 45.0f;
    float regionMinSize = 8.0f;
    float regionMergeSize = 20.0f;
    float edgeMaxLength = 12.0f;
    float edgeMaxError = 1.3f;
    float detailSampleDistance = 6.0f;
    float detailSampleMaxError = 1.0f;
    // Tiles are squares of this size, starting at `origin` (a streamed world's cells).
    float tileSize = 64.0f;
    glm::dvec3 origin{0.0};
    // Tiles held at once and polygons per tile: Detour packs both into 22 bits.
    int maxTiles = 256;
    int maxPolysPerTile = 16384;
    int maxAgents = 256;

    // A hash of everything that changes a built tile (not maxTiles or maxAgents): a cooked
    // tile is used only when the runtime's settings give the same key.
    uint64_t BuildKey() const;
};

// Triangles in world coordinates: what the navigation mesh is built from.
struct NavGeometry
{
    std::vector<float> vertices; // x, y, z
    std::vector<int> triangles;  // three vertex indices each
    glm::vec3 boundsMin{1e30f};
    glm::vec3 boundsMax{-1e30f};

    // A unit box (-0.5..0.5) transformed by `transform` (world space, scale included).
    void AddBox(const glm::mat4 &transform);
    void AddTriangle(const glm::vec3 &a, const glm::vec3 &b, const glm::vec3 &c);
    bool Empty() const
    {
        return triangles.empty();
    }
    // The static colliders under `root` (PhysicsComponent), as boxes, in world space.
    // Defined by the engine (NavigationSystem.cpp); the cooker reads scene files instead.
    void AddColliders(GameObject &root);
};

class NavMeshBuilder
{
  public:
    // The polygon flag every walkable polygon carries.
    static constexpr unsigned short WalkableFlag = 0x01;

    // Builds one tile's Detour data from geometry (any thread, no shared state). Empty
    // when the tile has nothing walkable.
    static std::vector<uint8_t> BuildTile(
        const NavGeometry &geometry, const NavMeshSettings &settings, int tileX, int tileZ, std::string *error = nullptr
    );
};

} // namespace Geni
