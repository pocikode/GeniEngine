#pragma once

#include "assets/AssetHandle.h"
#include "render/Sprite.h"
#include "scene/Component.h"
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>
#include <memory>

namespace Geni
{

class Texture;

// A textured rectangle in the object's XY plane (docs/design/0017). The texture can be
// an atlas cut into `columns` x `rows` equal frames, numbered left to right, top to
// bottom; `frame` picks one (SpriteAnimatorComponent changes it over time).
class SpriteComponent : public Component
{
    COMPONENT(SpriteComponent);

  public:
    void Update(float deltaTime) override;
    // An edited scene is still drawn.
    void EditUpdate() override
    {
        Update(0.0f);
    }
    void ResolveAssets(SerializationContext &context) override;

    // nullptr until the texture has loaded (then the sprite is not drawn).
    Texture *GetTexture() const;
    void SetTexture(const std::shared_ptr<Texture> &texture);
    void SetTexture(const AssetHandle<Texture> &texture);
    AssetID GetTextureID() const
    {
        return m_textureID;
    }
    // Changing the ID drops the loaded texture; ResolveAssets loads the new one.
    void SetTextureID(AssetID id);

    // Size in world units, and the point of the rectangle at the object's origin
    // ((0, 0) bottom-left, (0.5, 0.5) centre, (0.5, 0) bottom middle).
    glm::vec2 GetSize() const
    {
        return m_size;
    }
    void SetSize(const glm::vec2 &size)
    {
        m_size = size;
    }
    glm::vec2 GetPivot() const
    {
        return m_pivot;
    }
    void SetPivot(const glm::vec2 &pivot)
    {
        m_pivot = pivot;
    }
    // sRGB colour and alpha the texture is multiplied by.
    glm::vec4 GetColor() const
    {
        return m_color;
    }
    void SetColor(const glm::vec4 &color)
    {
        m_color = color;
    }
    int GetColumns() const
    {
        return m_columns;
    }
    void SetColumns(int columns)
    {
        m_columns = columns < 1 ? 1 : columns;
    }
    int GetRows() const
    {
        return m_rows;
    }
    void SetRows(int rows)
    {
        m_rows = rows < 1 ? 1 : rows;
    }
    int GetFrameCount() const
    {
        return m_columns * m_rows;
    }
    int GetFrame() const
    {
        return m_frame;
    }
    // Wraps into 0..GetFrameCount()-1.
    void SetFrame(int frame);
    bool GetFlipX() const
    {
        return m_flipX;
    }
    void SetFlipX(bool flip)
    {
        m_flipX = flip;
    }
    bool GetFlipY() const
    {
        return m_flipY;
    }
    void SetFlipY(bool flip)
    {
        m_flipY = flip;
    }
    int GetLayer() const
    {
        return m_layer;
    }
    void SetLayer(int layer)
    {
        m_layer = layer;
    }
    SpriteFilter GetFilter() const
    {
        return m_filter;
    }
    void SetFilter(SpriteFilter filter)
    {
        m_filter = filter;
    }
    bool IsVisible() const
    {
        return m_visible;
    }
    void SetVisible(bool visible)
    {
        m_visible = visible;
    }

    // The quad as it was last submitted (for tests and tools).
    const SpriteQuad &GetQuad() const
    {
        return m_quad;
    }
    // Rebuilds the quad from the properties; Update does this before submitting.
    void BuildQuad();

  private:
    AssetHandle<Texture> m_textureHandle;
    std::shared_ptr<Texture> m_texture;
    AssetID m_textureID;
    glm::vec2 m_size{1.0f};
    glm::vec2 m_pivot{0.5f};
    glm::vec4 m_color{1.0f};
    int m_columns = 1;
    int m_rows = 1;
    int m_frame = 0;
    bool m_flipX = false;
    bool m_flipY = false;
    bool m_visible = true;
    int m_layer = 0;
    SpriteFilter m_filter = SpriteFilter::Linear;
    SpriteQuad m_quad;
};

// Plays a run of a SpriteComponent's frames (on the same object) at a fixed rate.
class SpriteAnimatorComponent : public Component
{
    COMPONENT(SpriteAnimatorComponent);

  public:
    void Update(float deltaTime) override;

    // Starts `count` frames from `first` at `fps`. Calling it again with the same clip
    // keeps playing from the current frame, so it can be called every frame.
    void Play(int first, int count, float fps, bool loop = true);
    void Stop()
    {
        m_playing = false;
    }
    bool IsPlaying() const
    {
        return m_playing;
    }
    // True once a non-looping clip has shown its last frame.
    bool IsFinished() const
    {
        return !m_loop && m_time * m_fps >= static_cast<float>(m_frameCount);
    }

    int GetFirstFrame() const
    {
        return m_firstFrame;
    }
    void SetFirstFrame(int frame)
    {
        m_firstFrame = frame < 0 ? 0 : frame;
    }
    int GetClipFrameCount() const
    {
        return m_frameCount;
    }
    void SetClipFrameCount(int count)
    {
        m_frameCount = count < 1 ? 1 : count;
    }
    float GetFps() const
    {
        return m_fps;
    }
    void SetFps(float fps)
    {
        m_fps = fps < 0.0f ? 0.0f : fps;
    }
    bool GetLoop() const
    {
        return m_loop;
    }
    void SetLoop(bool loop)
    {
        m_loop = loop;
    }
    bool GetPlaying() const
    {
        return m_playing;
    }
    void SetPlaying(bool playing)
    {
        m_playing = playing;
    }

  private:
    int m_firstFrame = 0;
    int m_frameCount = 1;
    float m_fps = 8.0f;
    bool m_loop = true;
    bool m_playing = true;
    float m_time = 0.0f;
};

} // namespace Geni
