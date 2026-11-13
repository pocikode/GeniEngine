#include "game/TrafficSystem.h"

#include "core/Engine.h"
#include "core/Log.h"
#include "physics/PhysicsManager.h"
#include "physics/RigidBody.h"
#include "scene/GameObject.h"
#include "scene/Scene.h"
#include "scene/SceneSerializer.h"
#include "scene/components/PhysicsComponent.h"

#include <glm/ext/vector_double2.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>

namespace Geni
{

namespace
{

constexpr double kMinGap = 4.0;        // metres kept to the car ahead when stopped
constexpr double kHeadway = 1.2;       // seconds of following distance
constexpr double kReserveAhead = 14.0; // claim the intersection this far before it
constexpr float kAccelerate = 3.0f;
constexpr float kBrake = 9.0f;

} // namespace

struct TrafficSystem::Car
{
    uint64_t object = 0;
    uint32_t bodyId = 0xffffffffu;
    uint32_t lane = 0;
    double distance = 0.0;
    float speed = 0.0f;
    float cruise = 9.0f;
    // The lane after this one (chosen before the end), and whether the car may enter it.
    int64_t next = -1;
    bool reserved = false;
    double driven = 0.0;
    float blocked = 0.0f;
};

TrafficSystem::TrafficSystem() = default;

TrafficSystem::~TrafficSystem() = default;

float TrafficSystem::Random01()
{
    m_random ^= m_random << 13;
    m_random ^= m_random >> 17;
    m_random ^= m_random << 5;
    return static_cast<float>(m_random >> 8) * (1.0f / 16777216.0f);
}

void TrafficSystem::Start(RoadNetwork network, const TrafficConfig &config)
{
    Stop();
    m_network = std::move(network);
    m_config = config;
    m_random = config.seed ? config.seed * 2654435761u : 0x9E3779B9u;
    m_running = !m_network.Empty() && !config.models.empty();
    m_spawnTimer = 0.0f;
}

void TrafficSystem::Stop()
{
    if (m_scene)
    {
        for (const auto &car : m_cars)
        {
            if (GameObject *object = m_scene->FindObjectByID(car->object))
            {
                object->MarkForDestroy();
            }
        }
    }
    m_cars.clear();
    m_keepClear.clear();
    m_running = false;
    m_spawned = 0;
    m_despawned = 0;
    m_drivenByRemoved = 0.0;
}

void TrafficSystem::KeepClear(const glm::dvec3 &world, float radius)
{
    m_keepClear.push_back({world, radius});
}

bool TrafficSystem::FocusPosition(Scene &scene, glm::dvec3 &world) const
{
    if (m_focus)
    {
        if (GameObject *object = scene.FindObjectByID(m_focus))
        {
            world = scene.ToWorld(object->GetWorldPosition());
            return true;
        }
    }
    glm::mat4 view(1.0f);
    if (!Engine::GetInstance().GetActiveView(view))
    {
        return false;
    }
    world = scene.ToWorld(glm::vec3(view[3]));
    return true;
}

bool TrafficSystem::LaneOccupiedFor(uint32_t lane, const Car *except) const
{
    // Occupied (or claimed) by a car on a lane that conflicts with `lane`.
    for (const auto &car : m_cars)
    {
        if (car.get() == except)
        {
            continue;
        }
        if (m_network.Conflicts(lane, car->lane) ||
            (car->reserved && car->next >= 0 && m_network.Conflicts(lane, static_cast<uint32_t>(car->next))))
        {
            return true;
        }
    }
    return false;
}

void TrafficSystem::TrySpawn(Scene &scene, const glm::dvec3 &focus)
{
    const auto &lanes = m_network.GetLanes();
    std::vector<uint32_t> nearby;
    m_network.LanesNear(focus, m_config.radius, nearby);
    if (nearby.empty())
    {
        return;
    }
    // A few random street lanes in range; the first that fits wins.
    for (int attempt = 0; attempt < 12; ++attempt)
    {
        const uint32_t lane =
            nearby[static_cast<size_t>(Random01() * static_cast<float>(nearby.size())) % nearby.size()];
        if (lanes[lane].intersection >= 0 || lanes[lane].length < 8.0f)
        {
            continue;
        }
        const double distance = 0.5 * lanes[lane].length;
        const glm::dvec3 position = m_network.PointAt(lane, distance);
        const double away = glm::length(glm::dvec2(position.x - focus.x, position.z - focus.z));
        if (away < 40.0 || away > m_config.radius)
        {
            continue;
        }
        bool clear = true;
        for (const auto &[centre, radius] : m_keepClear)
        {
            clear = clear && glm::distance(glm::dvec2(centre.x, centre.z), glm::dvec2(position.x, position.z)) > radius;
        }
        for (const auto &car : m_cars)
        {
            clear = clear && glm::distance(m_network.PointAt(car->lane, car->distance), position) > 14.0;
        }
        if (!clear)
        {
            continue;
        }

        const std::string &model = m_config.models[static_cast<size_t>(m_spawned) % m_config.models.size()];
        std::string error;
        GameObject *object = SceneSerializer::Spawn(scene, model, scene.ToScene(position), 0.0f, &error);
        if (!object)
        {
            GENI_LOG_WARN("traffic: %s", error.c_str());
            m_running = false; // a broken model would fail every time
            return;
        }
        ++m_spawned;
        object->SetName("Traffic" + std::to_string(m_spawned));
        auto *physics = new PhysicsComponent();
        physics->SetBodyType(BodyType::Kinematic);
        physics->SetColliderType(ColliderType::Box);
        physics->SetSize(m_config.bodySize);
        object->AddComponent(physics);

        auto car = std::make_unique<Car>();
        car->object = object->GetID();
        car->bodyId = physics->GetRigidBody() ? physics->GetRigidBody()->GetBodyId() : 0xffffffffu;
        car->lane = lane;
        car->distance = distance;
        car->cruise = m_config.cruiseMin + (m_config.cruiseMax - m_config.cruiseMin) * Random01();
        car->speed = car->cruise * 0.5f;
        Place(scene, *car);
        m_cars.push_back(std::move(car));
        return;
    }
}

void TrafficSystem::Place(Scene &scene, Car &car)
{
    GameObject *object = scene.FindObjectByID(car.object);
    if (!object)
    {
        return;
    }
    const glm::dvec3 position = m_network.PointAt(car.lane, car.distance);
    const glm::dvec3 direction = m_network.DirectionAt(car.lane, car.distance);
    object->SetPosition(scene.ToScene(position));
    object->SetRotation(glm::quatLookAtRH(glm::normalize(glm::vec3(direction)), glm::vec3(0.0f, 1.0f, 0.0f)));
}

void TrafficSystem::Drive(Scene &scene, Car &car, float deltaTime)
{
    const RoadLane &lane = m_network.GetLanes()[car.lane];
    const double remaining = lane.length - car.distance;

    // Choose the next lane well before the end.
    if (car.next < 0)
    {
        const auto &out = m_network.GetNodes()[lane.to].out;
        if (!out.empty())
        {
            car.next = out[static_cast<size_t>(Random01() * static_cast<float>(out.size())) % out.size()];
        }
    }

    double target = std::min(car.cruise, lane.speed);
    // The car ahead: on this lane, else on the next one.
    double gap = 1e9;
    for (const auto &other : m_cars)
    {
        if (other.get() == &car)
        {
            continue;
        }
        if (other->lane == car.lane && other->distance > car.distance)
        {
            gap = std::min(gap, other->distance - car.distance);
        }
        else if (car.next >= 0 && other->lane == static_cast<uint32_t>(car.next))
        {
            gap = std::min(gap, remaining + other->distance);
        }
    }
    // Entering an intersection: claim the next lane when nothing conflicting is on or
    // claiming it; otherwise stop at the end of this lane.
    if (car.next >= 0 && !car.reserved)
    {
        const RoadLane &next = m_network.GetLanes()[static_cast<size_t>(car.next)];
        if (next.intersection >= 0 && remaining < kReserveAhead)
        {
            if (LaneOccupiedFor(static_cast<uint32_t>(car.next), &car))
            {
                gap = std::min(gap, remaining + kMinGap - 0.5);
            }
            else
            {
                car.reserved = true;
            }
        }
        else if (next.intersection < 0)
        {
            car.reserved = true;
        }
    }
    if (gap < 1e8)
    {
        target = std::min(target, std::max(0.0, (gap - kMinGap) / kHeadway));
    }
    // Anything else in front (the player, the player's car, pedestrians).
    const glm::dvec3 forward = m_network.DirectionAt(car.lane, car.distance);
    const glm::dvec3 nose = m_network.PointAt(car.lane, car.distance) + forward * 2.4;
    PhysicsHit hit;
    PhysicsManager &physics = Engine::GetInstance().GetPhysicsManager();
    if (physics.IsInitialized() &&
        physics.CastRay(nose, glm::vec3(forward), 11.0f, hit, PhysicsLayerMoving, car.bodyId))
    {
        bool traffic = false;
        for (const auto &other : m_cars)
        {
            traffic = traffic || other->object == hit.userData;
        }
        if (!traffic && car.blocked < 6.0f)
        {
            target = std::min(target, hit.distance < 4.0f ? 0.0 : car.cruise * (hit.distance - 4.0) / 7.0);
        }
    }
    car.blocked = car.speed < 0.3f && target < 0.3 ? car.blocked + deltaTime : 0.0f;

    const float rate = target > car.speed ? kAccelerate : kBrake;
    car.speed += std::clamp(static_cast<float>(target) - car.speed, -rate * deltaTime, rate * deltaTime);
    car.speed = std::max(0.0f, car.speed);
    double step = car.speed * deltaTime;
    car.driven += step;
    // Along the lane and on into the next while the step lasts.
    while (step > 0.0)
    {
        const double left = m_network.GetLanes()[car.lane].length - car.distance;
        if (step < left)
        {
            car.distance += step;
            break;
        }
        if (car.next < 0 || !car.reserved)
        {
            // Nowhere to go (a dead end) or waiting at the line.
            car.distance = m_network.GetLanes()[car.lane].length;
            car.speed = car.next < 0 ? 0.0f : car.speed;
            break;
        }
        step -= left;
        car.lane = static_cast<uint32_t>(car.next);
        car.distance = 0.0;
        car.next = -1;
        car.reserved = false;
    }
    Place(scene, car);
}

void TrafficSystem::Update(float deltaTime, Scene *scene)
{
    if (scene != m_scene)
    {
        // A new scene: the old cars went with the old one.
        m_cars.clear();
        m_scene = scene;
    }
    if (!m_running || !scene)
    {
        return;
    }
    glm::dvec3 focus(0.0);
    if (!FocusPosition(*scene, focus))
    {
        return;
    }
    // Drop cars that are gone or too far, and stuck ones at dead ends.
    for (size_t i = 0; i < m_cars.size();)
    {
        Car &car = *m_cars[i];
        GameObject *object = scene->FindObjectByID(car.object);
        const glm::dvec3 position = m_network.PointAt(car.lane, car.distance);
        const double away = glm::length(glm::dvec2(position.x - focus.x, position.z - focus.z));
        const bool deadEnd =
            car.next < 0 && car.speed == 0.0f && car.distance >= m_network.GetLanes()[car.lane].length - 1e-6;
        if (!object || away > 1.6 * m_config.radius || deadEnd)
        {
            if (object)
            {
                object->MarkForDestroy();
            }
            m_drivenByRemoved += car.driven;
            ++m_despawned;
            m_cars.erase(m_cars.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
        }
        ++i;
    }
    for (auto &car : m_cars)
    {
        Drive(*scene, *car, deltaTime);
    }
    m_spawnTimer -= deltaTime;
    if (static_cast<int>(m_cars.size()) < m_config.count && m_spawnTimer <= 0.0f)
    {
        m_spawnTimer = m_config.spawnInterval;
        TrySpawn(*scene, focus);
    }
}

TrafficStats TrafficSystem::GetStats() const
{
    TrafficStats stats;
    stats.cars = static_cast<int>(m_cars.size());
    stats.driven = m_drivenByRemoved;
    for (const auto &car : m_cars)
    {
        stats.driven += car->driven;
    }
    stats.spawned = m_spawned;
    stats.despawned = m_despawned;
    return stats;
}

std::vector<GameObject *> TrafficSystem::GetCars(Scene &scene) const
{
    std::vector<GameObject *> objects;
    for (const auto &car : m_cars)
    {
        if (GameObject *object = scene.FindObjectByID(car->object))
        {
            objects.push_back(object);
        }
    }
    return objects;
}

} // namespace Geni
