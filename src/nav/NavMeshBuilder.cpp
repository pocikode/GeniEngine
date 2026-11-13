#include "nav/NavMeshBuilder.h"

#include <DetourAlloc.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <Recast.h>

#include <glm/common.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Geni
{

namespace
{

// Unit-cube corners and the 12 triangles of its faces, wound outwards.
const glm::vec3 kCorners[8] = {
    {-0.5f, -0.5f, -0.5f}, {0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, -0.5f}, {-0.5f, 0.5f, -0.5f},
    {-0.5f, -0.5f, 0.5f},  {0.5f, -0.5f, 0.5f},  {0.5f, 0.5f, 0.5f},  {-0.5f, 0.5f, 0.5f},
};
const int kBoxTriangles[36] = {
    0, 2, 1, 0, 3, 2, // -z
    4, 5, 6, 4, 6, 7, // +z
    0, 4, 7, 0, 7, 3, // -x
    1, 2, 6, 1, 6, 5, // +x
    3, 7, 6, 3, 6, 2, // +y (top: walkable)
    0, 1, 5, 0, 5, 4, // -y
};

} // namespace

void NavGeometry::AddTriangle(const glm::vec3 &a, const glm::vec3 &b, const glm::vec3 &c)
{
    const int base = static_cast<int>(vertices.size() / 3);
    for (const glm::vec3 &v : {a, b, c})
    {
        vertices.push_back(v.x);
        vertices.push_back(v.y);
        vertices.push_back(v.z);
        boundsMin = glm::min(boundsMin, v);
        boundsMax = glm::max(boundsMax, v);
    }
    triangles.push_back(base);
    triangles.push_back(base + 1);
    triangles.push_back(base + 2);
}

void NavGeometry::AddBox(const glm::mat4 &transform)
{
    const int base = static_cast<int>(vertices.size() / 3);
    for (const glm::vec3 &corner : kCorners)
    {
        const glm::vec3 v(transform * glm::vec4(corner, 1.0f));
        vertices.push_back(v.x);
        vertices.push_back(v.y);
        vertices.push_back(v.z);
        boundsMin = glm::min(boundsMin, v);
        boundsMax = glm::max(boundsMax, v);
    }
    for (int index : kBoxTriangles)
    {
        triangles.push_back(base + index);
    }
}

uint64_t NavMeshSettings::BuildKey() const
{
    // FNV-1a over every value that changes a tile's bytes.
    uint64_t hash = 14695981039346656037ull;
    auto mix = [&hash](const void *data, size_t size) {
        const auto *bytes = static_cast<const unsigned char *>(data);
        for (size_t i = 0; i < size; ++i)
        {
            hash ^= bytes[i];
            hash *= 1099511628211ull;
        }
    };
    const float values[] = {cellSize,      cellHeight,    agentRadius,          agentHeight,
                            agentClimb,    agentMaxSlope, regionMinSize,        regionMergeSize,
                            edgeMaxLength, edgeMaxError,  detailSampleDistance, detailSampleMaxError,
                            tileSize};
    mix(values, sizeof(values));
    const double originValues[] = {origin.x, origin.y, origin.z};
    mix(originValues, sizeof(originValues));
    mix(&maxPolysPerTile, sizeof(maxPolysPerTile));
    const uint32_t version = 1; // bump when BuildTile changes what it produces
    mix(&version, sizeof(version));
    return hash;
}

std::vector<uint8_t> NavMeshBuilder::BuildTile(
    const NavGeometry &geometry, const NavMeshSettings &settings, int tileX, int tileZ, std::string *error
)
{
    auto fail = [error](const char *message) {
        if (error)
        {
            *error = message;
        }
        return std::vector<uint8_t>();
    };
    if (geometry.Empty())
    {
        return fail("no geometry");
    }

    rcConfig cfg{};
    // Detour links neighbouring tiles by their shared edges, so a tile must be a whole
    // number of cells: nudge the cell size to divide the tile exactly.
    const int tileCells = std::max(1, static_cast<int>(std::lround(settings.tileSize / settings.cellSize)));
    cfg.cs = settings.tileSize / static_cast<float>(tileCells);
    cfg.ch = settings.cellHeight;
    cfg.walkableSlopeAngle = settings.agentMaxSlope;
    cfg.walkableHeight = static_cast<int>(std::ceil(settings.agentHeight / cfg.ch));
    cfg.walkableClimb = static_cast<int>(std::floor(settings.agentClimb / cfg.ch));
    cfg.walkableRadius = static_cast<int>(std::ceil(settings.agentRadius / cfg.cs));
    cfg.maxEdgeLen = static_cast<int>(settings.edgeMaxLength / cfg.cs);
    cfg.maxSimplificationError = settings.edgeMaxError;
    cfg.minRegionArea = static_cast<int>(settings.regionMinSize * settings.regionMinSize);
    cfg.mergeRegionArea = static_cast<int>(settings.regionMergeSize * settings.regionMergeSize);
    cfg.maxVertsPerPoly = DT_VERTS_PER_POLYGON;
    cfg.tileSize = tileCells;
    cfg.borderSize = cfg.walkableRadius + 3;
    cfg.width = cfg.tileSize + cfg.borderSize * 2;
    cfg.height = cfg.tileSize + cfg.borderSize * 2;
    cfg.detailSampleDist = settings.detailSampleDistance < 0.9f ? 0.0f : cfg.cs * settings.detailSampleDistance;
    cfg.detailSampleMaxError = cfg.ch * settings.detailSampleMaxError;

    const float originX = static_cast<float>(settings.origin.x);
    const float originZ = static_cast<float>(settings.origin.z);
    cfg.bmin[0] = originX + static_cast<float>(tileX) * settings.tileSize - static_cast<float>(cfg.borderSize) * cfg.cs;
    cfg.bmin[1] = geometry.boundsMin.y - 1.0f;
    cfg.bmin[2] = originZ + static_cast<float>(tileZ) * settings.tileSize - static_cast<float>(cfg.borderSize) * cfg.cs;
    cfg.bmax[0] =
        originX + static_cast<float>(tileX + 1) * settings.tileSize + static_cast<float>(cfg.borderSize) * cfg.cs;
    cfg.bmax[1] = geometry.boundsMax.y + 1.0f;
    cfg.bmax[2] =
        originZ + static_cast<float>(tileZ + 1) * settings.tileSize + static_cast<float>(cfg.borderSize) * cfg.cs;

    rcContext context(false);
    struct Owned
    {
        rcHeightfield *solid = nullptr;
        rcCompactHeightfield *compact = nullptr;
        rcContourSet *contours = nullptr;
        rcPolyMesh *mesh = nullptr;
        rcPolyMeshDetail *detail = nullptr;
        ~Owned()
        {
            rcFreeHeightField(solid);
            rcFreeCompactHeightfield(compact);
            rcFreeContourSet(contours);
            rcFreePolyMesh(mesh);
            rcFreePolyMeshDetail(detail);
        }
    } owned;

    owned.solid = rcAllocHeightfield();
    if (!owned.solid ||
        !rcCreateHeightfield(&context, *owned.solid, cfg.width, cfg.height, cfg.bmin, cfg.bmax, cfg.cs, cfg.ch))
    {
        return fail("could not create the heightfield");
    }

    // Only triangles that overlap the tile (with its border) are rasterized.
    const int vertexCount = static_cast<int>(geometry.vertices.size() / 3);
    std::vector<int> triangles;
    triangles.reserve(geometry.triangles.size());
    for (size_t t = 0; t + 2 < geometry.triangles.size(); t += 3)
    {
        float minX = 1e30f;
        float maxX = -1e30f;
        float minZ = 1e30f;
        float maxZ = -1e30f;
        for (int k = 0; k < 3; ++k)
        {
            const float *v = &geometry.vertices[static_cast<size_t>(geometry.triangles[t + k]) * 3];
            minX = std::min(minX, v[0]);
            maxX = std::max(maxX, v[0]);
            minZ = std::min(minZ, v[2]);
            maxZ = std::max(maxZ, v[2]);
        }
        if (maxX < cfg.bmin[0] || minX > cfg.bmax[0] || maxZ < cfg.bmin[2] || minZ > cfg.bmax[2])
        {
            continue;
        }
        triangles.push_back(geometry.triangles[t]);
        triangles.push_back(geometry.triangles[t + 1]);
        triangles.push_back(geometry.triangles[t + 2]);
    }
    const int triangleCount = static_cast<int>(triangles.size() / 3);
    if (triangleCount == 0)
    {
        return fail("no geometry in the tile");
    }
    std::vector<unsigned char> areas(static_cast<size_t>(triangleCount), 0);
    rcMarkWalkableTriangles(
        &context, cfg.walkableSlopeAngle, geometry.vertices.data(), vertexCount, triangles.data(), triangleCount,
        areas.data()
    );
    if (!rcRasterizeTriangles(
            &context, geometry.vertices.data(), vertexCount, triangles.data(), areas.data(), triangleCount,
            *owned.solid, cfg.walkableClimb
        ))
    {
        return fail("could not rasterize");
    }

    rcFilterLowHangingWalkableObstacles(&context, cfg.walkableClimb, *owned.solid);
    rcFilterLedgeSpans(&context, cfg.walkableHeight, cfg.walkableClimb, *owned.solid);
    rcFilterWalkableLowHeightSpans(&context, cfg.walkableHeight, *owned.solid);

    owned.compact = rcAllocCompactHeightfield();
    if (!owned.compact ||
        !rcBuildCompactHeightfield(&context, cfg.walkableHeight, cfg.walkableClimb, *owned.solid, *owned.compact))
    {
        return fail("could not build the compact heightfield");
    }
    rcFreeHeightField(owned.solid);
    owned.solid = nullptr;
    if (!rcErodeWalkableArea(&context, cfg.walkableRadius, *owned.compact))
    {
        return fail("could not erode");
    }
    // Monotone partitioning: fast, and good enough for a city's rectilinear streets.
    if (!rcBuildRegionsMonotone(&context, *owned.compact, cfg.borderSize, cfg.minRegionArea, cfg.mergeRegionArea))
    {
        return fail("could not build regions");
    }
    owned.contours = rcAllocContourSet();
    if (!owned.contours ||
        !rcBuildContours(&context, *owned.compact, cfg.maxSimplificationError, cfg.maxEdgeLen, *owned.contours))
    {
        return fail("could not build contours");
    }
    if (owned.contours->nconts == 0)
    {
        return fail("nothing walkable in the tile");
    }
    owned.mesh = rcAllocPolyMesh();
    if (!owned.mesh || !rcBuildPolyMesh(&context, *owned.contours, cfg.maxVertsPerPoly, *owned.mesh))
    {
        return fail("could not build the polygon mesh");
    }
    owned.detail = rcAllocPolyMeshDetail();
    if (!owned.detail ||
        !rcBuildPolyMeshDetail(
            &context, *owned.mesh, *owned.compact, cfg.detailSampleDist, cfg.detailSampleMaxError, *owned.detail
        ))
    {
        return fail("could not build the detail mesh");
    }
    if (owned.mesh->nverts >= 0xffff || owned.mesh->npolys > settings.maxPolysPerTile)
    {
        return fail("too many vertices or polygons for one tile");
    }
    for (int i = 0; i < owned.mesh->npolys; ++i)
    {
        if (owned.mesh->areas[i] == RC_WALKABLE_AREA)
        {
            owned.mesh->areas[i] = 0;
        }
        owned.mesh->flags[i] = NavMeshBuilder::WalkableFlag;
    }

    dtNavMeshCreateParams params{};
    params.verts = owned.mesh->verts;
    params.vertCount = owned.mesh->nverts;
    params.polys = owned.mesh->polys;
    params.polyAreas = owned.mesh->areas;
    params.polyFlags = owned.mesh->flags;
    params.polyCount = owned.mesh->npolys;
    params.nvp = owned.mesh->nvp;
    params.detailMeshes = owned.detail->meshes;
    params.detailVerts = owned.detail->verts;
    params.detailVertsCount = owned.detail->nverts;
    params.detailTris = owned.detail->tris;
    params.detailTriCount = owned.detail->ntris;
    params.walkableHeight = settings.agentHeight;
    params.walkableRadius = settings.agentRadius;
    params.walkableClimb = settings.agentClimb;
    params.tileX = tileX;
    params.tileY = tileZ;
    params.tileLayer = 0;
    std::memcpy(params.bmin, owned.mesh->bmin, sizeof(params.bmin));
    std::memcpy(params.bmax, owned.mesh->bmax, sizeof(params.bmax));
    params.cs = cfg.cs;
    params.ch = cfg.ch;
    params.buildBvTree = true;

    unsigned char *data = nullptr;
    int size = 0;
    if (!dtCreateNavMeshData(&params, &data, &size))
    {
        return fail("could not create the Detour tile");
    }
    std::vector<uint8_t> result(data, data + size);
    dtFree(data);
    return result;
}

} // namespace Geni
