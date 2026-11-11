#pragma once

#include "CommandStack.h"

#include <cstdint>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>
#include <vector>

namespace Geni
{
class GameObject;
}

namespace Geni::Editor
{

// The part of an object that is not a component: what the top of the inspector edits.
struct NodeState
{
    std::string name;
    bool active = true;
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};

    static NodeState Capture(const GameObject &object);
    void Apply(GameObject &object) const;
    bool operator==(const NodeState &other) const;
    bool operator!=(const NodeState &other) const
    {
        return !(*this == other);
    }
};

// Name, active flag and transform of one or more objects (a rename, a gizmo drag).
class NodeStateCommand : public Command
{
  public:
    struct Entry
    {
        uint64_t id = 0;
        NodeState before;
        NodeState after;
    };

    NodeStateCommand(std::string name, std::vector<Entry> entries)
        : m_name(std::move(name)), m_entries(std::move(entries))
    {
    }
    const char *GetName() const override
    {
        return m_name.c_str();
    }
    void Do(EditContext &context) override;
    void Undo(EditContext &context) override;

  private:
    std::string m_name;
    std::vector<Entry> m_entries;
};

// One reflected property of one component, by component index and property name.
class PropertyCommand : public Command
{
  public:
    PropertyCommand(uint64_t object, int component, std::string property, nlohmann::json before, nlohmann::json after)
        : m_object(object), m_component(component), m_property(std::move(property)), m_before(std::move(before)),
          m_after(std::move(after))
    {
    }
    const char *GetName() const override
    {
        return "Change property";
    }
    void Do(EditContext &context) override
    {
        Apply(context, m_after);
    }
    void Undo(EditContext &context) override
    {
        Apply(context, m_before);
    }

    // Sets a property on a live component and lets it resolve asset references. Shared
    // with the inspector, which previews a value before the command exists.
    static bool Set(
        EditContext &context, uint64_t object, int component, const std::string &property, const nlohmann::json &value
    );

  private:
    void Apply(EditContext &context, const nlohmann::json &value);

    uint64_t m_object = 0;
    int m_component = 0;
    std::string m_property;
    nlohmann::json m_before;
    nlohmann::json m_after;
};

// Adds a component of a registered type, at an index (-1: last), with properties.
class AddComponentCommand : public Command
{
  public:
    AddComponentCommand(uint64_t object, std::string type, nlohmann::json properties, int index = -1)
        : m_object(object), m_type(std::move(type)), m_properties(std::move(properties)), m_index(index)
    {
    }
    const char *GetName() const override
    {
        return "Add component";
    }
    void Do(EditContext &context) override;
    void Undo(EditContext &context) override;

  private:
    uint64_t m_object = 0;
    std::string m_type;
    nlohmann::json m_properties;
    int m_index = -1;
};

// Removes the component at an index, remembering its type and properties.
class RemoveComponentCommand : public Command
{
  public:
    RemoveComponentCommand(uint64_t object, int index) : m_object(object), m_index(index)
    {
    }
    const char *GetName() const override
    {
        return "Remove component";
    }
    void Do(EditContext &context) override;
    void Undo(EditContext &context) override;

  private:
    uint64_t m_object = 0;
    int m_index = 0;
    std::string m_type;
    nlohmann::json m_properties;
};

// Moves objects to another parent and/or position among their siblings. Parent ID 0 is
// the scene root.
class ReparentCommand : public Command
{
  public:
    struct Entry
    {
        uint64_t id = 0;
        uint64_t oldParent = 0;
        int oldIndex = -1;
        uint64_t newParent = 0;
        int newIndex = -1;
    };

    explicit ReparentCommand(std::vector<Entry> entries) : m_entries(std::move(entries))
    {
    }
    const char *GetName() const override
    {
        return "Move in hierarchy";
    }
    void Do(EditContext &context) override;
    void Undo(EditContext &context) override;

  private:
    std::vector<Entry> m_entries;
};

// A subtree as it was at one moment: where it hung and everything in it, with IDs.
struct SubtreeSnapshot
{
    uint64_t root = 0;
    uint64_t parent = 0;
    int index = -1;
    nlohmann::json document;

    static SubtreeSnapshot Capture(GameObject &root);
    // Recreates the subtree with the same IDs, in the same place. False when it could not.
    bool Restore(EditContext &context) const;
};

// Objects that were created (new, duplicated, pasted, instantiated). The objects must
// already exist when the command is made; undo destroys them and redo recreates them
// with the same IDs.
class CreateObjectsCommand : public Command
{
  public:
    CreateObjectsCommand(std::string name, const std::vector<GameObject *> &roots);
    const char *GetName() const override
    {
        return m_name.c_str();
    }
    void Do(EditContext &context) override;
    void Undo(EditContext &context) override;

  private:
    std::string m_name;
    std::vector<SubtreeSnapshot> m_snapshots;
};

// Deletes objects (with their subtrees) and restores them on undo, main camera included.
class DeleteObjectsCommand : public Command
{
  public:
    // `roots` must not contain an object together with one of its ancestors.
    DeleteObjectsCommand(EditContext &context, const std::vector<GameObject *> &roots);
    const char *GetName() const override
    {
        return "Delete";
    }
    void Do(EditContext &context) override;
    void Undo(EditContext &context) override;

  private:
    std::vector<SubtreeSnapshot> m_snapshots;
    uint64_t m_mainCamera = 0;
};

// Swaps one subtree for another version of itself (unpacking a prefab instance, turning
// an object into an instance). Both snapshots have the same root ID and place.
class ReplaceObjectCommand : public Command
{
  public:
    ReplaceObjectCommand(std::string name, SubtreeSnapshot before, SubtreeSnapshot after)
        : m_name(std::move(name)), m_before(std::move(before)), m_after(std::move(after))
    {
    }
    const char *GetName() const override
    {
        return m_name.c_str();
    }
    void Do(EditContext &context) override
    {
        Swap(context, m_before, m_after);
    }
    void Undo(EditContext &context) override
    {
        Swap(context, m_after, m_before);
    }

  private:
    static void Swap(EditContext &context, const SubtreeSnapshot &from, const SubtreeSnapshot &to);

    std::string m_name;
    SubtreeSnapshot m_before;
    SubtreeSnapshot m_after;
};

class SetMainCameraCommand : public Command
{
  public:
    SetMainCameraCommand(uint64_t before, uint64_t after) : m_before(before), m_after(after)
    {
    }
    const char *GetName() const override
    {
        return "Set main camera";
    }
    void Do(EditContext &context) override;
    void Undo(EditContext &context) override;

  private:
    uint64_t m_before = 0;
    uint64_t m_after = 0;
};

} // namespace Geni::Editor
