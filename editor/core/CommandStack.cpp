#include "CommandStack.h"

namespace Geni::Editor
{

void CompositeCommand::Do(EditContext &context)
{
    for (auto &command : m_commands)
    {
        command->Do(context);
    }
}

void CompositeCommand::Undo(EditContext &context)
{
    for (auto it = m_commands.rbegin(); it != m_commands.rend(); ++it)
    {
        (*it)->Undo(context);
    }
}

void CommandStack::Execute(std::unique_ptr<Command> command, EditContext &context)
{
    if (!command)
    {
        return;
    }
    command->Do(context);
    Push(std::move(command));
}

void CommandStack::Push(std::unique_ptr<Command> command)
{
    if (!command)
    {
        return;
    }
    // A new edit discards what was undone. If the saved state was in there, it is gone.
    if (m_cursor < m_commands.size())
    {
        m_commands.resize(m_cursor);
        if (m_savedCursor > static_cast<long>(m_cursor))
        {
            m_savedCursor = -1;
        }
    }
    m_commands.push_back(std::move(command));
    ++m_cursor;

    if (m_commands.size() > m_limit)
    {
        const size_t drop = m_commands.size() - m_limit;
        m_commands.erase(m_commands.begin(), m_commands.begin() + static_cast<std::ptrdiff_t>(drop));
        m_cursor -= drop;
        m_savedCursor = m_savedCursor < static_cast<long>(drop) ? -1 : m_savedCursor - static_cast<long>(drop);
    }
}

bool CommandStack::Undo(EditContext &context)
{
    if (!CanUndo())
    {
        return false;
    }
    m_commands[--m_cursor]->Undo(context);
    return true;
}

bool CommandStack::Redo(EditContext &context)
{
    if (!CanRedo())
    {
        return false;
    }
    m_commands[m_cursor++]->Do(context);
    return true;
}

const char *CommandStack::GetUndoName() const
{
    return CanUndo() ? m_commands[m_cursor - 1]->GetName() : "";
}

const char *CommandStack::GetRedoName() const
{
    return CanRedo() ? m_commands[m_cursor]->GetName() : "";
}

void CommandStack::Clear()
{
    m_commands.clear();
    m_cursor = 0;
    m_savedCursor = 0;
}

void CommandStack::MarkSaved()
{
    m_savedCursor = static_cast<long>(m_cursor);
}

} // namespace Geni::Editor
