#pragma once

// Timeline edits for the editor (docs/design/0026): pure functions on TimelineData, so they
// are tested without a window. The app writes the result back through
// EditorDocument::SetProperty on the component's `tracks`, which makes every edit undoable.

#include "scene/Timeline.h"

#include <string>

namespace Geni::Editor
{

// Keys closer than this to each other are the same key.
constexpr float TimelineKeySnap = 1.0f / 120.0f;

// The track for `target`, or -1.
int FindTimelineTrack(const TimelineData &timeline, const std::string &target);
// Adds an empty track for `target` (or returns the existing one).
int AddTimelineTrack(TimelineData &timeline, const std::string &target);
// Sets the key of `target` at `time` (replacing one within TimelineKeySnap), adding the
// track when needed. Keys stay sorted. Returns the track index.
int SetTimelineKey(TimelineData &timeline, const std::string &target, const TimelineKey &key);
bool RemoveTimelineKey(TimelineData &timeline, int track, int key);
// Moves a key to `time` (keys stay sorted; a key already there is replaced). Returns the
// key's new index, or -1.
int MoveTimelineKey(TimelineData &timeline, int track, int key, float time);
bool RemoveTimelineTrack(TimelineData &timeline, int track);

} // namespace Geni::Editor
