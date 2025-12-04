#include "core/Application.h"

namespace Orbis
{

void Application::SetNeedsToBeClosed(bool value)
{
    m_needsToBeClosed = value;
}

bool Application::NeedsToBeClosed() const
{
    return m_needsToBeClosed;
}

} // namespace Orbis
