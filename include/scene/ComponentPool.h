#pragma once

#include "scene/Component.h"

#include <cstddef>
#include <functional>
#include <vector>

namespace Geni
{

// A dense per-type index of the components in a scene. Component objects keep the
// addresses their owner gave them (GameObject still owns them), so iteration still
// dereferences one pointer per component; what this removes is the tree walk and the
// per-object component search. Each component stores its index in the pool, so removal
// is O(1) (swap with the last entry).
class ComponentPool
{
  public:
    void Add(Component *component)
    {
        if (component)
        {
            component->m_poolIndex = m_components.size();
            m_components.push_back(component);
        }
    }

    void Remove(Component *component)
    {
        if (!component)
        {
            return;
        }
        const size_t index = component->m_poolIndex;
        if (index >= m_components.size() || m_components[index] != component)
        {
            return;
        }
        Component *last = m_components.back();
        m_components[index] = last;
        last->m_poolIndex = index;
        m_components.pop_back();
        component->m_poolIndex = Component::NotInPool;
    }

    void Clear()
    {
        for (Component *component : m_components)
        {
            component->m_poolIndex = Component::NotInPool;
        }
        m_components.clear();
    }

    size_t Count() const
    {
        return m_components.size();
    }

    template <typename T, typename Fn> void ForEach(Fn &&fn) const
    {
        for (Component *component : m_components)
        {
            fn(*static_cast<T *>(component));
        }
    }

    const std::vector<Component *> &GetComponents() const
    {
        return m_components;
    }

  private:
    std::vector<Component *> m_components;
};

} // namespace Geni
