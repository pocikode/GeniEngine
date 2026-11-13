#pragma once

// Cars driving a road network around the player (docs/design/0025).

#include "world/RoadNetwork.h"

#include <glm/ext/vector_double3.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Geni
{

class GameObject;
class Scene;

struct TrafficConfig
{
    // Cooked models (.gltf) or prefabs, spawned in turn.
    std::vector<std::string> models;
    int count = 10;
    // Cars live within this distance of the focus.
    float radius = 140.0f;
    float cruiseMin = 8.0f;
    float cruiseMax = 11.0f;
    // The car body for collisions (a kinematic box, metres).
    glm::vec3 bodySize{1.8f, 1.3f, 4.2f};
    // Seconds between spawn attempts while below `count`.
    float spawnInterval = 0.25f;
    uint32_t seed = 1;
};

struct TrafficStats
{
    int cars = 0;
    double driven = 0.0;
    int spawned = 0;
    int despawned = 0;
};

class TrafficSystem
{
  public:
    TrafficSystem();
    ~TrafficSystem();

    // Takes the network; cars appear from the next Update.
    void Start(RoadNetwork network, const TrafficConfig &config);
    // Removes every car.
    void Stop();
    bool IsRunning() const
    {
        return m_running;
    }
    const RoadNetwork &GetNetwork() const
    {
        return m_network;
    }

    // Cars spawn and despawn around this object (0: the active view).
    void SetFocus(uint64_t objectId)
    {
        m_focus = objectId;
    }
    // No car spawns within `radius` of this world position (a street a script needs).
    void KeepClear(const glm::dvec3 &world, float radius);

    // Engine calls it once per frame after Application::Update.
    void Update(float deltaTime, Scene *scene);

    TrafficStats GetStats() const;
    // The objects of the cars (for tests and tools).
    std::vector<GameObject *> GetCars(Scene &scene) const;

  private:
    struct Car;
    bool FocusPosition(Scene &scene, glm::dvec3 &world) const;
    void TrySpawn(Scene &scene, const glm::dvec3 &focus);
    void Drive(Scene &scene, Car &car, float deltaTime);
    void Place(Scene &scene, Car &car);
    bool LaneOccupiedFor(uint32_t lane, const Car *except) const;
    float Random01();

    RoadNetwork m_network;
    TrafficConfig m_config;
    std::vector<std::unique_ptr<Car>> m_cars;
    std::vector<std::pair<glm::dvec3, float>> m_keepClear;
    uint64_t m_focus = 0;
    bool m_running = false;
    float m_spawnTimer = 0.0f;
    uint32_t m_random = 1;
    int m_spawned = 0;
    int m_despawned = 0;
    double m_drivenByRemoved = 0.0;
    Scene *m_scene = nullptr;
};

} // namespace Geni
