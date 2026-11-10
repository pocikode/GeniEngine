#pragma once

#include "core/Assert.h"
#include <cstdint>
#include <utility>
#include <vector>

namespace Geni::rhi
{

// A reference to a GPU object: a slot index plus the generation of the object that lived
// in that slot when the handle was made. Generation 0 is never used, so a
// default-constructed handle is invalid. A handle to a destroyed object stays detectably
// stale even after its slot is reused.
template <typename Tag> struct Handle
{
    uint32_t index = 0;
    uint32_t generation = 0;

    bool IsValid() const
    {
        return generation != 0;
    }
    bool operator==(const Handle &other) const
    {
        return index == other.index && generation == other.generation;
    }
    bool operator!=(const Handle &other) const
    {
        return !(*this == other);
    }
};

using BufferHandle = Handle<struct BufferTag>;
using TextureHandle = Handle<struct TextureTag>;
using SamplerHandle = Handle<struct SamplerTag>;
using ShaderModuleHandle = Handle<struct ShaderModuleTag>;
using BindGroupLayoutHandle = Handle<struct BindGroupLayoutTag>;
using BindGroupHandle = Handle<struct BindGroupTag>;
using PipelineHandle = Handle<struct PipelineTag>;

// Storage for the objects behind one handle type. Backends keep their native objects
// here; Get asserts on a stale or invalid handle instead of returning garbage.
template <typename T, typename Tag> class HandlePool
{
  public:
    using HandleType = Handle<Tag>;

    HandleType Add(T value)
    {
        uint32_t index;
        if (!m_free.empty())
        {
            index = m_free.back();
            m_free.pop_back();
        }
        else
        {
            index = static_cast<uint32_t>(m_slots.size());
            m_slots.emplace_back();
        }
        Slot &slot = m_slots[index];
        slot.value = std::move(value);
        slot.alive = true;
        ++m_liveCount;
        return HandleType{index, slot.generation};
    }

    bool Contains(HandleType handle) const
    {
        return handle.IsValid() && handle.index < m_slots.size() && m_slots[handle.index].alive &&
               m_slots[handle.index].generation == handle.generation;
    }

    T *Get(HandleType handle)
    {
        GENI_ASSERT_MSG(
            Contains(handle), "stale or invalid handle (index %u, generation %u)", handle.index, handle.generation
        );
        return Contains(handle) ? &m_slots[handle.index].value : nullptr;
    }

    // Removes the object and returns it so the caller can destroy the native resource.
    // Returns false for a stale or invalid handle.
    bool Remove(HandleType handle, T &out)
    {
        if (!Contains(handle))
        {
            return false;
        }
        Slot &slot = m_slots[handle.index];
        out = std::move(slot.value);
        slot.value = T();
        slot.alive = false;
        // Skip 0 on wrap-around: it marks the invalid handle.
        slot.generation = slot.generation == UINT32_MAX ? 1 : slot.generation + 1;
        m_free.push_back(handle.index);
        --m_liveCount;
        return true;
    }

    size_t LiveCount() const
    {
        return m_liveCount;
    }

    // Calls fn(value) for every live object.
    template <typename Fn> void ForEach(Fn &&fn)
    {
        for (auto &slot : m_slots)
        {
            if (slot.alive)
            {
                fn(slot.value);
            }
        }
    }

    void Clear()
    {
        m_slots.clear();
        m_free.clear();
        m_liveCount = 0;
    }

  private:
    struct Slot
    {
        T value{};
        uint32_t generation = 1;
        bool alive = false;
    };

    std::vector<Slot> m_slots;
    std::vector<uint32_t> m_free;
    size_t m_liveCount = 0;
};

} // namespace Geni::rhi
