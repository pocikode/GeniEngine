#pragma once

// A lane graph for traffic (docs/design/0025): nodes in world coordinates and directed
// lanes between them, straight or curved, some of them inside intersections.

#include <glm/ext/vector_double3.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace Geni
{

struct RoadNode
{
    glm::dvec3 position{0.0};
    // Lanes that start here.
    std::vector<uint32_t> out;
};

struct RoadLane
{
    uint32_t from = 0;
    uint32_t to = 0;
    // Metres per second.
    float speed = 10.0f;
    // A quadratic Bézier through `control` when curved.
    bool curved = false;
    glm::dvec3 control{0.0};
    // The intersection the lane crosses (-1: a street lane).
    int32_t intersection = -1;
    float length = 0.0f;
};

struct RoadGridDesc
{
    // World X and Z of the first street lines; lines are `spacing` apart.
    double originX = 0.0;
    double originZ = 0.0;
    int columns = 2; // street lines along X (constant X)
    int rows = 2;    // street lines along Z
    double spacing = 32.0;
    // Half the street's width (where crossings begin) and the lane's distance from the
    // centre line (right-hand traffic).
    double halfWidth = 4.0;
    double laneOffset = 2.0;
    // The height of the lanes (cars' ride height).
    double height = 0.0;
    float speed = 10.0f;
};

class RoadNetwork
{
  public:
    uint32_t AddNode(const glm::dvec3 &position);
    // `control`: nullptr for a straight lane.
    uint32_t AddLane(
        uint32_t from, uint32_t to, float speed, const glm::dvec3 *control = nullptr, int32_t intersection = -1
    );
    void Clear();

    const std::vector<RoadNode> &GetNodes() const
    {
        return m_nodes;
    }
    const std::vector<RoadLane> &GetLanes() const
    {
        return m_lanes;
    }
    bool Empty() const
    {
        return m_lanes.empty();
    }

    // A point and the unit direction of travel `distance` metres along a lane (clamped).
    glm::dvec3 PointAt(uint32_t lane, double distance) const;
    glm::dvec3 DirectionAt(uint32_t lane, double distance) const;
    // Two lanes of one intersection whose paths come within 2.5 m, unless they start at the
    // same node. Computed per intersection the first time it is asked.
    bool Conflicts(uint32_t a, uint32_t b) const;
    // Lanes whose midpoint is within `radius` (on the ground plane) of `world`; a grid of
    // 64 m buckets is built on first use.
    void LanesNear(const glm::dvec3 &world, double radius, std::vector<uint32_t> &out) const;

    bool LoadJson(const nlohmann::json &document, std::string *error = nullptr);
    nlohmann::json ToJson() const;

    // A right-hand-traffic street grid: straight lanes between crossings and, inside each
    // crossing, straight, left and right connectors.
    static RoadNetwork Grid(const RoadGridDesc &desc);

  private:
    double Parameter(const RoadLane &lane, double distance) const;
    std::vector<RoadNode> m_nodes;
    std::vector<RoadLane> m_lanes;
    // Per intersection: its lanes and, once computed, which pairs conflict.
    std::map<int32_t, std::vector<uint32_t>> m_intersections;
    mutable std::map<std::pair<uint32_t, uint32_t>, bool> m_conflicts;
    mutable std::map<int32_t, bool> m_computed;
    mutable std::map<std::pair<int64_t, int64_t>, std::vector<uint32_t>> m_buckets;
};

} // namespace Geni
