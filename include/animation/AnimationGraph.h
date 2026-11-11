#pragma once

#include <cstdint>
#include <functional>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace Geni
{

struct AnimationClip;

enum class AnimParameterType
{
    Float,
    Bool,
    Trigger
};

struct AnimParameterDesc
{
    std::string name;
    AnimParameterType type = AnimParameterType::Float;
    float defaultValue = 0.0f;
};

struct AnimBlendPoint
{
    std::string clip;
    float position = 0.0f;
};

// A single clip, or a 1D blend space when `blendParameter` is set (docs/design/0011).
struct AnimStateDesc
{
    std::string name;
    std::string clip;
    std::string blendParameter;
    std::vector<AnimBlendPoint> blend; // sorted by position
    bool loop = true;
    float speed = 1.0f;
};

enum class AnimCompare
{
    IsSet, // a trigger, or a bool that is true
    Equal,
    NotEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual
};

struct AnimCondition
{
    std::string parameter;
    AnimCompare compare = AnimCompare::IsSet;
    float value = 0.0f;
};

struct AnimTransitionDesc
{
    int from = -1; // -1: any state
    int to = 0;
    std::vector<AnimCondition> conditions;
    float duration = 0.2f;
    // Normalised time of the source state after which the transition may fire; < 0: any.
    float exitTime = -1.0f;
};

struct AnimEventDesc
{
    std::string clip;
    float time = 0.0f; // seconds into the clip
    std::string name;
};

// A parsed .animgraph file: immutable, shared by every animator that uses it.
class AnimationGraph
{
  public:
    static std::shared_ptr<AnimationGraph> Parse(const nlohmann::json &document, std::string *error = nullptr);

    int FindState(const std::string &name) const;
    const AnimParameterDesc *FindParameter(const std::string &name) const;
    // Every clip name a state uses.
    std::vector<std::string> GetClipNames() const;

    std::vector<AnimParameterDesc> parameters;
    std::vector<AnimStateDesc> states;
    std::vector<AnimTransitionDesc> transitions;
    std::vector<AnimEventDesc> events;
    int initialState = 0;
};

// One translation, rotation and scale per bound joint.
struct AnimPose
{
    std::vector<glm::vec3> translations;
    std::vector<glm::quat> rotations;
    std::vector<glm::vec3> scales;

    size_t Size() const
    {
        return rotations.size();
    }
    void Resize(size_t joints);
};

// Mixes b into a: a = a * (1 - weight) + b * weight (rotations by shortest-arc nlerp).
void BlendPoses(AnimPose &a, const AnimPose &b, float weight);

// The playing state of one graph (docs/design/0011): parameters, the current state and
// its phase, a cross-fade in progress, and events. Advance runs on the main thread;
// Sample only reads, so it may run on a worker.
class AnimationGraphInstance
{
  public:
    using ClipLookup = std::function<const AnimationClip *(const std::string &)>;

    // Binds the graph to clips and joints. `joints` are the track target names to drive,
    // `rest` their transforms when nothing animates them.
    void Bind(std::shared_ptr<const AnimationGraph> graph, const ClipLookup &clips, const std::vector<std::string> &joints,
              const AnimPose &rest);
    bool IsBound() const
    {
        return m_graph != nullptr;
    }
    const AnimationGraph *GetGraph() const
    {
        return m_graph.get();
    }

    // Parameters may be set before Bind; unknown names are kept and ignored.
    void SetFloat(const std::string &name, float value);
    void SetBool(const std::string &name, bool value);
    void SetTrigger(const std::string &name);
    float GetFloat(const std::string &name) const;
    bool GetBool(const std::string &name) const;

    // Moves time forward, fires transitions, collects events. Triggers last one Advance.
    void Advance(float deltaTime);
    void Sample(AnimPose &pose) const;

    int GetState() const
    {
        return m_current.state;
    }
    std::string GetStateName() const;
    // 0..1 through the current state (wraps when looping).
    float GetNormalizedTime() const
    {
        return m_current.phase;
    }
    bool IsInTransition() const
    {
        return m_transitionDuration > 0.0f;
    }
    // Forces a state now, without a fade (or with one).
    void Play(const std::string &state, float fadeSeconds = 0.0f);
    // Events fired since the last call.
    std::vector<std::string> TakeEvents();

  private:
    struct BoundClip
    {
        const AnimationClip *clip = nullptr;
        // Track index -> joint index (-1: not bound).
        std::vector<int> trackJoints;
    };
    struct StatePlayback
    {
        int state = -1;
        float phase = 0.0f;
    };

    int ClipIndex(const std::string &name) const;
    float StateDuration(int state) const;
    void StateWeights(int state, int &clipA, int &clipB, float &weightB) const;
    void AdvanceState(StatePlayback &playback, float deltaTime, bool fireEvents);
    void SampleState(const StatePlayback &playback, AnimPose &pose, AnimPose &scratch) const;
    void SampleClip(int clip, float time, AnimPose &pose) const;
    bool ConditionsHold(const AnimTransitionDesc &transition) const;
    void StartTransition(int to, float duration);

    std::shared_ptr<const AnimationGraph> m_graph;
    std::vector<BoundClip> m_clips;
    std::unordered_map<std::string, int> m_clipIndex;
    AnimPose m_rest;
    std::unordered_map<std::string, float> m_values;
    std::vector<std::string> m_triggers;
    StatePlayback m_current;
    StatePlayback m_previous;
    float m_transitionElapsed = 0.0f;
    float m_transitionDuration = 0.0f;
    std::vector<std::string> m_events;
};

} // namespace Geni
