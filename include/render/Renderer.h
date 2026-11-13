#pragma once

#include "core/Common.h"
#include "render/Cascades.h"
#include "render/Mesh.h"
#include "render/RenderQueue.h"
#include "render/ShaderInterface.h"
#include "render/Sprite.h"
#include "render/TimeOfDay.h"
#include "render/Weather.h"
#include "rhi/Handle.h"
#include "rhi/Types.h"
#include <array>
#include <cstdint>
#include <glm/mat4x4.hpp>
#include <map>
#include <memory>
#include <tuple>
#include <vector>

namespace Geni
{

namespace rhi
{
class Device;
class CommandList;
} // namespace rhi

class Material;
class Renderer;
struct SpriteVertex;
class Shader;
class Texture;

// The curve that maps HDR scene colour to the display.
enum class Tonemapper : uint8_t
{
    // ACES filmic: highlights roll off, colours are slightly desaturated (3D scenes).
    Aces,
    // Clamped, otherwise unchanged: with exposure 1 a sprite's colours reach the screen as
    // painted (2D games, docs/design/0017).
    None
};

// Art-direction and quality settings for renderer v1. Mobile quality tiers (M7) and the
// CI benchmark change these; the defaults are the desktop baseline.
struct RendererConfig
{
    // 1 turns MSAA off.
    uint32_t msaaSamples = 4;
    // Cascaded shadow maps.
    bool shadows = true;
    uint32_t cascadeCount = 4;
    uint32_t shadowMapSize = 2048;
    // Only this near part of the camera frustum casts and receives shadows.
    float shadowDistance = 120.0f;
    // How far beyond a cascade, towards the sun, objects are still drawn into it as
    // casters. Covers tall buildings over a narrow first cascade.
    float shadowCasterDistance = 200.0f;
    // Clustered forward lighting.
    uint32_t clusterCountX = 16;
    uint32_t clusterCountY = 9;
    uint32_t clusterCountZ = 24;
    uint32_t maxLightsPerCluster = ShaderInterface::MaxLightsPerCluster;
    // Tonemapping and atmosphere.
    float exposure = 1.0f;
    Tonemapper tonemapper = Tonemapper::Aces;
    // false: no sky; the scene pass is cleared to clearColor (sRGB) instead.
    bool sky = true;
    glm::vec3 clearColor{0.0f};
    glm::vec3 skyZenith{0.05f, 0.13f, 0.32f};
    glm::vec3 skyHorizon{0.38f, 0.50f, 0.68f};
    glm::vec3 fogColor{0.44f, 0.55f, 0.70f};
    float fogDensity = 0.0018f;
    float ambientIntensity = 0.15f;
    // Mesh LOD selection (docs/design/0007): each object draws the coarsest LOD whose
    // simplification error projects to at most this many pixels. 0 always draws LOD 0.
    float lodErrorPixels = 1.0f;
};

// The part of the backbuffer the scene is drawn into, in pixels from the top-left corner.
// A zero width or height means the whole backbuffer (the default). The editor sets it to
// the area its panels leave free.
struct SceneViewport
{
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t width = 0;
    uint32_t height = 0;
};

// What the most recent frame did, for the debug overlay and the benchmark.
struct RenderStats
{
    uint32_t submitted = 0;
    uint32_t visible = 0;
    uint32_t culled = 0;
    // Draw calls for scene objects (instanced groups), and for the sky and tonemap.
    uint32_t mainDraws = 0;
    uint32_t fullscreenDraws = 0;
    uint32_t shadowDraws = 0;
    uint32_t instances = 0;
    uint32_t clusteredLights = 0;
    uint32_t overflowLights = 0;
    // 2D (docs/design/0017): quads drawn, sprite commands culled, and indexed draws.
    uint32_t sprites = 0;
    uint32_t spritesCulled = 0;
    uint32_t spriteBatches = 0;
    // Visible instances drawn at each LOD (index 0 is the full mesh).
    std::array<uint32_t, MaxMeshLods> lodInstances{};
};

// Turns the frame's render commands into the renderer-v1 frame: cascaded shadows, an HDR
// MSAA pass with sky and clustered PBR draws, then tonemapping into the backbuffer. The
// tonemap pass is left open so Dear ImGui draws into it.
class Renderer
{
  public:
    bool Init(rhi::Device *device, const RendererConfig &config = RendererConfig());
    // Releases GPU objects. Must run before the device is destroyed.
    void Shutdown();

    rhi::Device *GetDevice() const
    {
        return m_device;
    }
    const RendererConfig &GetConfig() const
    {
        return m_config;
    }
    const RenderStats &GetRenderStats() const
    {
        return m_stats;
    }
    // Settings that need no new GPU objects can change between frames.
    void SetTonemapper(Tonemapper tonemapper)
    {
        m_config.tonemapper = tonemapper;
    }
    void SetExposure(float exposure)
    {
        m_config.exposure = exposure;
    }
    // With `sky` off, the scene pass is cleared to `clearColor` (sRGB).
    void SetBackground(bool sky, const glm::vec3 &clearColor = glm::vec3(0.0f))
    {
        m_config.sky = sky;
        m_config.clearColor = clearColor;
    }
    // Replaces the config's sky, fog and ambient values for the next RenderFrame only
    // (docs/design/0018). Whoever drives them sets them every frame; when nobody does,
    // the config values come back on their own.
    void SetAtmosphereOverride(const Atmosphere &atmosphere)
    {
        m_atmosphereOverride = atmosphere;
        m_hasAtmosphereOverride = true;
    }
    // Weather for the next RenderFrame only (docs/design/0020), applied on top of the
    // atmosphere: cloud cover dims the sun and greys the sky, fog thickens.
    void SetWeather(const WeatherState &weather)
    {
        m_weather = weather;
        m_hasWeather = true;
    }
    // What the last RenderFrame used (clear when nobody set it).
    const WeatherState &GetFrameWeather() const
    {
        return m_frameWeather;
    }
    // The config's sky, fog and ambient values.
    Atmosphere GetConfigAtmosphere() const;
    // What the last RenderFrame used.
    const Atmosphere &GetFrameAtmosphere() const
    {
        return m_frameAtmosphere;
    }

    // The HDR targets take the viewport's size and the tonemap pass draws into its
    // rectangle; the rest of the backbuffer is cleared to black.
    void SetSceneViewport(const SceneViewport &viewport)
    {
        m_sceneViewport = viewport;
    }
    // The rectangle the scene is drawn into this frame: the viewport clipped to the
    // backbuffer, or the whole backbuffer. Cameras take their aspect ratio from it.
    SceneViewport GetSceneRect() const;

    // Built-in PBR shader (glTF metallic-roughness). Material parameters are
    // `uBaseColorFactor`, `uMetallicRoughness`, `uEmissiveFactor`,
    // `uNormalScaleOcclusion` and the texture names `baseColorTexture`,
    // `metallicRoughnessTexture`, `normalTexture`, `occlusionTexture`, `emissiveTexture`.
    const std::shared_ptr<Shader> &GetDefaultShader() const
    {
        return m_defaultShader;
    }
    // The built-in shader for skinned meshes (same surface, bone palette applied).
    const std::shared_ptr<Shader> &GetSkinnedShader() const
    {
        return m_skinnedShader;
    }
    // The terrain shader (docs/design/0022), "builtin:terrain" in cooked materials.
    const std::shared_ptr<Shader> &GetTerrainShader() const
    {
        return m_terrainShader;
    }
    rhi::SamplerHandle GetDefaultSampler() const
    {
        return m_sampler;
    }

    // Runs one frame inside the command buffer, starting with the shadow passes and
    // ending with the tonemap pass still open. `commands` may be empty (sky only).
    // `sprites` are drawn after the opaque meshes, in the same pass.
    void RenderFrame(
        rhi::CommandList &cmd, const std::vector<RenderCommand> &commands, const CameraData &camera,
        const std::vector<LightData> &lights, const std::vector<SpriteCommand> &sprites = std::vector<SpriteCommand>()
    );

    // Destroys the cached pipelines made from a shader (called when it is destroyed).
    void ReleasePipelines(uint64_t shaderId);
    size_t GetPipelineCount() const
    {
        return m_mainPipelines.size() + m_shadowPipelines.size();
    }

  private:
    struct FrameResources
    {
        // Frame set: uniforms and the clustered light data.
        rhi::BufferHandle frameBuffer;
        rhi::BufferHandle lightsBuffer;
        rhi::BufferHandle clusterGridBuffer;
        rhi::BufferHandle clusterIndicesBuffer;
        rhi::BindGroupHandle frameGroup;
        uint64_t lightsCapacity = 0;
        uint64_t gridCapacity = 0;
        uint64_t indexCapacity = 0;
        // Samples this frame slot's resolved HDR image in the tonemap pass.
        rhi::BindGroupHandle tonemapGroup;

        // Per-instance transforms (binding 1), one mat4 per instance.
        rhi::BufferHandle instanceBuffer;
        uint64_t instanceCapacity = 0;

        // Bones and per-draw uniforms addressed by dynamic offset.
        rhi::BufferHandle drawBuffer;
        rhi::BindGroupHandle drawGroup;
        uint64_t drawCapacity = 0;
        uint64_t drawCursor = 0;

        // HDR scene targets and their MSAA companions.
        rhi::TextureHandle hdrColor;  // single-sample, sampled by the tonemap pass
        rhi::TextureHandle msaaColor; // multisampled, empty when MSAA is off
        rhi::TextureHandle depth;     // same sample count as msaaColor
        uint32_t width = 0;
        uint32_t height = 0;

        // Sprite vertices (4 per quad), rewritten every frame.
        rhi::BufferHandle spriteVertexBuffer;
        uint64_t spriteQuadCapacity = 0;

        // Per-cascade instance streams: model matrix plus the cascade index (80 bytes).
        std::array<rhi::BufferHandle, ShaderInterface::Cascades> shadowInstanceBuffers{};
        std::array<uint64_t, ShaderInterface::Cascades> shadowInstanceCapacities{};
    };

    struct DrawItem
    {
        const RenderCommand *command = nullptr;
        uint64_t shaderId = 0;
        const Material *material = nullptr;
        const Mesh *mesh = nullptr;
        bool skinned = false;
        // LOD and the index range it draws (indexCount 0: non-indexed mesh).
        uint32_t lod = 0;
        uint32_t firstIndex = 0;
        uint32_t indexCount = 0;
        uint32_t instanceIndex = 0;
        uint32_t boneOffset = 0;
        float distance = 0.0f;
        // World-space bounding sphere (radius 0: unknown, never culled).
        glm::vec3 center{0.0f};
        float radius = 0.0f;
    };

    // A run of sorted sprite quads that share a texture and filter: one indexed draw.
    struct SpriteBatch
    {
        rhi::BindGroupHandle group;
        SpriteBlend blend = SpriteBlend::Alpha;
        bool lit = false;
        uint32_t firstQuad = 0;
        uint32_t quadCount = 0;
    };

    struct SpriteGroupKey
    {
        uint32_t index = 0;
        uint32_t generation = 0;
        SpriteFilter filter = SpriteFilter::Linear;
        bool operator<(const SpriteGroupKey &o) const
        {
            return std::tie(index, generation, filter) < std::tie(o.index, o.generation, o.filter);
        }
    };
    struct SpriteGroup
    {
        rhi::BindGroupHandle group;
        uint64_t lastUsedFrame = 0;
    };

    struct MainPipelineKey
    {
        uint64_t shader = 0;
        uint64_t layout = 0;
        uint8_t topology = 0;
        bool operator<(const MainPipelineKey &o) const
        {
            return std::tie(shader, layout, topology) < std::tie(o.shader, o.layout, o.topology);
        }
    };

    struct ShadowPipelineKey
    {
        uint64_t layout = 0;
        uint8_t topology = 0;
        bool skinned = false;
        bool operator<(const ShadowPipelineKey &o) const
        {
            return std::tie(layout, topology, skinned) < std::tie(o.layout, o.topology, o.skinned);
        }
    };

    bool CreateFrameResources(FrameResources &frame, uint32_t width, uint32_t height);
    void DestroyFrameResources(FrameResources &frame);
    bool CreateShadowMaps();
    bool EnsureInstanceCapacity(FrameResources &frame, uint64_t instances);
    bool EnsureShadowInstanceCapacity(FrameResources &frame, uint32_t cascade, uint64_t instances);
    bool EnsureDrawCapacity(FrameResources &frame, uint64_t capacity);
    bool EnsureClusterCapacity(FrameResources &frame, uint64_t lightCount, uint64_t clusterCount, uint64_t indexCount);
    void CreateFrameBindGroup(FrameResources &frame);

    void PrepareFrame(FrameResources &frame, const CameraData &camera, const std::vector<LightData> &lights);
    // Chooses the item's LOD from its projected simplification error.
    void SelectLod(DrawItem &item, float pixelsPerUnitAtOneMetre) const;
    // Shadow, HDR scene passes. False when nothing was drawn (resources missing).
    bool RenderScene(
        rhi::CommandList &cmd, const std::vector<RenderCommand> &commands, const CameraData &camera,
        const std::vector<LightData> &lights, const std::vector<SpriteCommand> &sprites
    );
    // `casters` is every submitted object, not only those the camera sees.
    void RenderShadows(rhi::CommandList &cmd, std::vector<DrawItem> &casters);
    void RenderMain(rhi::CommandList &cmd, const std::vector<DrawItem> &items);
    // Culls, sorts and writes this frame's sprite vertices; fills m_spriteBatches.
    bool PrepareSprites(FrameResources &frame, const std::vector<SpriteCommand> &sprites, const CameraData &camera);
    void RenderSprites(rhi::CommandList &cmd, FrameResources &frame);
    // Writes a billboard command's quads facing the camera (sorted back to front when
    // alpha-blended) at `out`, four vertices each.
    void WriteBillboards(
        const SpriteCommand &command, const glm::mat4 &relativeModel, const CameraData &camera, float decode,
        SpriteVertex *out
    );
    rhi::BindGroupHandle GetSpriteGroup(const Texture *texture, SpriteFilter filter);
    bool EnsureSpriteCapacity(FrameResources &frame, uint64_t quads);
    // Always opens the backbuffer pass; draws the HDR image only when `sceneDrawn`.
    void RenderTonemap(rhi::CommandList &cmd, bool sceneDrawn);

    rhi::PipelineHandle GetMainPipeline(const Shader &shader, const Mesh &mesh);
    rhi::PipelineHandle GetShadowPipeline(const Mesh &mesh, bool skinned);
    rhi::PipelineHandle GetSkyPipeline();
    rhi::PipelineHandle GetTonemapPipeline();
    rhi::PipelineHandle GetSpritePipeline(SpriteBlend blend = SpriteBlend::Alpha, bool lit = false);

    void BuildVertexInput(
        const Mesh &mesh, rhi::VertexLayoutDesc &layout, const std::vector<uint32_t> *shaderInputs
    ) const;
    uint64_t Align(uint64_t size) const;

    rhi::Device *m_device = nullptr;
    RendererConfig m_config;
    RenderStats m_stats;
    SceneViewport m_sceneViewport;

    rhi::BindGroupLayoutHandle m_frameLayout;
    rhi::BindGroupLayoutHandle m_drawLayout;
    rhi::BindGroupLayoutHandle m_tonemapLayout;
    // Used as the layout of a descriptor set a pipeline does not use (set 1 of the
    // skinned shadow pass); the RHI has no "null layout".
    rhi::BindGroupLayoutHandle m_emptyLayout;
    rhi::SamplerHandle m_sampler;
    rhi::SamplerHandle m_shadowSampler;

    std::shared_ptr<Shader> m_defaultShader;
    std::shared_ptr<Shader> m_skinnedShader;
    std::shared_ptr<Shader> m_terrainShader;
    std::shared_ptr<Shader> m_skyShader;
    rhi::ShaderModuleHandle m_shadowVertexModule;
    rhi::ShaderModuleHandle m_shadowSkinnedVertexModule;
    rhi::ShaderModuleHandle m_shadowFragmentModule;
    rhi::ShaderModuleHandle m_tonemapVertexModule;
    rhi::ShaderModuleHandle m_tonemapFragmentModule;

    rhi::PipelineHandle m_skyPipeline;
    rhi::PipelineHandle m_tonemapPipeline;

    // 2D (docs/design/0017).
    rhi::ShaderModuleHandle m_spriteVertexModule;
    rhi::ShaderModuleHandle m_spriteFragmentModule;
    rhi::ShaderModuleHandle m_spriteLitVertexModule;
    rhi::ShaderModuleHandle m_spriteLitFragmentModule;
    // [blend][lit] (docs/design/0019, 0021).
    rhi::PipelineHandle m_spritePipelines[2][2];
    // Back-to-front order of one alpha-blended billboard command's quads (reused).
    std::vector<std::pair<float, uint32_t>> m_billboardOrder;
    rhi::SamplerHandle m_spriteLinearSampler;
    rhi::SamplerHandle m_spriteNearestSampler;
    rhi::TextureHandle m_spriteWhiteTexture;
    // Quad indices (0 1 2, 0 2 3 per quad), shared by every frame; replaced when it grows.
    rhi::BufferHandle m_spriteIndexBuffer;
    uint64_t m_spriteIndexCapacity = 0;
    std::map<SpriteGroupKey, SpriteGroup> m_spriteGroups;
    std::vector<SpriteBatch> m_spriteBatches;
    uint64_t m_frameCounter = 0;

    std::vector<FrameResources> m_frames;
    // One map per cascade, shared by every frame slot: a pass's external dependency orders
    // the next frame's depth writes after this frame's shader reads on the one queue.
    std::array<rhi::TextureHandle, ShaderInterface::Cascades> m_shadowMaps{};
    uint32_t m_alignment = 256;

    std::map<MainPipelineKey, rhi::PipelineHandle> m_mainPipelines;
    std::map<ShadowPipelineKey, rhi::PipelineHandle> m_shadowPipelines;

    bool m_hasSun = false;
    LightData m_sun;
    Atmosphere m_atmosphereOverride;
    bool m_hasAtmosphereOverride = false;
    Atmosphere m_frameAtmosphere;
    WeatherState m_weather;
    bool m_hasWeather = false;
    WeatherState m_frameWeather;
    // This frame's lights with night-only ones scaled by the darkness (reused).
    std::vector<LightData> m_frameLights;
    CascadeSetup m_cascades;
    std::vector<ShaderInterface::GpuLight> m_clusteredLights;
    std::vector<ShaderInterface::ClusterRange> m_clusters;
    std::vector<uint32_t> m_clusterIndices;
    // Camera-relative rendering (docs/design/0007): everything uploaded this frame is
    // relative to this point, the camera position, so shaders see the camera at zero.
    glm::vec3 m_renderOrigin{0.0f};
};

} // namespace Geni