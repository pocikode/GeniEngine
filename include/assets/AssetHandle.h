#pragma once

#include "assets/Asset.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace Geni
{

enum class AssetLoadState
{
    NotLoaded,
    Pending,
    Ready,
    Failed
};

namespace Detail
{
struct AssetSlotBase
{
    AssetID id;
    AssetType type = AssetType::Unknown;
    std::atomic<AssetLoadState> state{AssetLoadState::NotLoaded};
    std::string error;
    // Where the asset was read from (relative to the assets folder), its source name and
    // the file's modification time then: AssetManager::ReloadChanged compares them.
    std::string path;
    std::string source;
    int64_t fileTime = 0;
    // GPU memory the asset holds (vertex and index buffers, texture levels); 0 for assets
    // without GPU data. AssetManager::GetResidentGpuBytes sums it.
    uint64_t gpuBytes = 0;
    // Re-reads the file and swaps the new object into this slot. Set by AssetManager.
    std::function<bool()> reload;
    virtual ~AssetSlotBase() = default;
    // References to the loaded object held outside this slot (0 when nothing is loaded).
    virtual long ExternalAssetReferences() const = 0;
};

template <typename T> struct AssetSlot : AssetSlotBase
{
    std::shared_ptr<T> asset;

    long ExternalAssetReferences() const override
    {
        return asset ? asset.use_count() - 1 : 0;
    }
};
} // namespace Detail

// A reference-counted handle to an asynchronously loaded asset. The handle points at a
// slot owned by AssetManager; `Get()` returns nullptr until the worker has decoded the
// file and the main thread has created the GPU object. Copying a handle keeps the slot
// (and the asset) alive. The cache keeps one reference of its own until
// AssetManager::CollectGarbage finds that nothing else uses the asset.
template <typename T> class AssetHandle
{
  public:
    AssetHandle() = default;
    explicit AssetHandle(std::shared_ptr<Detail::AssetSlot<T>> slot) : m_slot(std::move(slot))
    {
    }

    bool IsValid() const
    {
        return m_slot != nullptr;
    }
    bool IsReady() const
    {
        return Get() != nullptr;
    }
    bool HasFailed() const
    {
        return m_slot && m_slot->state.load(std::memory_order_acquire) == AssetLoadState::Failed;
    }
    bool IsPending() const
    {
        return m_slot && m_slot->state.load(std::memory_order_acquire) == AssetLoadState::Pending;
    }
    AssetID GetID() const
    {
        return m_slot ? m_slot->id : AssetID{};
    }
    const std::string &GetError() const
    {
        static const std::string empty;
        return m_slot ? m_slot->error : empty;
    }

    T *Get() const
    {
        return m_slot ? m_slot->asset.get() : nullptr;
    }
    T *operator->() const
    {
        return Get();
    }
    explicit operator bool() const
    {
        return IsReady();
    }
    std::shared_ptr<T> Shared() const
    {
        return m_slot ? m_slot->asset : nullptr;
    }

    void Reset()
    {
        m_slot.reset();
    }

    const std::shared_ptr<Detail::AssetSlot<T>> &GetSlot() const
    {
        return m_slot;
    }

  private:
    std::shared_ptr<Detail::AssetSlot<T>> m_slot;
};

} // namespace Geni
