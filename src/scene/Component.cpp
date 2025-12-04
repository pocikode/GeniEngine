#include "scene/Component.h"

namespace Orbis
{

GameObject *Component::GetOwner()
{
    return m_owner;
}

} // namespace Orbis