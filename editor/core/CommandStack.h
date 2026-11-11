#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace Geni
{
class AssetManager;
class Scene;
} // namespace Geni

namespace Geni::Editor
{

// What a command works on. Commands name objects by ID (GameObject::GetID) and carry
// JSON, never pointers, so they stay valid when objects are deleted and restored.
struct EditContext
{
    Scene *scene = nullptr;
    AssetManager *assets = nullptr;
};

// One undoable edit (docs/design/0006).
class Command
{
  public:
    virtual ~Command() = default;
    virtual const char *GetName() const = 0;
    // Applies the edit. Also what redo calls.
    virtual void Do(EditContext &context) = 0;
    virtual void Undo(EditContext &context) = 0;
};

// Several commands that undo and redo as one step.
class CompositeCommand : public Command
{
  public:
    explicit CompositeCommand(std::string name) : m_name(std::move(name))
    {
    }
    void Add(std::unique_ptr<Command> command)
    {
        if (command)
        {
            m_commands.push_back(std::move(command));
        }
    }
    bool IsEmpty() const
    {
        return m_commands.empty();
    }
    const char *GetName() const override
    {
        return m_name.c_str();
    }
    void Do(EditContext &context) override;
    void Undo(EditContext &context) override;

  private:
    std::string m_name;
    std::vector<std::unique_ptr<Command>> m_commands;
};

// Undo and redo history with a "saved" marker for the document's dirty flag.
class CommandStack
{
  public:
    explicit CommandStack(size_t limit = 256) : m_limit(limit)
    {
    }

    // Runs the command and records it.
    void Execute(std::unique_ptr<Command> command, EditContext &context);
    // Records a command whose effect is already in the scene (a finished drag).
    void Push(std::unique_ptr<Command> command);

    bool CanUndo() const
    {
        return m_cursor > 0;
    }
    bool CanRedo() const
    {
        return m_cursor < m_commands.size();
    }
    bool Undo(EditContext &context);
    bool Redo(EditContext &context);
    // Name of the command Undo / Redo would act on, or "".
    const char *GetUndoName() const;
    const char *GetRedoName() const;

    void Clear();
    // The document was saved in its current state.
    void MarkSaved();
    bool IsDirty() const
    {
        return m_savedCursor != static_cast<long>(m_cursor);
    }
    size_t GetCount() const
    {
        return m_commands.size();
    }

  private:
    std::vector<std::unique_ptr<Command>> m_commands;
    // Commands before the cursor are done, the ones from it on are undone.
    size_t m_cursor = 0;
    // Cursor value at the last save; -1 when that state can no longer be reached.
    long m_savedCursor = 0;
    size_t m_limit = 256;
};

} // namespace Geni::Editor
