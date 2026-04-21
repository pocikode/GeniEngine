#pragma once

#include <glm/mat4x4.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace Geni
{

class GameObject;

// A Skeleton is a palette-ordered view over joint GameObjects in the scene graph.
// Each bone is a GameObject whose world transform is used to compute the skinning palette.
// The scene graph continues to own the joint objects; Skeleton holds non-owning pointers.
// This lets the existing AnimationComponent + FindChildByName pipeline animate joints
// without a separate "bone pose" code path.
class Skeleton
{
  public:
    static constexpr int MaxBones = 128;

    int AddJoint(GameObject *node, const glm::mat4 &inverseBindMatrix);

    int FindJoint(const std::string &name) const;
    int GetJointCount() const;
    GameObject *GetJointNode(int index) const;
    const glm::mat4 &GetInverseBindMatrix(int index) const;

    // Returns palette[i] = jointWorld[i] * inverseBindMatrix[i], padded to MaxBones with identity.
    const std::vector<glm::mat4> &BuildPalette();

  private:
    std::vector<GameObject *> m_joints;
    std::vector<glm::mat4> m_inverseBindMatrices;
    std::unordered_map<std::string, int> m_nameToIndex;
    std::vector<glm::mat4> m_palette;
};

} // namespace Geni
