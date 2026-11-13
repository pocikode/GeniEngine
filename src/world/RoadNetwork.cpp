#include "world/RoadNetwork.h"

#include <glm/ext/vector_double2.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

namespace Geni
{

namespace
{

glm::dvec3 Bezier(const glm::dvec3 &a, const glm::dvec3 &p, const glm::dvec3 &b, double t)
{
    const double u = 1.0 - t;
    return a * (u * u) + p * (2.0 * u * t) + b * (t * t);
}

glm::dvec3 BezierTangent(const glm::dvec3 &a, const glm::dvec3 &p, const glm::dvec3 &b, double t)
{
    return (p - a) * (2.0 * (1.0 - t)) + (b - p) * (2.0 * t);
}

constexpr int kSamples = 16;
constexpr double kConflictDistance = 2.5;

} // namespace

uint32_t RoadNetwork::AddNode(const glm::dvec3 &position)
{
    m_nodes.push_back({position, {}});
    return static_cast<uint32_t>(m_nodes.size() - 1);
}

uint32_t RoadNetwork::AddLane(uint32_t from, uint32_t to, float speed, const glm::dvec3 *control, int32_t intersection)
{
    RoadLane lane;
    lane.from = from;
    lane.to = to;
    lane.speed = speed;
    lane.intersection = intersection;
    const glm::dvec3 a = m_nodes[from].position;
    const glm::dvec3 b = m_nodes[to].position;
    if (control)
    {
        lane.curved = true;
        lane.control = *control;
        double length = 0.0;
        glm::dvec3 previous = a;
        for (int i = 1; i <= kSamples; ++i)
        {
            const glm::dvec3 point = Bezier(a, lane.control, b, static_cast<double>(i) / kSamples);
            length += glm::distance(previous, point);
            previous = point;
        }
        lane.length = static_cast<float>(length);
    }
    else
    {
        lane.length = static_cast<float>(glm::distance(a, b));
    }
    const uint32_t index = static_cast<uint32_t>(m_lanes.size());
    m_lanes.push_back(lane);
    m_nodes[from].out.push_back(index);
    if (intersection >= 0)
    {
        m_intersections[intersection].push_back(index);
    }
    return index;
}

void RoadNetwork::Clear()
{
    m_nodes.clear();
    m_lanes.clear();
    m_intersections.clear();
    m_conflicts.clear();
    m_computed.clear();
    m_buckets.clear();
}

double RoadNetwork::Parameter(const RoadLane &lane, double distance) const
{
    return lane.length > 0.0f ? std::clamp(distance / lane.length, 0.0, 1.0) : 0.0;
}

glm::dvec3 RoadNetwork::PointAt(uint32_t index, double distance) const
{
    const RoadLane &lane = m_lanes[index];
    const glm::dvec3 a = m_nodes[lane.from].position;
    const glm::dvec3 b = m_nodes[lane.to].position;
    const double t = Parameter(lane, distance);
    return lane.curved ? Bezier(a, lane.control, b, t) : a + (b - a) * t;
}

glm::dvec3 RoadNetwork::DirectionAt(uint32_t index, double distance) const
{
    const RoadLane &lane = m_lanes[index];
    const glm::dvec3 a = m_nodes[lane.from].position;
    const glm::dvec3 b = m_nodes[lane.to].position;
    glm::dvec3 d = lane.curved ? BezierTangent(a, lane.control, b, Parameter(lane, distance)) : b - a;
    const double length = glm::length(d);
    return length > 1e-9 ? d / length : glm::dvec3(0.0, 0.0, -1.0);
}

bool RoadNetwork::Conflicts(uint32_t a, uint32_t b) const
{
    if (a == b || a >= m_lanes.size() || b >= m_lanes.size())
    {
        return false;
    }
    const int32_t intersection = m_lanes[a].intersection;
    if (intersection < 0 || intersection != m_lanes[b].intersection)
    {
        return false;
    }
    if (!m_computed[intersection])
    {
        m_computed[intersection] = true;
        const auto &lanes = m_intersections.at(intersection);
        for (size_t i = 0; i < lanes.size(); ++i)
        {
            for (size_t j = i + 1; j < lanes.size(); ++j)
            {
                const RoadLane &x = m_lanes[lanes[i]];
                const RoadLane &y = m_lanes[lanes[j]];
                bool close = false;
                if (x.from != y.from)
                {
                    for (int s = 0; s <= 8 && !close; ++s)
                    {
                        const glm::dvec3 p = PointAt(lanes[i], x.length * s / 8.0);
                        for (int r = 0; r <= 8 && !close; ++r)
                        {
                            close = glm::distance(p, PointAt(lanes[j], y.length * r / 8.0)) < kConflictDistance;
                        }
                    }
                }
                m_conflicts[{std::min(lanes[i], lanes[j]), std::max(lanes[i], lanes[j])}] = close;
            }
        }
    }
    const auto it = m_conflicts.find({std::min(a, b), std::max(a, b)});
    return it != m_conflicts.end() && it->second;
}

void RoadNetwork::LanesNear(const glm::dvec3 &world, double radius, std::vector<uint32_t> &out) const
{
    constexpr double kBucket = 64.0;
    if (m_buckets.empty() && !m_lanes.empty())
    {
        for (uint32_t i = 0; i < m_lanes.size(); ++i)
        {
            const glm::dvec3 middle = PointAt(i, 0.5 * m_lanes[i].length);
            m_buckets[{static_cast<int64_t>(std::floor(middle.x / kBucket)),
                       static_cast<int64_t>(std::floor(middle.z / kBucket))}]
                .push_back(i);
        }
    }
    out.clear();
    const int64_t x0 = static_cast<int64_t>(std::floor((world.x - radius) / kBucket));
    const int64_t x1 = static_cast<int64_t>(std::floor((world.x + radius) / kBucket));
    const int64_t z0 = static_cast<int64_t>(std::floor((world.z - radius) / kBucket));
    const int64_t z1 = static_cast<int64_t>(std::floor((world.z + radius) / kBucket));
    for (int64_t z = z0; z <= z1; ++z)
    {
        for (int64_t x = x0; x <= x1; ++x)
        {
            const auto it = m_buckets.find({x, z});
            if (it == m_buckets.end())
            {
                continue;
            }
            for (uint32_t lane : it->second)
            {
                const glm::dvec3 middle = PointAt(lane, 0.5 * m_lanes[lane].length);
                if (glm::length(glm::dvec2(middle.x - world.x, middle.z - world.z)) <= radius)
                {
                    out.push_back(lane);
                }
            }
        }
    }
}

bool RoadNetwork::LoadJson(const nlohmann::json &document, std::string *error)
{
    RoadNetwork loaded;
    try
    {
        if (document.value("format", std::string()) != "geni.roads" || document.value("version", 0) != 1)
        {
            if (error)
            {
                *error = "not a version 1 geni.roads document";
            }
            return false;
        }
        for (const auto &node : document.at("nodes"))
        {
            loaded.AddNode(glm::dvec3(node.at(0).get<double>(), node.at(1).get<double>(), node.at(2).get<double>()));
        }
        for (const auto &lane : document.at("lanes"))
        {
            const uint32_t from = lane.at("from").get<uint32_t>();
            const uint32_t to = lane.at("to").get<uint32_t>();
            if (from >= loaded.m_nodes.size() || to >= loaded.m_nodes.size() || from == to)
            {
                if (error)
                {
                    *error = "a lane refers to a missing node";
                }
                return false;
            }
            glm::dvec3 control(0.0);
            const bool curved = lane.contains("control");
            if (curved)
            {
                const auto &c = lane.at("control");
                control = glm::dvec3(c.at(0).get<double>(), c.at(1).get<double>(), c.at(2).get<double>());
            }
            loaded.AddLane(
                from, to, lane.value("speed", 10.0f), curved ? &control : nullptr, lane.value("intersection", -1)
            );
        }
    }
    catch (const std::exception &e)
    {
        if (error)
        {
            *error = std::string("malformed road network: ") + e.what();
        }
        return false;
    }
    *this = std::move(loaded);
    return true;
}

nlohmann::json RoadNetwork::ToJson() const
{
    nlohmann::json nodes = nlohmann::json::array();
    for (const RoadNode &node : m_nodes)
    {
        nodes.push_back({node.position.x, node.position.y, node.position.z});
    }
    nlohmann::json lanes = nlohmann::json::array();
    for (const RoadLane &lane : m_lanes)
    {
        nlohmann::json item = {{"from", lane.from}, {"to", lane.to}, {"speed", lane.speed}};
        if (lane.curved)
        {
            item["control"] = {lane.control.x, lane.control.y, lane.control.z};
        }
        if (lane.intersection >= 0)
        {
            item["intersection"] = lane.intersection;
        }
        lanes.push_back(std::move(item));
    }
    return {{"format", "geni.roads"}, {"version", 1}, {"nodes", nodes}, {"lanes", lanes}};
}

RoadNetwork RoadNetwork::Grid(const RoadGridDesc &desc)
{
    RoadNetwork network;
    const int columns = std::max(1, desc.columns);
    const int rows = std::max(1, desc.rows);
    // Directions +X, -X, +Z, -Z and their right-hand sides (right of d is (-d.z, 0, d.x)).
    const glm::dvec3 directions[4] = {{1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}};
    auto right = [](const glm::dvec3 &d) { return glm::dvec3(-d.z, 0.0, d.x); };
    auto opposite = [](int d) { return d ^ 1; };
    auto crossing = [&](int i, int j) { return j * columns + i; };
    auto centre = [&](int i, int j) {
        return glm::dvec3(desc.originX + i * desc.spacing, desc.height, desc.originZ + j * desc.spacing);
    };
    auto neighbour = [&](int i, int j, int d, int &ni, int &nj) {
        ni = i + static_cast<int>(directions[d].x);
        nj = j + static_cast<int>(directions[d].z);
        return ni >= 0 && nj >= 0 && ni < columns && nj < rows;
    };

    // Per crossing: entry[4] then exit[4] nodes.
    std::vector<uint32_t> entry(static_cast<size_t>(columns) * rows * 4);
    std::vector<uint32_t> exit(entry.size());
    for (int j = 0; j < rows; ++j)
    {
        for (int i = 0; i < columns; ++i)
        {
            const glm::dvec3 c = centre(i, j);
            for (int d = 0; d < 4; ++d)
            {
                const glm::dvec3 side = right(directions[d]) * desc.laneOffset;
                entry[crossing(i, j) * 4 + d] = network.AddNode(c - directions[d] * desc.halfWidth + side);
                exit[crossing(i, j) * 4 + d] = network.AddNode(c + directions[d] * desc.halfWidth + side);
            }
        }
    }
    // Streets: from a crossing's exit to the next crossing's entry, same direction.
    for (int j = 0; j < rows; ++j)
    {
        for (int i = 0; i < columns; ++i)
        {
            for (int d = 0; d < 4; ++d)
            {
                int ni = 0;
                int nj = 0;
                if (neighbour(i, j, d, ni, nj))
                {
                    network.AddLane(exit[crossing(i, j) * 4 + d], entry[crossing(ni, nj) * 4 + d], desc.speed);
                }
            }
        }
    }
    // Inside each crossing: from each entry that has a street arriving (the neighbour
    // behind exists) to each exit that has a street leaving, no U-turns.
    for (int j = 0; j < rows; ++j)
    {
        for (int i = 0; i < columns; ++i)
        {
            const int id = crossing(i, j);
            const glm::dvec3 c = centre(i, j);
            for (int d = 0; d < 4; ++d)
            {
                int bi = 0;
                int bj = 0;
                if (!neighbour(i, j, opposite(d), bi, bj))
                {
                    continue;
                }
                for (int e = 0; e < 4; ++e)
                {
                    int ni = 0;
                    int nj = 0;
                    if (e == opposite(d) || !neighbour(i, j, e, ni, nj))
                    {
                        continue;
                    }
                    const uint32_t from = entry[id * 4 + d];
                    const uint32_t to = exit[id * 4 + e];
                    if (e == d)
                    {
                        network.AddLane(from, to, desc.speed, nullptr, id);
                    }
                    else
                    {
                        // The corner where the two lanes' lines meet.
                        const glm::dvec3 corner =
                            c + right(directions[d]) * desc.laneOffset + right(directions[e]) * desc.laneOffset;
                        network.AddLane(from, to, std::min(desc.speed, 5.5f), &corner, id);
                    }
                }
            }
        }
    }
    return network;
}

} // namespace Geni
