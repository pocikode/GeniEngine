#include "scene/Skeleton.h"

#include "scene/GameObject.h"

namespace Geni
{

int Skeleton::AddJoint(GameObject *node, const glm::mat4 &inverseBindMatrix)
{
    int index = static_cast<int>(m_joints.size());
    m_joints.push_back(node);
    m_inverseBindMatrices.push_back(inverseBindMatrix);
    if (node)
    {
        m_nameToIndex[node->GetName()] = index;
    }
    return index;
}

int Skeleton::FindJoint(const std::string &name) const
{
    auto it = m_nameToIndex.find(name);
    return it == m_nameToIndex.end() ? -1 : it->second;
}

int Skeleton::GetJointCount() const
{
    return static_cast<int>(m_joints.size());
}

GameObject *Skeleton::GetJointNode(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_joints.size()))
    {
        return nullptr;
    }
    return m_joints[index];
}

const glm::mat4 &Skeleton::GetInverseBindMatrix(int index) const
{
    return m_inverseBindMatrices[index];
}

const std::vector<glm::mat4> &Skeleton::BuildPalette()
{
    m_palette.assign(MaxBones, glm::mat4(1.0f));
    int count = std::min(static_cast<int>(m_joints.size()), MaxBones);
    for (int i = 0; i < count; ++i)
    {
        if (m_joints[i])
        {
            m_palette[i] = m_joints[i]->GetWorldTransform() * m_inverseBindMatrices[i];
        }
    }
    return m_palette;
}

} // namespace Geni
