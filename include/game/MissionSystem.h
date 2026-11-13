#pragma once

// Missions made of ordered objectives (docs/design/0024).

#include <glm/ext/vector_double3.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace Geni
{

class FileSystem;
class Scene;

enum class MissionState
{
    Locked,
    Available,
    Active,
    Completed,
    Failed
};

enum class ObjectiveKind
{
    // The player comes within `radius` of a world position or a named object.
    Reach,
    // AddProgress reaches `count`.
    Count,
    // The game completes it (CompleteObjective).
    Custom
};

struct ObjectiveDef
{
    std::string id;
    std::string text;
    ObjectiveKind kind = ObjectiveKind::Custom;
    // Reach: a world position, or an object's name (looked up in the scene).
    glm::dvec3 position{0.0};
    std::string object;
    float radius = 5.0f;
    // Count.
    int count = 1;
};

struct MissionDef
{
    std::string id;
    std::string title;
    std::string description;
    std::vector<std::string>
        requires;
    // Seconds; 0: none.
    float timeLimit = 0.0f;
    std::vector<ObjectiveDef> objectives;
};

struct MissionStatus
{
    MissionState state = MissionState::Locked;
    // Index of the current objective while active (objectives.size() when completed).
    size_t current = 0;
    // Progress of each objective (Count; 1 for a completed Reach or Custom).
    std::vector<int> progress;
    float elapsed = 0.0f;
    std::string failReason;
};

struct MissionEvent
{
    enum class Type
    {
        Unlocked,
        Started,
        ObjectiveCompleted,
        Completed,
        Failed
    };
    Type type = Type::Started;
    std::string mission;
    // For ObjectiveCompleted.
    std::string objective;
    // For Failed.
    std::string reason;
};

const char *MissionStateName(MissionState state);
const char *MissionEventName(MissionEvent::Type type);

class MissionSystem
{
  public:
    // Adds the missions of a "geni.mission" document. A mission whose id exists is
    // replaced (its status is kept when its objectives still fit).
    bool LoadJson(const nlohmann::json &document, std::string *error = nullptr);
    // Reads `path` (relative to the assets folder) and loads it.
    bool LoadFile(FileSystem &files, const std::string &path, std::string *error = nullptr);
    void Clear();

    const MissionDef *Find(const std::string &id) const;
    const MissionStatus *GetStatus(const std::string &id) const;
    MissionState GetState(const std::string &id) const;
    // The current objective of an active mission, or nullptr.
    const ObjectiveDef *GetCurrentObjective(const std::string &id) const;
    std::vector<std::string> GetMissionIds() const;
    std::vector<std::string> GetActive() const;

    // Available or failed missions become active at their first objective.
    bool Start(const std::string &id);
    bool Fail(const std::string &id, const std::string &reason);
    // An active or failed mission goes back to available, its progress cleared.
    bool Abandon(const std::string &id);
    // The current objective must be the one named (or `objective` empty: the current one).
    bool AddProgress(const std::string &id, const std::string &objective, int amount = 1);
    bool CompleteObjective(const std::string &id, const std::string &objective = std::string());

    // The object whose position Reach objectives check (its scene ID; 0: none).
    void SetPlayer(uint64_t objectId)
    {
        m_player = objectId;
    }
    uint64_t GetPlayer() const
    {
        return m_player;
    }
    // Reach checks against `scene` (may be null) and time limits. Engine calls it once per
    // frame after Application::Update.
    void Update(float deltaTime, Scene *scene);

    // Events since the last call, oldest first.
    std::vector<MissionEvent> TakeEvents();

    nlohmann::json SaveState() const;
    // Applies a SaveState document to the loaded missions (unknown ids are ignored).
    bool RestoreState(const nlohmann::json &state, std::string *error = nullptr);

  private:
    struct Entry
    {
        MissionDef def;
        MissionStatus status;
    };
    Entry *FindEntry(const std::string &id);
    const Entry *FindEntry(const std::string &id) const;
    void Advance(Entry &entry);
    void UpdateLocks();
    void Push(
        MissionEvent::Type type, const std::string &mission, const std::string &objective = std::string(),
        const std::string &reason = std::string()
    );

    std::vector<Entry> m_missions;
    std::vector<MissionEvent> m_events;
    uint64_t m_player = 0;
};

} // namespace Geni
