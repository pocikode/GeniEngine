#include "scene/Component.h"
#include <cstddef>

namespace Geni
{

size_t Component::nextID = 1;

void Component::Init()
{
}

GameObject *Component::GetOwner()
{
    return m_owner;
}

} // namespace Geni