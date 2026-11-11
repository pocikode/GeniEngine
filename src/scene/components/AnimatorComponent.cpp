#include "scene/components/AnimatorComponent.h"

#include "assets/AssetManager.h"
#include "core/Engine.h"
#include "core/Log.h"
#include "scene/GameObject.h"
#include "scene/Reflection.h"
#include "scene/components/AnimationComponent.h"

#include <mutex>
#include <unordered_map>

namespace Geni
{

namespace
{
std::mutex s_graphMutex;
std::unordered_map<std::string, std::shared_ptr<const AnimationGraph>> s_graphs;
} // namespace

std::shared_ptr<const AnimationGraph> AnimatorComponent::LoadGraph(const std::string &path)
{
    {
        std::lock_guard<std::mutex> lock(s_graphMutex);
        auto it = s_graphs.find(path);
        if (it != s_graphs.end())
        {
            return it->second;
        }
    }
    const std::string text = Engine::GetInstance().GetFileSystem().LoadAssetFileText(path);
    if (text.empty())
    {
        GENI_LOG_ERROR("Animator: could not read %s", path.c_str());
        return nullptr;
    }
    nlohmann::json document = nlohmann::json::parse(text, nullptr, false, true);
    if (document.is_discarded())
    {
        GENI_LOG_ERROR("Animator: %s is not valid JSON", path.c_str());
        return nullptr;
    }
    std::string error;
    std::shared_ptr<const AnimationGraph> graph = AnimationGraph::Parse(document, &error);
    if (!graph)
    {
        GENI_LOG_ERROR("Animator: %s: %s", path.c_str(), error.c_str());
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(s_graphMutex);
    s_graphs[path] = graph;
    return graph;
}

void AnimatorComponent::ClearGraphCache()
{
    std::lock_guard<std::mutex> lock(s_graphMutex);
    s_graphs.clear();
}

void AnimatorComponent::ResolveAssets(SerializationContext &context)
{
    if (context.assets && m_animationID.IsValid() && !m_animation.IsValid())
    {
        SetAnimation(context.assets->LoadAnimation(m_animationID, true));
    }
}

void AnimatorComponent::SetAnimation(const AssetHandle<AnimationAsset> &animation)
{
    m_animation = animation;
    m_animationID = animation.GetID();
}

void AnimatorComponent::SetAnimationID(AssetID id)
{
    if (id != m_animationID)
    {
        m_animation.Reset();
    }
    m_animationID = id;
}

void AnimatorComponent::SetGraphPath(const std::string &path)
{
    if (path != m_graphPath)
    {
        m_graph.reset();
        m_graphFailed = false;
    }
    m_graphPath = path;
}

void AnimatorComponent::SetGraph(std::shared_ptr<const AnimationGraph> graph)
{
    m_graph = std::move(graph);
    m_graphFailed = false;
}

bool AnimatorComponent::TryBind()
{
    if (m_instance.IsBound())
    {
        return true;
    }
    if (!m_graph && !m_graphFailed && !m_graphPath.empty())
    {
        m_graph = LoadGraph(m_graphPath);
        m_graphFailed = m_graph == nullptr;
    }
    if (!m_graph || !m_animation.IsReady() || !m_owner)
    {
        return false;
    }

    // The clips this graph uses, and every joint their tracks drive.
    std::unordered_map<std::string, const AnimationClip *> clips;
    for (const auto &clip : m_animation->clips)
    {
        if (clip)
        {
            clips[clip->name] = clip.get();
        }
    }
    std::vector<std::string> jointNames;
    m_joints.clear();
    for (const std::string &name : m_graph->GetClipNames())
    {
        auto it = clips.find(name);
        if (it == clips.end())
        {
            GENI_LOG_WARN("Animator on '%s': clip '%s' is not in the animation", m_owner->GetName().c_str(), name.c_str());
            continue;
        }
        for (const TransformTrack &track : it->second->tracks)
        {
            if (std::find(jointNames.begin(), jointNames.end(), track.targetName) != jointNames.end())
            {
                continue;
            }
            if (GameObject *joint = m_owner->FindChildByName(track.targetName))
            {
                jointNames.push_back(track.targetName);
                m_joints.push_back(joint);
            }
        }
    }
    AnimPose rest;
    rest.Resize(m_joints.size());
    for (size_t i = 0; i < m_joints.size(); ++i)
    {
        rest.translations[i] = m_joints[i]->GetPosition();
        rest.rotations[i] = m_joints[i]->GetRotation();
        rest.scales[i] = m_joints[i]->GetScale();
    }
    m_instance.Bind(
        m_graph,
        [&clips](const std::string &name) -> const AnimationClip * {
            auto it = clips.find(name);
            return it == clips.end() ? nullptr : it->second;
        },
        jointNames, rest
    );
    // A cooked glTF scene comes with an AnimationComponent playing its first clip; the
    // animator takes over the joints.
    if (auto *player = m_owner->GetComponent<AnimationComponent>())
    {
        player->SetPlaying(false);
    }
    return true;
}

bool AnimatorComponent::Advance(float deltaTime)
{
    if (!m_playing || !TryBind())
    {
        return false;
    }
    m_instance.Advance(deltaTime);
    return true;
}

void AnimatorComponent::SamplePose()
{
    m_instance.Sample(m_pose);
    m_hasPose = true;
}

void AnimatorComponent::ApplyPose()
{
    if (!m_hasPose)
    {
        return;
    }
    const size_t count = std::min(m_pose.Size(), m_joints.size());
    for (size_t i = 0; i < count; ++i)
    {
        GameObject *joint = m_joints[i];
        joint->SetPosition(m_pose.translations[i]);
        joint->SetRotation(m_pose.rotations[i]);
        joint->SetScale(m_pose.scales[i]);
    }
    m_hasPose = false;
}

void AnimatorComponent::Update(float deltaTime)
{
    if (m_owner && m_owner->GetScene())
    {
        return; // Scene::Update already ran Advance, SamplePose and ApplyPose
    }
    if (Advance(deltaTime))
    {
        SamplePose();
        ApplyPose();
    }
}

namespace
{
using A = AnimatorComponent;
const bool s_registered = RegisterComponentType<A>(
    "AnimatorComponent",
    {
        AssetIDProperty<A>("animation", AssetType::Animation, &A::GetAnimationID, &A::SetAnimationID),
        StringProperty<A>("graph", &A::GetGraphPath, &A::SetGraphPath),
        BoolProperty<A>("playing", &A::IsPlaying, &A::SetPlaying),
    }
);
} // namespace

void LinkAnimatorComponent()
{
}

} // namespace Geni
