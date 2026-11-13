#include "scene/Timeline.h"

#include <glm/common.hpp>

#include <algorithm>

namespace Geni
{

namespace
{

glm::vec3 Vec3(const nlohmann::json &value, const glm::vec3 &fallback)
{
    if (!value.is_array() || value.size() != 3)
    {
        return fallback;
    }
    return glm::vec3(value[0].get<float>(), value[1].get<float>(), value[2].get<float>());
}

glm::vec3 CatmullRom(const glm::vec3 &p0, const glm::vec3 &p1, const glm::vec3 &p2, const glm::vec3 &p3, float t)
{
    const float t2 = t * t;
    const float t3 = t2 * t;
    return 0.5f * ((2.0f * p1) + (-p0 + p2) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                   (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
}

template <typename T> void SortByTime(std::vector<T> &items)
{
    std::stable_sort(items.begin(), items.end(), [](const T &a, const T &b) { return a.time < b.time; });
}

} // namespace

float TimelineData::LastKeyTime() const
{
    float last = 0.0f;
    for (const auto &track : transforms)
    {
        if (!track.keys.empty())
        {
            last = std::max(last, track.keys.back().time);
        }
    }
    for (const auto &cut : cuts)
    {
        last = std::max(last, cut.time);
    }
    for (const auto &cue : sounds)
    {
        last = std::max(last, cue.time);
    }
    for (const auto &event : events)
    {
        last = std::max(last, event.time);
    }
    return last;
}

bool ParseTimeline(const nlohmann::json &document, TimelineData &out, std::string *error)
{
    TimelineData timeline;
    if (document.is_null() || (document.is_object() && document.empty()))
    {
        out = timeline;
        return true;
    }
    try
    {
        for (const auto &item : document.value("transforms", nlohmann::json::array()))
        {
            TimelineTrack track;
            track.target = item.at("target").get<std::string>();
            for (const auto &key : item.value("keys", nlohmann::json::array()))
            {
                TimelineKey k;
                k.time = std::max(0.0f, key.at("time").get<float>());
                k.position = Vec3(key.value("position", nlohmann::json()), glm::vec3(0.0f));
                k.scale = Vec3(key.value("scale", nlohmann::json()), glm::vec3(1.0f));
                const auto r = key.value("rotation", nlohmann::json::array({0.0, 0.0, 0.0, 1.0}));
                // [x, y, z, w] as in scene files.
                k.rotation = glm::normalize(
                    glm::quat(r.at(3).get<float>(), r.at(0).get<float>(), r.at(1).get<float>(), r.at(2).get<float>())
                );
                track.keys.push_back(k);
            }
            SortByTime(track.keys);
            timeline.transforms.push_back(std::move(track));
        }
        for (const auto &item : document.value("cuts", nlohmann::json::array()))
        {
            timeline.cuts.push_back(
                {std::max(0.0f, item.at("time").get<float>()), item.at("camera").get<std::string>()}
            );
        }
        for (const auto &item : document.value("sounds", nlohmann::json::array()))
        {
            timeline.sounds.push_back(
                {std::max(0.0f, item.at("time").get<float>()), item.at("sound").get<std::string>(),
                 item.value("volume", 1.0f)}
            );
        }
        for (const auto &item : document.value("events", nlohmann::json::array()))
        {
            timeline.events.push_back(
                {std::max(0.0f, item.at("time").get<float>()), item.at("name").get<std::string>()}
            );
        }
    }
    catch (const std::exception &e)
    {
        if (error)
        {
            *error = std::string("malformed timeline: ") + e.what();
        }
        return false;
    }
    SortByTime(timeline.cuts);
    SortByTime(timeline.sounds);
    SortByTime(timeline.events);
    out = std::move(timeline);
    return true;
}

nlohmann::json TimelineToJson(const TimelineData &timeline)
{
    nlohmann::json document = nlohmann::json::object();
    if (!timeline.transforms.empty())
    {
        nlohmann::json tracks = nlohmann::json::array();
        for (const auto &track : timeline.transforms)
        {
            nlohmann::json keys = nlohmann::json::array();
            for (const auto &key : track.keys)
            {
                keys.push_back({
                    {"time", key.time},
                    {"position", {key.position.x, key.position.y, key.position.z}},
                    {"rotation", {key.rotation.x, key.rotation.y, key.rotation.z, key.rotation.w}},
                    {"scale", {key.scale.x, key.scale.y, key.scale.z}},
                });
            }
            tracks.push_back({{"target", track.target}, {"keys", keys}});
        }
        document["transforms"] = std::move(tracks);
    }
    if (!timeline.cuts.empty())
    {
        nlohmann::json cuts = nlohmann::json::array();
        for (const auto &cut : timeline.cuts)
        {
            cuts.push_back({{"time", cut.time}, {"camera", cut.camera}});
        }
        document["cuts"] = std::move(cuts);
    }
    if (!timeline.sounds.empty())
    {
        nlohmann::json sounds = nlohmann::json::array();
        for (const auto &cue : timeline.sounds)
        {
            sounds.push_back({{"time", cue.time}, {"sound", cue.sound}, {"volume", cue.volume}});
        }
        document["sounds"] = std::move(sounds);
    }
    if (!timeline.events.empty())
    {
        nlohmann::json events = nlohmann::json::array();
        for (const auto &event : timeline.events)
        {
            events.push_back({{"time", event.time}, {"name", event.name}});
        }
        document["events"] = std::move(events);
    }
    return document;
}

bool SampleTrack(const TimelineTrack &track, float time, glm::vec3 &position, glm::quat &rotation, glm::vec3 &scale)
{
    const auto &keys = track.keys;
    if (keys.empty())
    {
        return false;
    }
    if (time <= keys.front().time || keys.size() == 1)
    {
        position = keys.front().position;
        rotation = keys.front().rotation;
        scale = keys.front().scale;
        return true;
    }
    if (time >= keys.back().time)
    {
        position = keys.back().position;
        rotation = keys.back().rotation;
        scale = keys.back().scale;
        return true;
    }
    size_t i = 0;
    while (i + 1 < keys.size() && keys[i + 1].time <= time)
    {
        ++i;
    }
    const TimelineKey &a = keys[i];
    const TimelineKey &b = keys[i + 1];
    const float span = b.time - a.time;
    const float t = span > 0.0f ? (time - a.time) / span : 1.0f;
    // Neighbours beyond the ends repeat the end keys.
    const glm::vec3 before = i > 0 ? keys[i - 1].position : a.position;
    const glm::vec3 after = i + 2 < keys.size() ? keys[i + 2].position : b.position;
    position = CatmullRom(before, a.position, b.position, after, t);
    rotation = glm::slerp(a.rotation, b.rotation, t);
    scale = glm::mix(a.scale, b.scale, t);
    return true;
}

} // namespace Geni
