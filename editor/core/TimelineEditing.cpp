#include "TimelineEditing.h"

#include <algorithm>
#include <cmath>

namespace Geni::Editor
{

namespace
{

void Sort(TimelineTrack &track)
{
    std::stable_sort(track.keys.begin(), track.keys.end(), [](const TimelineKey &a, const TimelineKey &b) {
        return a.time < b.time;
    });
}

bool Valid(const TimelineData &timeline, int track)
{
    return track >= 0 && track < static_cast<int>(timeline.transforms.size());
}

} // namespace

int FindTimelineTrack(const TimelineData &timeline, const std::string &target)
{
    for (size_t i = 0; i < timeline.transforms.size(); ++i)
    {
        if (timeline.transforms[i].target == target)
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int AddTimelineTrack(TimelineData &timeline, const std::string &target)
{
    const int existing = FindTimelineTrack(timeline, target);
    if (existing >= 0)
    {
        return existing;
    }
    timeline.transforms.push_back({target, {}});
    return static_cast<int>(timeline.transforms.size() - 1);
}

int SetTimelineKey(TimelineData &timeline, const std::string &target, const TimelineKey &key)
{
    const int index = AddTimelineTrack(timeline, target);
    TimelineTrack &track = timeline.transforms[static_cast<size_t>(index)];
    TimelineKey clean = key;
    clean.time = std::isfinite(key.time) ? std::max(0.0f, key.time) : 0.0f;
    for (TimelineKey &existing : track.keys)
    {
        if (std::abs(existing.time - clean.time) < TimelineKeySnap)
        {
            existing = clean;
            return index;
        }
    }
    track.keys.push_back(clean);
    Sort(track);
    return index;
}

bool RemoveTimelineKey(TimelineData &timeline, int track, int key)
{
    if (!Valid(timeline, track))
    {
        return false;
    }
    auto &keys = timeline.transforms[static_cast<size_t>(track)].keys;
    if (key < 0 || key >= static_cast<int>(keys.size()))
    {
        return false;
    }
    keys.erase(keys.begin() + key);
    return true;
}

int MoveTimelineKey(TimelineData &timeline, int track, int key, float time)
{
    if (!Valid(timeline, track))
    {
        return -1;
    }
    auto &keys = timeline.transforms[static_cast<size_t>(track)].keys;
    if (key < 0 || key >= static_cast<int>(keys.size()))
    {
        return -1;
    }
    TimelineKey moved = keys[static_cast<size_t>(key)];
    keys.erase(keys.begin() + key);
    moved.time = std::isfinite(time) ? std::max(0.0f, time) : 0.0f;
    // A key already at the destination is replaced.
    keys.erase(
        std::remove_if(
            keys.begin(), keys.end(),
            [&](const TimelineKey &k) { return std::abs(k.time - moved.time) < TimelineKeySnap; }
        ),
        keys.end()
    );
    keys.push_back(moved);
    Sort(timeline.transforms[static_cast<size_t>(track)]);
    for (size_t i = 0; i < keys.size(); ++i)
    {
        if (keys[i].time == moved.time)
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool RemoveTimelineTrack(TimelineData &timeline, int track)
{
    if (!Valid(timeline, track))
    {
        return false;
    }
    timeline.transforms.erase(timeline.transforms.begin() + track);
    return true;
}

} // namespace Geni::Editor
