#include "scene/components/SpriteComponent.h"
#include "assets/AssetManager.h"
#include "core/Engine.h"
#include "graphics/Texture.h"
#include "render/RenderQueue.h"
#include "scene/GameObject.h"
#include "scene/Reflection.h"
#include <cmath>

namespace Geni
{

Texture *SpriteComponent::GetTexture() const
{
    if (m_atlasID.IsValid())
    {
        return m_atlasTexture.IsValid() ? m_atlasTexture.Get() : nullptr;
    }
    // The handle first: after a hot reload its slot holds the new texture.
    return m_textureHandle.IsValid() ? m_textureHandle.Get() : m_texture.get();
}

void SpriteComponent::SetTexture(const std::shared_ptr<Texture> &texture)
{
    m_texture = texture;
    m_textureHandle = AssetHandle<Texture>();
    m_textureID = texture ? texture->GetAssetID() : AssetID();
}

void SpriteComponent::SetTexture(const AssetHandle<Texture> &texture)
{
    m_textureHandle = texture;
    m_texture.reset();
    m_textureID = texture.GetID();
}

void SpriteComponent::SetTextureID(AssetID id)
{
    if (id != m_textureID)
    {
        m_textureHandle.Reset();
        m_texture.reset();
    }
    m_textureID = id;
}

void SpriteComponent::SetAtlasID(AssetID id)
{
    if (id != m_atlasID)
    {
        m_atlasHandle.Reset();
        m_atlasTexture.Reset();
    }
    m_atlasID = id;
}

void SpriteComponent::SetAtlas(const AssetHandle<AtlasAsset> &atlas, const AssetHandle<Texture> &texture)
{
    m_atlasHandle = atlas;
    m_atlasTexture = texture;
    m_atlasID = atlas.GetID();
}

bool SpriteComponent::HasAtlasRegion() const
{
    glm::vec2 a;
    glm::vec2 b;
    return m_atlasHandle.IsReady() && m_atlasHandle->atlas.FindRegion(m_region, a, b);
}

void SpriteComponent::SetFrame(int frame)
{
    const int count = GetFrameCount();
    frame %= count;
    m_frame = frame < 0 ? frame + count : frame;
}

void SpriteComponent::BuildQuad()
{
    m_quad.min = -m_pivot * m_size;
    m_quad.max = m_quad.min + m_size;

    // The whole texture, or the atlas region; columns and rows cut it into frames.
    glm::vec2 regionTopLeft(0.0f);
    glm::vec2 regionBottomRight(1.0f);
    if (m_atlasID.IsValid() && m_atlasHandle.IsReady())
    {
        m_atlasHandle->atlas.FindRegion(m_region, regionTopLeft, regionBottomRight);
    }
    const glm::vec2 regionSize = regionBottomRight - regionTopLeft;
    const int frame = m_frame % GetFrameCount();
    const glm::vec2 cell = regionSize / glm::vec2(static_cast<float>(m_columns), static_cast<float>(m_rows));
    glm::vec2 topLeft =
        regionTopLeft +
        glm::vec2(static_cast<float>(frame % m_columns) * cell.x, static_cast<float>(frame / m_columns) * cell.y);
    glm::vec2 bottomRight = topLeft + cell;
    if (m_flipX)
    {
        std::swap(topLeft.x, bottomRight.x);
    }
    if (m_flipY)
    {
        std::swap(topLeft.y, bottomRight.y);
    }
    m_quad.uvTopLeft = topLeft;
    m_quad.uvBottomRight = bottomRight;
    m_quad.color = SrgbToLinear(m_color);
}

void SpriteComponent::Update(float deltaTime)
{
    (void)deltaTime;
    if (!m_visible || m_color.a <= 0.0f)
    {
        return;
    }
    Texture *texture = GetTexture();
    // A texture (or atlas) that is still loading draws nothing (not a white box).
    if (!texture && (m_textureID.IsValid() || m_textureHandle.IsValid() || m_atlasID.IsValid()))
    {
        return;
    }
    BuildQuad();

    SpriteCommand command;
    command.texture = texture;
    command.filter = m_filter;
    command.quads = &m_quad;
    command.quadCount = 1;
    command.modelMatrix = GetOwner()->GetWorldTransform();
    command.boundsCenter = glm::vec3((m_quad.min + m_quad.max) * 0.5f, 0.0f);
    command.boundsRadius = 0.5f * glm::length(m_size);
    command.layer = m_layer;
    Engine::GetInstance().GetRenderQueue().SubmitSprites(command);
}

void SpriteComponent::ResolveAssets(SerializationContext &context)
{
    if (!context.assets)
    {
        return;
    }
    if (m_atlasID.IsValid() && !m_atlasHandle.IsValid())
    {
        // The atlas is a few hundred bytes of JSON: read it now; its texture streams in.
        const AssetHandle<AtlasAsset> atlas = context.assets->LoadAtlas(m_atlasID, false);
        if (atlas.IsReady())
        {
            m_atlasHandle = atlas;
            m_atlasTexture = context.assets->LoadTexture(atlas->atlas.texture, true);
        }
    }
    else if (!m_atlasID.IsValid() && m_textureID.IsValid() && !m_textureHandle.IsValid())
    {
        SetTexture(context.assets->LoadTexture(m_textureID, true));
    }
}

void SpriteAnimatorComponent::Play(int first, int count, float fps, bool loop)
{
    first = first < 0 ? 0 : first;
    count = count < 1 ? 1 : count;
    if (m_playing && first == m_firstFrame && count == m_frameCount && loop == m_loop)
    {
        m_fps = fps;
        return;
    }
    m_firstFrame = first;
    m_frameCount = count;
    m_fps = fps < 0.0f ? 0.0f : fps;
    m_loop = loop;
    m_playing = true;
    m_time = 0.0f;
    if (auto *sprite = GetOwner()->GetComponent<SpriteComponent>())
    {
        sprite->SetFrame(m_firstFrame);
    }
}

void SpriteAnimatorComponent::Update(float deltaTime)
{
    auto *sprite = GetOwner()->GetComponent<SpriteComponent>();
    if (!sprite || !m_playing)
    {
        return;
    }
    m_time += deltaTime;
    int index = static_cast<int>(std::floor(m_time * m_fps));
    if (m_loop)
    {
        index %= m_frameCount;
        // Keep the clock small so a long-running loop does not lose precision.
        const float period = m_fps > 0.0f ? static_cast<float>(m_frameCount) / m_fps : 0.0f;
        if (period > 0.0f && m_time >= period)
        {
            m_time = std::fmod(m_time, period);
        }
    }
    else if (index >= m_frameCount)
    {
        index = m_frameCount - 1;
    }
    sprite->SetFrame(m_firstFrame + index);
}

namespace
{
const bool s_spriteRegistered = RegisterComponentType<SpriteComponent>(
    "SpriteComponent",
    {
        AssetIDProperty<SpriteComponent>(
            "texture", AssetType::Texture, &SpriteComponent::GetTextureID, &SpriteComponent::SetTextureID
        ),
        AssetIDProperty<SpriteComponent>(
            "atlas", AssetType::Atlas, &SpriteComponent::GetAtlasID, &SpriteComponent::SetAtlasID
        ),
        StringProperty<SpriteComponent>("region", &SpriteComponent::GetRegion, &SpriteComponent::SetRegion),
        Vec2Property<SpriteComponent>("size", &SpriteComponent::GetSize, &SpriteComponent::SetSize),
        Vec2Property<SpriteComponent>("pivot", &SpriteComponent::GetPivot, &SpriteComponent::SetPivot),
        Vec4Property<SpriteComponent>("color", &SpriteComponent::GetColor, &SpriteComponent::SetColor),
        IntProperty<SpriteComponent>("columns", &SpriteComponent::GetColumns, &SpriteComponent::SetColumns),
        IntProperty<SpriteComponent>("rows", &SpriteComponent::GetRows, &SpriteComponent::SetRows),
        IntProperty<SpriteComponent>("frame", &SpriteComponent::GetFrame, &SpriteComponent::SetFrame),
        BoolProperty<SpriteComponent>("flipX", &SpriteComponent::GetFlipX, &SpriteComponent::SetFlipX),
        BoolProperty<SpriteComponent>("flipY", &SpriteComponent::GetFlipY, &SpriteComponent::SetFlipY),
        BoolProperty<SpriteComponent>("visible", &SpriteComponent::IsVisible, &SpriteComponent::SetVisible),
        IntProperty<SpriteComponent>("layer", &SpriteComponent::GetLayer, &SpriteComponent::SetLayer),
        EnumProperty<SpriteComponent, SpriteFilter>(
            "filter", &SpriteComponent::GetFilter, &SpriteComponent::SetFilter,
            {{SpriteFilter::Linear, "linear"}, {SpriteFilter::Nearest, "nearest"}}
        ),
    }
);

const bool s_animatorRegistered = RegisterComponentType<SpriteAnimatorComponent>(
    "SpriteAnimatorComponent",
    {
        IntProperty<SpriteAnimatorComponent>(
            "firstFrame", &SpriteAnimatorComponent::GetFirstFrame, &SpriteAnimatorComponent::SetFirstFrame
        ),
        IntProperty<SpriteAnimatorComponent>(
            "frameCount", &SpriteAnimatorComponent::GetClipFrameCount, &SpriteAnimatorComponent::SetClipFrameCount
        ),
        FloatProperty<SpriteAnimatorComponent>(
            "fps", &SpriteAnimatorComponent::GetFps, &SpriteAnimatorComponent::SetFps
        ),
        BoolProperty<SpriteAnimatorComponent>(
            "loop", &SpriteAnimatorComponent::GetLoop, &SpriteAnimatorComponent::SetLoop
        ),
        BoolProperty<SpriteAnimatorComponent>(
            "playing", &SpriteAnimatorComponent::GetPlaying, &SpriteAnimatorComponent::SetPlaying
        ),
    }
);
} // namespace

} // namespace Geni

namespace Geni
{
void LinkSpriteComponent()
{
}
} // namespace Geni
