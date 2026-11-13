#pragma once

// Timeline data (docs/design/0026): tracks of keys over time. Pure data and sampling,
// shared by TimelineComponent and the editor's Timeline panel.

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace Geni
{

struct TimelineKey
{
    float time = 0.0f;
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};
};

// Moves one object (found by name) through its keys, sorted by time.
struct TimelineTrack
{
    std::string target;
    std::vector<TimelineKey> keys;
};

struct CameraCut
{
    float time = 0.0f;
    std::string camera;
};

struct SoundCue
{
    float time = 0.0f;
    std::string sound;
    float volume = 1.0f;
};

struct TimelineEvent
{
    float time = 0.0f;
    std::string name;
};

struct TimelineData
{
    std::vector<TimelineTrack> transforms;
    std::vector<CameraCut> cuts;
    std::vector<SoundCue> sounds;
    std::vector<TimelineEvent> events;

    // The time of the last key, cut, cue or event.
    float LastKeyTime() const;
    bool Empty() const
    {
        return transforms.empty() && cuts.empty() && sounds.empty() && events.empty();
    }
};

bool ParseTimeline(const nlohmann::json &document, TimelineData &out, std::string *error = nullptr);
nlohmann::json TimelineToJson(const TimelineData &timeline);

// The track at `time`: Catmull-Rom through the positions, slerp between rotations,
// linear scale; the end keys hold outside the keyed range. False without keys.
bool SampleTrack(const TimelineTrack &track, float time, glm::vec3 &position, glm::quat &rotation, glm::vec3 &scale);

} // namespace Geni
