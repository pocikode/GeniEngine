#include "game/MissionSystem.h"

#include "io/FileSystem.h"
#include "scene/GameObject.h"
#include "scene/Scene.h"

#include <glm/geometric.hpp>

#include <algorithm>

namespace Geni
{

namespace
{

bool SetError(std::string *error, const std::string &message)
{
    if (error)
    {
        *error = message;
    }
    return false;
}

MissionState StateFromName(const std::string &name)
{
    for (MissionState state :
         {MissionState::Locked, MissionState::Available, MissionState::Active, MissionState::Completed,
          MissionState::Failed})
    {
        if (name == MissionStateName(state))
        {
            return state;
        }
    }
    return MissionState::Available;
}

} // namespace

const char *MissionStateName(MissionState state)
{
    switch (state)
    {
    case MissionState::Locked:
        return "locked";
    case MissionState::Available:
        return "available";
    case MissionState::Active:
        return "active";
    case MissionState::Completed:
        return "completed";
    case MissionState::Failed:
        return "failed";
    }
    return "locked";
}

const char *MissionEventName(MissionEvent::Type type)
{
    switch (type)
    {
    case MissionEvent::Type::Unlocked:
        return "unlocked";
    case MissionEvent::Type::Started:
        return "started";
    case MissionEvent::Type::ObjectiveCompleted:
        return "objective";
    case MissionEvent::Type::Completed:
        return "completed";
    case MissionEvent::Type::Failed:
        return "failed";
    }
    return "started";
}

bool MissionSystem::LoadJson(const nlohmann::json &document, std::string *error)
{
    std::vector<MissionDef> loaded;
    try
    {
        if (document.value("format", std::string()) != "geni.mission" || document.value("version", 0) != 1)
        {
            return SetError(error, "not a version 1 geni.mission document");
        }
        for (const auto &item : document.at("missions"))
        {
            MissionDef mission;
            mission.id = item.at("id").get<std::string>();
            mission.title = item.value("title", mission.id);
            mission.description = item.value("description", std::string());
            mission.timeLimit = std::max(0.0f, item.value("timeLimit", 0.0f));
            for (const auto &required : item.value("requires", nlohmann::json::array()))
            {
                mission.
                    requires
                    .push_back(required.get<std::string>());
            }
            for (const auto &entry : item.at("objectives"))
            {
                ObjectiveDef objective;
                objective.id = entry.at("id").get<std::string>();
                objective.text = entry.value("text", objective.id);
                const std::string kind = entry.value("kind", std::string("custom"));
                if (kind == "reach")
                {
                    objective.kind = ObjectiveKind::Reach;
                    objective.radius = std::max(0.1f, entry.value("radius", objective.radius));
                    if (entry.contains("position"))
                    {
                        const auto &p = entry.at("position");
                        objective.position =
                            glm::dvec3(p.at(0).get<double>(), p.at(1).get<double>(), p.at(2).get<double>());
                    }
                    else
                    {
                        objective.object = entry.at("object").get<std::string>();
                    }
                }
                else if (kind == "count")
                {
                    objective.kind = ObjectiveKind::Count;
                    objective.count = std::max(1, entry.value("count", 1));
                }
                else if (kind != "custom")
                {
                    return SetError(error, "mission " + mission.id + ": unknown objective kind '" + kind + "'");
                }
                mission.objectives.push_back(std::move(objective));
            }
            if (mission.objectives.empty())
            {
                return SetError(error, "mission " + mission.id + " has no objectives");
            }
            loaded.push_back(std::move(mission));
        }
    }
    catch (const std::exception &e)
    {
        return SetError(error, std::string("malformed mission file: ") + e.what());
    }
    for (MissionDef &mission : loaded)
    {
        Entry *existing = FindEntry(mission.id);
        if (existing)
        {
            const bool fits = existing->def.objectives.size() == mission.objectives.size();
            existing->def = std::move(mission);
            if (!fits)
            {
                existing->status = MissionStatus();
                existing->status.progress.assign(existing->def.objectives.size(), 0);
            }
        }
        else
        {
            Entry entry;
            entry.def = std::move(mission);
            entry.status.progress.assign(entry.def.objectives.size(), 0);
            m_missions.push_back(std::move(entry));
        }
    }
    UpdateLocks();
    return true;
}

bool MissionSystem::LoadFile(FileSystem &files, const std::string &path, std::string *error)
{
    const std::string text = files.LoadAssetFileText(path);
    if (text.empty())
    {
        return SetError(error, "cannot read " + path);
    }
    const nlohmann::json document = nlohmann::json::parse(text, nullptr, false);
    if (document.is_discarded())
    {
        return SetError(error, path + " is not JSON");
    }
    return LoadJson(document, error);
}

void MissionSystem::Clear()
{
    m_missions.clear();
    m_events.clear();
}

MissionSystem::Entry *MissionSystem::FindEntry(const std::string &id)
{
    for (Entry &entry : m_missions)
    {
        if (entry.def.id == id)
        {
            return &entry;
        }
    }
    return nullptr;
}

const MissionSystem::Entry *MissionSystem::FindEntry(const std::string &id) const
{
    return const_cast<MissionSystem *>(this)->FindEntry(id);
}

const MissionDef *MissionSystem::Find(const std::string &id) const
{
    const Entry *entry = FindEntry(id);
    return entry ? &entry->def : nullptr;
}

const MissionStatus *MissionSystem::GetStatus(const std::string &id) const
{
    const Entry *entry = FindEntry(id);
    return entry ? &entry->status : nullptr;
}

MissionState MissionSystem::GetState(const std::string &id) const
{
    const Entry *entry = FindEntry(id);
    return entry ? entry->status.state : MissionState::Locked;
}

const ObjectiveDef *MissionSystem::GetCurrentObjective(const std::string &id) const
{
    const Entry *entry = FindEntry(id);
    if (!entry || entry->status.state != MissionState::Active || entry->status.current >= entry->def.objectives.size())
    {
        return nullptr;
    }
    return &entry->def.objectives[entry->status.current];
}

std::vector<std::string> MissionSystem::GetMissionIds() const
{
    std::vector<std::string> ids;
    for (const Entry &entry : m_missions)
    {
        ids.push_back(entry.def.id);
    }
    return ids;
}

std::vector<std::string> MissionSystem::GetActive() const
{
    std::vector<std::string> ids;
    for (const Entry &entry : m_missions)
    {
        if (entry.status.state == MissionState::Active)
        {
            ids.push_back(entry.def.id);
        }
    }
    return ids;
}

void MissionSystem::Push(
    MissionEvent::Type type, const std::string &mission, const std::string &objective, const std::string &reason
)
{
    m_events.push_back({type, mission, objective, reason});
}

void MissionSystem::UpdateLocks()
{
    // Repeat until stable: unlocking one mission can satisfy another's requirement only
    // once it completes, so one pass is enough today, but the loop keeps it correct.
    for (Entry &entry : m_missions)
    {
        if (entry.status.state != MissionState::Locked && entry.status.state != MissionState::Available)
        {
            continue;
        }
        bool open = true;
        for (const std::string &required : entry.def.requires)
        {
            const Entry *other = FindEntry(required);
            open = open && other && other->status.state == MissionState::Completed;
        }
        const MissionState wanted = open ? MissionState::Available : MissionState::Locked;
        if (wanted != entry.status.state)
        {
            entry.status.state = wanted;
            if (open)
            {
                Push(MissionEvent::Type::Unlocked, entry.def.id);
            }
        }
    }
}

bool MissionSystem::Start(const std::string &id)
{
    Entry *entry = FindEntry(id);
    if (!entry || (entry->status.state != MissionState::Available && entry->status.state != MissionState::Failed))
    {
        return false;
    }
    entry->status = MissionStatus();
    entry->status.state = MissionState::Active;
    entry->status.progress.assign(entry->def.objectives.size(), 0);
    Push(MissionEvent::Type::Started, id);
    return true;
}

bool MissionSystem::Fail(const std::string &id, const std::string &reason)
{
    Entry *entry = FindEntry(id);
    if (!entry || entry->status.state != MissionState::Active)
    {
        return false;
    }
    entry->status.state = MissionState::Failed;
    entry->status.failReason = reason;
    Push(MissionEvent::Type::Failed, id, std::string(), reason);
    return true;
}

bool MissionSystem::Abandon(const std::string &id)
{
    Entry *entry = FindEntry(id);
    if (!entry || (entry->status.state != MissionState::Active && entry->status.state != MissionState::Failed))
    {
        return false;
    }
    entry->status = MissionStatus();
    entry->status.state = MissionState::Available;
    entry->status.progress.assign(entry->def.objectives.size(), 0);
    return true;
}

void MissionSystem::Advance(Entry &entry)
{
    const ObjectiveDef &done = entry.def.objectives[entry.status.current];
    entry.status.progress[entry.status.current] = done.kind == ObjectiveKind::Count ? done.count : 1;
    Push(MissionEvent::Type::ObjectiveCompleted, entry.def.id, done.id);
    ++entry.status.current;
    if (entry.status.current >= entry.def.objectives.size())
    {
        entry.status.state = MissionState::Completed;
        Push(MissionEvent::Type::Completed, entry.def.id);
        UpdateLocks();
    }
}

bool MissionSystem::AddProgress(const std::string &id, const std::string &objective, int amount)
{
    Entry *entry = FindEntry(id);
    const ObjectiveDef *current = GetCurrentObjective(id);
    if (!entry || !current || (!objective.empty() && current->id != objective) || current->kind != ObjectiveKind::Count)
    {
        return false;
    }
    int &progress = entry->status.progress[entry->status.current];
    progress = std::min(current->count, progress + std::max(0, amount));
    if (progress >= current->count)
    {
        Advance(*entry);
    }
    return true;
}

bool MissionSystem::CompleteObjective(const std::string &id, const std::string &objective)
{
    Entry *entry = FindEntry(id);
    const ObjectiveDef *current = GetCurrentObjective(id);
    if (!entry || !current || (!objective.empty() && current->id != objective))
    {
        return false;
    }
    Advance(*entry);
    return true;
}

void MissionSystem::Update(float deltaTime, Scene *scene)
{
    GameObject *player = scene && m_player ? scene->FindObjectByID(m_player) : nullptr;
    const glm::dvec3 playerWorld = player ? scene->ToWorld(player->GetWorldPosition()) : glm::dvec3(0.0);
    for (Entry &entry : m_missions)
    {
        if (entry.status.state != MissionState::Active)
        {
            continue;
        }
        entry.status.elapsed += std::max(0.0f, deltaTime);
        if (entry.def.timeLimit > 0.0f && entry.status.elapsed >= entry.def.timeLimit)
        {
            Fail(entry.def.id, "time");
            continue;
        }
        const ObjectiveDef &objective = entry.def.objectives[entry.status.current];
        if (objective.kind != ObjectiveKind::Reach || !player)
        {
            continue;
        }
        glm::dvec3 target = objective.position;
        if (!objective.object.empty())
        {
            GameObject *object = scene->FindObject(objective.object);
            if (!object)
            {
                continue;
            }
            target = scene->ToWorld(object->GetWorldPosition());
        }
        if (glm::distance(playerWorld, target) <= static_cast<double>(objective.radius))
        {
            Advance(entry);
        }
    }
}

std::vector<MissionEvent> MissionSystem::TakeEvents()
{
    std::vector<MissionEvent> events;
    events.swap(m_events);
    return events;
}

nlohmann::json MissionSystem::SaveState() const
{
    nlohmann::json missions = nlohmann::json::object();
    for (const Entry &entry : m_missions)
    {
        missions[entry.def.id] = {
            {"state", MissionStateName(entry.status.state)},
            {"current", entry.status.current},
            {"progress", entry.status.progress},
            {"elapsed", entry.status.elapsed},
            {"failReason", entry.status.failReason},
        };
    }
    return {{"format", "geni.missionstate"}, {"version", 1}, {"missions", missions}};
}

bool MissionSystem::RestoreState(const nlohmann::json &state, std::string *error)
{
    try
    {
        if (state.value("format", std::string()) != "geni.missionstate" || state.value("version", 0) != 1)
        {
            return SetError(error, "not a version 1 mission state");
        }
        for (const auto &[id, saved] : state.at("missions").items())
        {
            Entry *entry = FindEntry(id);
            if (!entry)
            {
                continue;
            }
            MissionStatus status;
            status.state = StateFromName(saved.value("state", std::string("available")));
            status.current = std::min(saved.value("current", size_t(0)), entry->def.objectives.size());
            status.progress = saved.value("progress", std::vector<int>());
            status.progress.resize(entry->def.objectives.size(), 0);
            status.elapsed = saved.value("elapsed", 0.0f);
            status.failReason = saved.value("failReason", std::string());
            entry->status = std::move(status);
        }
    }
    catch (const std::exception &e)
    {
        return SetError(error, std::string("malformed mission state: ") + e.what());
    }
    UpdateLocks();
    m_events.clear();
    return true;
}

} // namespace Geni
