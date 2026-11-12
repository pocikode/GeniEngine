#include "animation/AnimationGraph.h"

#include "scene/components/AnimationComponent.h"

#include <algorithm>
#include <cmath>

namespace Geni
{

namespace
{

bool ParseCompare(const std::string &text, AnimCompare &compare)
{
    static const std::pair<const char *, AnimCompare> table[] = {
        {"==", AnimCompare::Equal}, {"!=", AnimCompare::NotEqual}, {"<", AnimCompare::Less},
        {"<=", AnimCompare::LessEqual}, {">", AnimCompare::Greater}, {">=", AnimCompare::GreaterEqual},
    };
    for (const auto &[name, value] : table)
    {
        if (text == name)
        {
            compare = value;
            return true;
        }
    }
    return false;
}

float JsonNumber(const nlohmann::json &value)
{
    if (value.is_boolean())
    {
        return value.get<bool>() ? 1.0f : 0.0f;
    }
    if (value.is_number())
    {
        return value.get<float>();
    }
    return 0.0f;
}

template <typename Key> size_t FindKey(const std::vector<Key> &keys, float time)
{
    // Index of the last key at or before `time` (keys are sorted by time).
    size_t lo = 0;
    size_t hi = keys.size();
    while (hi - lo > 1)
    {
        const size_t mid = (lo + hi) / 2;
        if (keys[mid].time <= time)
        {
            lo = mid;
        }
        else
        {
            hi = mid;
        }
    }
    return lo;
}

glm::vec3 SampleVec3(const std::vector<KeyFrameVec3> &keys, float time)
{
    if (keys.size() == 1 || time <= keys.front().time)
    {
        return keys.front().value;
    }
    if (time >= keys.back().time)
    {
        return keys.back().value;
    }
    const size_t i = FindKey(keys, time);
    const float span = keys[i + 1].time - keys[i].time;
    const float t = span > 0.0f ? (time - keys[i].time) / span : 0.0f;
    return glm::mix(keys[i].value, keys[i + 1].value, t);
}

glm::quat SampleQuat(const std::vector<KeyFrameQuat> &keys, float time)
{
    if (keys.size() == 1 || time <= keys.front().time)
    {
        return keys.front().value;
    }
    if (time >= keys.back().time)
    {
        return keys.back().value;
    }
    const size_t i = FindKey(keys, time);
    const float span = keys[i + 1].time - keys[i].time;
    const float t = span > 0.0f ? (time - keys[i].time) / span : 0.0f;
    return glm::slerp(keys[i].value, keys[i + 1].value, t);
}

} // namespace

std::shared_ptr<AnimationGraph> AnimationGraph::Parse(const nlohmann::json &document, std::string *error)
{
    auto fail = [error](const std::string &message) {
        if (error)
        {
            *error = "animation graph: " + message;
        }
        return nullptr;
    };
    if (!document.is_object())
    {
        return fail("expected an object");
    }
    auto graph = std::make_shared<AnimationGraph>();

    if (auto parameters = document.find("parameters"); parameters != document.end() && parameters->is_object())
    {
        for (auto it = parameters->begin(); it != parameters->end(); ++it)
        {
            AnimParameterDesc parameter;
            parameter.name = it.key();
            const nlohmann::json &value = it.value();
            if (value.is_string() && value.get<std::string>() == "trigger")
            {
                parameter.type = AnimParameterType::Trigger;
            }
            else if (value.is_boolean())
            {
                parameter.type = AnimParameterType::Bool;
                parameter.defaultValue = value.get<bool>() ? 1.0f : 0.0f;
            }
            else if (value.is_number())
            {
                parameter.type = AnimParameterType::Float;
                parameter.defaultValue = value.get<float>();
            }
            else
            {
                return fail("parameter '" + parameter.name + "' must be a number, a bool or \"trigger\"");
            }
            graph->parameters.push_back(parameter);
        }
    }

    auto states = document.find("states");
    if (states == document.end() || !states->is_object() || states->empty())
    {
        return fail("needs a non-empty \"states\" object");
    }
    for (auto it = states->begin(); it != states->end(); ++it)
    {
        AnimStateDesc state;
        state.name = it.key();
        const nlohmann::json &value = it.value();
        state.loop = value.value("loop", true);
        state.speed = value.value("speed", 1.0f);
        if (auto blend = value.find("blend"); blend != value.end())
        {
            state.blendParameter = blend->value("parameter", std::string());
            if (auto clips = blend->find("clips"); clips != blend->end() && clips->is_array())
            {
                for (const auto &entry : *clips)
                {
                    if (!entry.is_array() || entry.size() != 2 || !entry[0].is_string() || !entry[1].is_number())
                    {
                        return fail("state '" + state.name + "': blend clips are [\"clip\", position] pairs");
                    }
                    state.blend.push_back({entry[0].get<std::string>(), entry[1].get<float>()});
                }
            }
            std::sort(state.blend.begin(), state.blend.end(), [](const AnimBlendPoint &a, const AnimBlendPoint &b) {
                return a.position < b.position;
            });
            if (state.blendParameter.empty() || state.blend.empty())
            {
                return fail("state '" + state.name + "': a blend needs a parameter and clips");
            }
        }
        else
        {
            state.clip = value.value("clip", std::string());
            if (state.clip.empty())
            {
                return fail("state '" + state.name + "' needs a clip or a blend");
            }
        }
        graph->states.push_back(state);
    }

    const std::string initial = document.value("initial", graph->states.front().name);
    graph->initialState = graph->FindState(initial);
    if (graph->initialState < 0)
    {
        return fail("unknown initial state '" + initial + "'");
    }

    if (auto transitions = document.find("transitions"); transitions != document.end() && transitions->is_array())
    {
        for (const auto &value : *transitions)
        {
            AnimTransitionDesc transition;
            const std::string from = value.value("from", std::string("*"));
            const std::string to = value.value("to", std::string());
            transition.from = from == "*" ? -1 : graph->FindState(from);
            transition.to = graph->FindState(to);
            if ((from != "*" && transition.from < 0) || transition.to < 0)
            {
                return fail("transition " + from + " -> " + to + " names an unknown state");
            }
            transition.duration = std::max(0.0f, value.value("duration", 0.2f));
            transition.exitTime = value.value("exitTime", -1.0f);
            if (auto when = value.find("when"); when != value.end() && when->is_array())
            {
                for (const auto &entry : *when)
                {
                    AnimCondition condition;
                    if (!entry.is_array() || entry.empty() || !entry[0].is_string())
                    {
                        return fail("conditions are [\"name\"] or [\"name\", \"op\", value]");
                    }
                    condition.parameter = entry[0].get<std::string>();
                    if (entry.size() >= 3)
                    {
                        if (!entry[1].is_string() || !ParseCompare(entry[1].get<std::string>(), condition.compare))
                        {
                            return fail("unknown comparison in a condition on '" + condition.parameter + "'");
                        }
                        condition.value = JsonNumber(entry[2]);
                    }
                    if (!graph->FindParameter(condition.parameter))
                    {
                        return fail("condition on unknown parameter '" + condition.parameter + "'");
                    }
                    transition.conditions.push_back(condition);
                }
            }
            graph->transitions.push_back(transition);
        }
    }

    if (auto events = document.find("events"); events != document.end() && events->is_array())
    {
        for (const auto &value : *events)
        {
            AnimEventDesc event;
            event.clip = value.value("clip", std::string());
            event.time = value.value("time", 0.0f);
            event.name = value.value("name", std::string());
            if (event.clip.empty() || event.name.empty())
            {
                return fail("events need a clip and a name");
            }
            graph->events.push_back(event);
        }
    }
    return graph;
}

int AnimationGraph::FindState(const std::string &name) const
{
    for (size_t i = 0; i < states.size(); ++i)
    {
        if (states[i].name == name)
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

const AnimParameterDesc *AnimationGraph::FindParameter(const std::string &name) const
{
    for (const AnimParameterDesc &parameter : parameters)
    {
        if (parameter.name == name)
        {
            return &parameter;
        }
    }
    return nullptr;
}

std::vector<std::string> AnimationGraph::GetClipNames() const
{
    std::vector<std::string> names;
    auto add = [&names](const std::string &name) {
        if (std::find(names.begin(), names.end(), name) == names.end())
        {
            names.push_back(name);
        }
    };
    for (const AnimStateDesc &state : states)
    {
        if (state.blend.empty())
        {
            add(state.clip);
        }
        for (const AnimBlendPoint &point : state.blend)
        {
            add(point.clip);
        }
    }
    return names;
}

void AnimPose::Resize(size_t joints)
{
    translations.assign(joints, glm::vec3(0.0f));
    rotations.assign(joints, glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
    scales.assign(joints, glm::vec3(1.0f));
}

void BlendPoses(AnimPose &a, const AnimPose &b, float weight)
{
    if (weight <= 0.0f)
    {
        return;
    }
    if (weight >= 1.0f)
    {
        a = b;
        return;
    }
    const size_t count = std::min(a.Size(), b.Size());
    for (size_t i = 0; i < count; ++i)
    {
        a.translations[i] = glm::mix(a.translations[i], b.translations[i], weight);
        a.scales[i] = glm::mix(a.scales[i], b.scales[i], weight);
        glm::quat q = b.rotations[i];
        if (glm::dot(a.rotations[i], q) < 0.0f)
        {
            q = -q;
        }
        a.rotations[i] = glm::normalize(a.rotations[i] * (1.0f - weight) + q * weight);
    }
}

void AnimationGraphInstance::Bind(
    std::shared_ptr<const AnimationGraph> graph, const ClipLookup &clips, const std::vector<std::string> &joints,
    const AnimPose &rest
)
{
    m_graph = std::move(graph);
    m_rest = rest;
    m_rest.Resize(joints.size());
    if (rest.Size() == joints.size())
    {
        m_rest = rest;
    }
    m_clips.clear();
    m_clipIndex.clear();
    if (!m_graph)
    {
        return;
    }
    std::unordered_map<std::string, int> jointIndex;
    for (size_t i = 0; i < joints.size(); ++i)
    {
        jointIndex[joints[i]] = static_cast<int>(i);
    }
    for (const std::string &name : m_graph->GetClipNames())
    {
        BoundClip bound;
        bound.clip = clips ? clips(name) : nullptr;
        if (bound.clip)
        {
            for (const TransformTrack &track : bound.clip->tracks)
            {
                auto it = jointIndex.find(track.targetName);
                bound.trackJoints.push_back(it == jointIndex.end() ? -1 : it->second);
            }
        }
        m_clipIndex[name] = static_cast<int>(m_clips.size());
        m_clips.push_back(std::move(bound));
    }
    for (const AnimParameterDesc &parameter : m_graph->parameters)
    {
        if (parameter.type != AnimParameterType::Trigger && m_values.find(parameter.name) == m_values.end())
        {
            m_values[parameter.name] = parameter.defaultValue;
        }
    }
    m_current = {m_graph->initialState, 0.0f};
    m_previous = {};
    m_transitionDuration = 0.0f;
    m_transitionElapsed = 0.0f;
}

void AnimationGraphInstance::SetFloat(const std::string &name, float value)
{
    m_values[name] = value;
}

void AnimationGraphInstance::SetBool(const std::string &name, bool value)
{
    m_values[name] = value ? 1.0f : 0.0f;
}

void AnimationGraphInstance::SetTrigger(const std::string &name)
{
    if (std::find(m_triggers.begin(), m_triggers.end(), name) == m_triggers.end())
    {
        m_triggers.push_back(name);
    }
}

float AnimationGraphInstance::GetFloat(const std::string &name) const
{
    auto it = m_values.find(name);
    return it == m_values.end() ? 0.0f : it->second;
}

bool AnimationGraphInstance::GetBool(const std::string &name) const
{
    return GetFloat(name) != 0.0f;
}

std::string AnimationGraphInstance::GetStateName() const
{
    if (!m_graph || m_current.state < 0)
    {
        return std::string();
    }
    return m_graph->states[static_cast<size_t>(m_current.state)].name;
}

int AnimationGraphInstance::ClipIndex(const std::string &name) const
{
    auto it = m_clipIndex.find(name);
    return it == m_clipIndex.end() ? -1 : it->second;
}

void AnimationGraphInstance::StateWeights(int state, int &clipA, int &clipB, float &weightB) const
{
    const AnimStateDesc &desc = m_graph->states[static_cast<size_t>(state)];
    clipB = -1;
    weightB = 0.0f;
    if (desc.blend.empty())
    {
        clipA = ClipIndex(desc.clip);
        return;
    }
    const float value = GetFloat(desc.blendParameter);
    const auto &points = desc.blend;
    if (value <= points.front().position || points.size() == 1)
    {
        clipA = ClipIndex(points.front().clip);
        return;
    }
    if (value >= points.back().position)
    {
        clipA = ClipIndex(points.back().clip);
        return;
    }
    for (size_t i = 0; i + 1 < points.size(); ++i)
    {
        if (value <= points[i + 1].position)
        {
            clipA = ClipIndex(points[i].clip);
            clipB = ClipIndex(points[i + 1].clip);
            const float span = points[i + 1].position - points[i].position;
            weightB = span > 0.0f ? (value - points[i].position) / span : 0.0f;
            return;
        }
    }
    clipA = ClipIndex(points.back().clip);
}

float AnimationGraphInstance::StateDuration(int state) const
{
    int a = -1;
    int b = -1;
    float w = 0.0f;
    StateWeights(state, a, b, w);
    auto duration = [this](int clip) {
        if (clip < 0 || !m_clips[static_cast<size_t>(clip)].clip)
        {
            return 0.0f;
        }
        return m_clips[static_cast<size_t>(clip)].clip->duration;
    };
    const float da = duration(a);
    if (b < 0)
    {
        return da;
    }
    // Blended clips share a phase; it advances at the weighted rate of the two.
    return da + (duration(b) - da) * w;
}

void AnimationGraphInstance::AdvanceState(StatePlayback &playback, float deltaTime, bool fireEvents)
{
    if (playback.state < 0)
    {
        return;
    }
    const AnimStateDesc &desc = m_graph->states[static_cast<size_t>(playback.state)];
    const float duration = StateDuration(playback.state);
    if (duration <= 0.0f)
    {
        return;
    }
    const float before = playback.phase;
    float after = before + deltaTime * desc.speed / duration;
    bool wrapped = false;
    if (desc.loop)
    {
        if (after >= 1.0f)
        {
            after = std::fmod(after, 1.0f);
            wrapped = true;
        }
    }
    else
    {
        after = std::min(after, 1.0f);
    }
    playback.phase = after;

    if (!fireEvents || m_graph->events.empty())
    {
        return;
    }
    // Events of the clip that dominates the state.
    int a = -1;
    int b = -1;
    float w = 0.0f;
    StateWeights(playback.state, a, b, w);
    const int dominant = (b >= 0 && w > 0.5f) ? b : a;
    if (dominant < 0 || !m_clips[static_cast<size_t>(dominant)].clip)
    {
        return;
    }
    const AnimationClip &clip = *m_clips[static_cast<size_t>(dominant)].clip;
    for (const AnimEventDesc &event : m_graph->events)
    {
        if (event.clip != clip.name || clip.duration <= 0.0f)
        {
            continue;
        }
        const float at = event.time / clip.duration;
        const bool crossed = wrapped ? (at >= before || at < after) : (at >= before && at < after);
        if (crossed && deltaTime > 0.0f)
        {
            m_events.push_back(event.name);
        }
    }
}

bool AnimationGraphInstance::ConditionsHold(const AnimTransitionDesc &transition) const
{
    for (const AnimCondition &condition : transition.conditions)
    {
        const AnimParameterDesc *parameter = m_graph->FindParameter(condition.parameter);
        if (!parameter)
        {
            return false;
        }
        if (parameter->type == AnimParameterType::Trigger)
        {
            if (std::find(m_triggers.begin(), m_triggers.end(), condition.parameter) == m_triggers.end())
            {
                return false;
            }
            continue;
        }
        const float value = GetFloat(condition.parameter);
        bool holds = false;
        switch (condition.compare)
        {
        case AnimCompare::IsSet:
            holds = value != 0.0f;
            break;
        case AnimCompare::Equal:
            holds = value == condition.value;
            break;
        case AnimCompare::NotEqual:
            holds = value != condition.value;
            break;
        case AnimCompare::Less:
            holds = value < condition.value;
            break;
        case AnimCompare::LessEqual:
            holds = value <= condition.value;
            break;
        case AnimCompare::Greater:
            holds = value > condition.value;
            break;
        case AnimCompare::GreaterEqual:
            holds = value >= condition.value;
            break;
        }
        if (!holds)
        {
            return false;
        }
    }
    return true;
}

void AnimationGraphInstance::StartTransition(int to, float duration)
{
    if (duration > 0.0f)
    {
        m_previous = m_current;
        m_transitionDuration = duration;
        m_transitionElapsed = 0.0f;
    }
    else
    {
        m_previous = {};
        m_transitionDuration = 0.0f;
    }
    m_current = {to, 0.0f};
}

void AnimationGraphInstance::Play(const std::string &state, float fadeSeconds)
{
    if (!m_graph)
    {
        return;
    }
    const int index = m_graph->FindState(state);
    if (index >= 0)
    {
        StartTransition(index, fadeSeconds);
    }
}

void AnimationGraphInstance::Advance(float deltaTime)
{
    if (!m_graph)
    {
        m_triggers.clear();
        return;
    }

    for (const AnimTransitionDesc &transition : m_graph->transitions)
    {
        if (transition.to == m_current.state || (transition.from >= 0 && transition.from != m_current.state))
        {
            continue;
        }
        if (transition.exitTime >= 0.0f && m_current.phase < transition.exitTime)
        {
            continue;
        }
        if (!ConditionsHold(transition))
        {
            continue;
        }
        // Consume the triggers this transition used.
        for (const AnimCondition &condition : transition.conditions)
        {
            m_triggers.erase(
                std::remove(m_triggers.begin(), m_triggers.end(), condition.parameter), m_triggers.end()
            );
        }
        StartTransition(transition.to, transition.duration);
        break;
    }
    m_triggers.clear();

    AdvanceState(m_current, deltaTime, true);
    if (m_transitionDuration > 0.0f)
    {
        AdvanceState(m_previous, deltaTime, false);
        m_transitionElapsed += deltaTime;
        if (m_transitionElapsed >= m_transitionDuration)
        {
            m_transitionDuration = 0.0f;
            m_previous = {};
        }
    }
}

void AnimationGraphInstance::SampleClip(int clip, float time, AnimPose &pose) const
{
    if (clip < 0)
    {
        return;
    }
    const BoundClip &bound = m_clips[static_cast<size_t>(clip)];
    if (!bound.clip)
    {
        return;
    }
    const auto &tracks = bound.clip->tracks;
    for (size_t i = 0; i < tracks.size() && i < bound.trackJoints.size(); ++i)
    {
        const int joint = bound.trackJoints[i];
        if (joint < 0)
        {
            continue;
        }
        const TransformTrack &track = tracks[i];
        const size_t j = static_cast<size_t>(joint);
        if (!track.positions.empty())
        {
            pose.translations[j] = SampleVec3(track.positions, time);
        }
        if (!track.rotations.empty())
        {
            pose.rotations[j] = SampleQuat(track.rotations, time);
        }
        if (!track.scales.empty())
        {
            pose.scales[j] = SampleVec3(track.scales, time);
        }
    }
}

void AnimationGraphInstance::SampleState(const StatePlayback &playback, AnimPose &pose, AnimPose &scratch) const
{
    pose = m_rest;
    if (playback.state < 0)
    {
        return;
    }
    int a = -1;
    int b = -1;
    float w = 0.0f;
    StateWeights(playback.state, a, b, w);
    auto clipTime = [this, &playback](int clip) {
        if (clip < 0 || !m_clips[static_cast<size_t>(clip)].clip)
        {
            return 0.0f;
        }
        return playback.phase * m_clips[static_cast<size_t>(clip)].clip->duration;
    };
    SampleClip(a, clipTime(a), pose);
    if (b >= 0 && w > 0.0f)
    {
        scratch = m_rest;
        SampleClip(b, clipTime(b), scratch);
        BlendPoses(pose, scratch, w);
    }
}

void AnimationGraphInstance::Sample(AnimPose &pose) const
{
    if (!m_graph)
    {
        pose = m_rest;
        return;
    }
    AnimPose scratch;
    SampleState(m_current, pose, scratch);
    if (m_transitionDuration > 0.0f && m_previous.state >= 0)
    {
        AnimPose from;
        SampleState(m_previous, from, scratch);
        const float t = std::clamp(m_transitionElapsed / m_transitionDuration, 0.0f, 1.0f);
        BlendPoses(from, pose, t);
        pose = std::move(from);
    }
}

std::vector<std::string> AnimationGraphInstance::TakeEvents()
{
    std::vector<std::string> events;
    events.swap(m_events);
    return events;
}

} // namespace Geni
