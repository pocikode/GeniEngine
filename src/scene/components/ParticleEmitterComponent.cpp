#include "scene/components/ParticleEmitterComponent.h"

#include "assets/AssetManager.h"
#include "core/Engine.h"
#include "graphics/Texture.h"
#include "render/RenderQueue.h"
#include "scene/GameObject.h"
#include "scene/Reflection.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/mat3x3.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <cmath>

namespace Geni
{

namespace
{

constexpr float kTwoPi = 6.28318530717959f;
// A hard ceiling so a typo in the inspector cannot allocate gigabytes.
constexpr int kParticleLimit = 1000000;

float NonNegative(float value)
{
    return std::isfinite(value) ? std::max(0.0f, value) : 0.0f;
}

// Any unit vector not parallel to `axis`, crossed into a basis perpendicular to it.
void Perpendiculars(const glm::vec3 &axis, glm::vec3 &u, glm::vec3 &v)
{
    const glm::vec3 helper = std::abs(axis.y) < 0.99f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
    u = glm::normalize(glm::cross(helper, axis));
    v = glm::cross(axis, u);
}

} // namespace

Texture *ParticleEmitterComponent::GetTexture() const
{
    return m_textureHandle.IsValid() ? m_textureHandle.Get() : m_texture.get();
}

void ParticleEmitterComponent::SetTexture(const std::shared_ptr<Texture> &texture)
{
    m_texture = texture;
    m_textureHandle = AssetHandle<Texture>();
    m_textureID = texture ? texture->GetAssetID() : AssetID();
}

void ParticleEmitterComponent::SetTexture(const AssetHandle<Texture> &texture)
{
    m_textureHandle = texture;
    m_texture.reset();
    m_textureID = texture.GetID();
}

void ParticleEmitterComponent::SetTextureID(AssetID id)
{
    if (id != m_textureID)
    {
        m_textureHandle.Reset();
        m_texture.reset();
    }
    m_textureID = id;
}

void ParticleEmitterComponent::ResolveAssets(SerializationContext &context)
{
    if (context.assets && m_textureID.IsValid() && !m_textureHandle.IsValid())
    {
        SetTexture(context.assets->LoadTexture(m_textureID, true));
    }
}

void ParticleEmitterComponent::SetRate(float perSecond)
{
    m_rate = NonNegative(perSecond);
}

void ParticleEmitterComponent::SetMaxParticles(int count)
{
    m_maxParticles = std::clamp(count, 0, kParticleLimit);
    if (static_cast<int>(m_particles.size()) > m_maxParticles)
    {
        m_particles.resize(static_cast<size_t>(m_maxParticles));
    }
}

void ParticleEmitterComponent::SetDuration(float seconds)
{
    m_duration = NonNegative(seconds);
}

void ParticleEmitterComponent::SetSeed(int seed)
{
    m_seed = static_cast<uint32_t>(seed);
    // xorshift has one fixed point: zero.
    m_random = m_seed == 0 ? 0x9E3779B9u : m_seed;
}

void ParticleEmitterComponent::SetRadius(float radius)
{
    m_radius = NonNegative(radius);
}

void ParticleEmitterComponent::SetBoxSize(const glm::vec3 &size)
{
    m_boxSize = glm::max(size, glm::vec3(0.0f));
}

void ParticleEmitterComponent::SetDirection(const glm::vec3 &direction)
{
    const float length = glm::length(direction);
    m_direction = length > 1e-6f && std::isfinite(length) ? direction / length : glm::vec3(0.0f, 1.0f, 0.0f);
}

void ParticleEmitterComponent::SetSpread(float degrees)
{
    m_spread = std::isfinite(degrees) ? glm::clamp(degrees, 0.0f, 180.0f) : 0.0f;
}

void ParticleEmitterComponent::SetSpeedMin(float speed)
{
    m_speedMin = NonNegative(speed);
}

void ParticleEmitterComponent::SetSpeedMax(float speed)
{
    m_speedMax = NonNegative(speed);
}

void ParticleEmitterComponent::SetLifetimeMin(float seconds)
{
    m_lifetimeMin = NonNegative(seconds);
}

void ParticleEmitterComponent::SetLifetimeMax(float seconds)
{
    m_lifetimeMax = NonNegative(seconds);
}

void ParticleEmitterComponent::SetDrag(float drag)
{
    m_drag = NonNegative(drag);
}

void ParticleEmitterComponent::SetBounce(float bounce)
{
    m_bounce = std::isfinite(bounce) ? glm::clamp(bounce, 0.0f, 1.0f) : 0.0f;
}

void ParticleEmitterComponent::SetStartSize(float size)
{
    m_startSize = NonNegative(size);
}

void ParticleEmitterComponent::SetEndSize(float size)
{
    m_endSize = NonNegative(size);
}

void ParticleEmitterComponent::SetWorldSpace(bool worldSpace)
{
    if (worldSpace != m_worldSpace)
    {
        // Positions would be in the wrong space.
        m_particles.clear();
    }
    m_worldSpace = worldSpace;
}

float ParticleEmitterComponent::Random01()
{
    // xorshift32: the same sequence on every platform, unlike the std distributions.
    m_random ^= m_random << 13;
    m_random ^= m_random >> 17;
    m_random ^= m_random << 5;
    return static_cast<float>(m_random >> 8) * (1.0f / 16777216.0f);
}

float ParticleEmitterComponent::RandomRange(float a, float b)
{
    return a + (b - a) * Random01();
}

void ParticleEmitterComponent::Emit(int count)
{
    if (count <= 0 || !m_owner)
    {
        return;
    }
    const glm::mat4 world = m_owner->GetWorldTransform();
    const int room = m_maxParticles - static_cast<int>(m_particles.size());
    count = std::min(count, room);
    for (int i = 0; i < count; ++i)
    {
        Spawn(world);
    }
}

void ParticleEmitterComponent::Clear()
{
    m_particles.clear();
    m_quads.clear();
}

void ParticleEmitterComponent::Restart()
{
    Clear();
    SetSeed(static_cast<int>(m_seed));
    m_clock = 0.0f;
    m_emitDebt = 0.0f;
    m_burstPending = true;
}

void ParticleEmitterComponent::Spawn(const glm::mat4 &world)
{
    Particle particle;

    glm::vec3 offset(0.0f);
    switch (m_shape)
    {
    case EmitterShape::Point:
        break;
    case EmitterShape::Sphere: {
        // Uniform in the ball: a uniform direction and radius * cbrt(u).
        const float z = RandomRange(-1.0f, 1.0f);
        const float phi = Random01() * kTwoPi;
        const float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
        offset = glm::vec3(r * std::cos(phi), r * std::sin(phi), z) * (m_radius * std::cbrt(Random01()));
        break;
    }
    case EmitterShape::Box:
        offset = (glm::vec3(Random01(), Random01(), Random01()) - 0.5f) * m_boxSize;
        break;
    }

    // A direction inside the cone, uniform over its cap.
    const float cosMax = std::cos(glm::radians(m_spread));
    const float cosTheta = 1.0f - Random01() * (1.0f - cosMax);
    const float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
    const float phi = Random01() * kTwoPi;
    glm::vec3 u;
    glm::vec3 v;
    Perpendiculars(m_direction, u, v);
    const glm::vec3 direction = m_direction * cosTheta + (u * std::cos(phi) + v * std::sin(phi)) * sinTheta;
    const glm::vec3 velocity = direction * RandomRange(m_speedMin, std::max(m_speedMin, m_speedMax));

    if (m_worldSpace)
    {
        particle.position = glm::vec3(world * glm::vec4(offset, 1.0f));
        particle.velocity = glm::mat3(world) * velocity;
    }
    else
    {
        particle.position = offset;
        particle.velocity = velocity;
    }
    particle.lifetime = std::max(1e-3f, RandomRange(m_lifetimeMin, std::max(m_lifetimeMin, m_lifetimeMax)));
    particle.rotation = (m_spinMin != 0.0f || m_spinMax != 0.0f) ? Random01() * kTwoPi : 0.0f;
    particle.spin = glm::radians(RandomRange(m_spinMin, m_spinMax));
    const int frames = m_columns * m_rows;
    particle.frame = frames > 1 && m_randomFrame
                         ? static_cast<uint16_t>(std::min(frames - 1, static_cast<int>(Random01() * frames)))
                         : 0;
    m_particles.push_back(particle);
}

void ParticleEmitterComponent::Step(float deltaTime)
{
    const float damping = 1.0f / (1.0f + m_drag * deltaTime);
    for (size_t i = 0; i < m_particles.size();)
    {
        Particle &p = m_particles[i];
        p.age += deltaTime;
        if (p.age >= p.lifetime)
        {
            // Order does not matter (alpha billboards are sorted when drawn).
            p = m_particles.back();
            m_particles.pop_back();
            continue;
        }
        p.velocity = (p.velocity + m_gravity * deltaTime) * damping;
        p.position += p.velocity * deltaTime;
        p.rotation += p.spin * deltaTime;
        if (m_collideFloor && p.position.y < m_floorY)
        {
            p.position.y = m_floorY;
            p.velocity.y = p.velocity.y < 0.0f ? -p.velocity.y * m_bounce : p.velocity.y;
        }
        ++i;
    }
}

void ParticleEmitterComponent::Simulate(float deltaTime)
{
    if (!(deltaTime > 0.0f) || !std::isfinite(deltaTime))
    {
        return;
    }
    Step(deltaTime);

    if (m_burstPending)
    {
        m_burstPending = false;
        Emit(m_burst);
    }
    if (!m_emitting)
    {
        m_emitDebt = 0.0f;
        return;
    }

    float active = deltaTime;
    m_clock += deltaTime;
    if (m_duration > 0.0f && m_clock >= m_duration)
    {
        if (m_loop)
        {
            m_clock = std::fmod(m_clock, m_duration);
            Emit(m_burst);
        }
        else
        {
            // Only the part of this step before the end emits.
            active = std::max(0.0f, deltaTime - (m_clock - m_duration));
            m_clock = m_duration;
        }
    }
    m_emitDebt += m_rate * active;
    const int count = static_cast<int>(m_emitDebt);
    m_emitDebt -= static_cast<float>(count);
    Emit(count);
}

void ParticleEmitterComponent::Update(float deltaTime)
{
    Simulate(deltaTime);
    Submit();
}

void ParticleEmitterComponent::EditUpdate()
{
    Submit();
}

void ParticleEmitterComponent::OnOriginShift(const glm::vec3 &offset)
{
    if (!m_worldSpace)
    {
        return;
    }
    for (Particle &p : m_particles)
    {
        p.position -= offset;
    }
}

void ParticleEmitterComponent::Submit()
{
    m_quads.clear();
    if (m_particles.empty() || !m_owner)
    {
        return;
    }
    Texture *texture = GetTexture();
    if (!texture && (m_textureID.IsValid() || m_textureHandle.IsValid()))
    {
        return; // still loading
    }

    const glm::vec4 startColor = SrgbToLinear(m_startColor);
    const glm::vec4 endColor = SrgbToLinear(m_endColor);
    const int frames = m_columns * m_rows;
    const glm::vec2 cell(1.0f / static_cast<float>(m_columns), 1.0f / static_cast<float>(m_rows));

    glm::vec3 low(m_particles.front().position);
    glm::vec3 high(low);
    float largest = 0.0f;
    m_quads.resize(m_particles.size());
    for (size_t i = 0; i < m_particles.size(); ++i)
    {
        const Particle &p = m_particles[i];
        const float t = glm::clamp(p.age / p.lifetime, 0.0f, 1.0f);
        const float size = m_startSize + (m_endSize - m_startSize) * t;
        SpriteQuad &quad = m_quads[i];
        quad.center = p.position;
        quad.min = glm::vec2(-0.5f * size);
        quad.max = glm::vec2(0.5f * size);
        quad.rotation = p.rotation;
        quad.color = startColor + (endColor - startColor) * t;
        int frame = p.frame;
        if (frames > 1 && !m_randomFrame)
        {
            frame = std::min(frames - 1, static_cast<int>(t * static_cast<float>(frames)));
        }
        const glm::vec2 topLeft(
            static_cast<float>(frame % m_columns) * cell.x, static_cast<float>(frame / m_columns) * cell.y
        );
        quad.uvTopLeft = topLeft;
        quad.uvBottomRight = topLeft + cell;
        low = glm::min(low, p.position);
        high = glm::max(high, p.position);
        largest = std::max(largest, size);
    }

    SpriteCommand command;
    command.texture = texture;
    command.filter = m_filter;
    command.blend = m_blend;
    command.billboard = true;
    command.quads = m_quads.data();
    command.quadCount = static_cast<uint32_t>(m_quads.size());
    command.modelMatrix = m_worldSpace ? glm::mat4(1.0f) : m_owner->GetWorldTransform();
    command.boundsCenter = (low + high) * 0.5f;
    // Half the box diagonal plus a rotated quad's half-diagonal; never 0 (never culled).
    command.boundsRadius = glm::length(high - low) * 0.5f + largest * 0.7072f + 1e-3f;
    command.layer = m_layer;
    command.lit = m_lit;
    Engine::GetInstance().GetRenderQueue().SubmitSprites(command);
}

namespace
{
using P = ParticleEmitterComponent;
const bool s_registered = RegisterComponentType<ParticleEmitterComponent>(
    "ParticleEmitterComponent",
    {
        AssetIDProperty<P>("texture", AssetType::Texture, &P::GetTextureID, &P::SetTextureID),
        IntProperty<P>("columns", &P::GetColumns, &P::SetColumns),
        IntProperty<P>("rows", &P::GetRows, &P::SetRows),
        BoolProperty<P>("randomFrame", &P::GetRandomFrame, &P::SetRandomFrame),
        BoolProperty<P>("emitting", &P::GetEmitting, &P::SetEmitting),
        FloatProperty<P>("rate", &P::GetRate, &P::SetRate),
        IntProperty<P>("burst", &P::GetBurst, &P::SetBurst),
        IntProperty<P>("maxParticles", &P::GetMaxParticles, &P::SetMaxParticles),
        FloatProperty<P>("duration", &P::GetDuration, &P::SetDuration),
        BoolProperty<P>("loop", &P::GetLoop, &P::SetLoop),
        IntProperty<P>("seed", &P::GetSeed, &P::SetSeed),
        EnumProperty<P, EmitterShape>(
            "shape", &P::GetShape, &P::SetShape,
            {{EmitterShape::Point, "point"}, {EmitterShape::Sphere, "sphere"}, {EmitterShape::Box, "box"}}
        ),
        FloatProperty<P>("radius", &P::GetRadius, &P::SetRadius),
        Vec3Property<P>("boxSize", &P::GetBoxSize, &P::SetBoxSize),
        Vec3Property<P>("direction", &P::GetDirection, &P::SetDirection),
        FloatProperty<P>("spread", &P::GetSpread, &P::SetSpread),
        FloatProperty<P>("speedMin", &P::GetSpeedMin, &P::SetSpeedMin),
        FloatProperty<P>("speedMax", &P::GetSpeedMax, &P::SetSpeedMax),
        FloatProperty<P>("lifetimeMin", &P::GetLifetimeMin, &P::SetLifetimeMin),
        FloatProperty<P>("lifetimeMax", &P::GetLifetimeMax, &P::SetLifetimeMax),
        Vec3Property<P>("gravity", &P::GetGravity, &P::SetGravity),
        FloatProperty<P>("drag", &P::GetDrag, &P::SetDrag),
        BoolProperty<P>("collideFloor", &P::GetCollideFloor, &P::SetCollideFloor),
        FloatProperty<P>("floorY", &P::GetFloorY, &P::SetFloorY),
        FloatProperty<P>("bounce", &P::GetBounce, &P::SetBounce),
        FloatProperty<P>("startSize", &P::GetStartSize, &P::SetStartSize),
        FloatProperty<P>("endSize", &P::GetEndSize, &P::SetEndSize),
        Vec4Property<P>("startColor", &P::GetStartColor, &P::SetStartColor),
        Vec4Property<P>("endColor", &P::GetEndColor, &P::SetEndColor),
        FloatProperty<P>("spinMin", &P::GetSpinMin, &P::SetSpinMin),
        FloatProperty<P>("spinMax", &P::GetSpinMax, &P::SetSpinMax),
        EnumProperty<P, SpriteBlend>(
            "blend", &P::GetBlend, &P::SetBlend, {{SpriteBlend::Alpha, "alpha"}, {SpriteBlend::Additive, "additive"}}
        ),
        EnumProperty<P, SpriteFilter>(
            "filter", &P::GetFilter, &P::SetFilter,
            {{SpriteFilter::Linear, "linear"}, {SpriteFilter::Nearest, "nearest"}}
        ),
        IntProperty<P>("layer", &P::GetLayer, &P::SetLayer),
        BoolProperty<P>("lit", &P::GetLit, &P::SetLit),
        BoolProperty<P>("worldSpace", &P::GetWorldSpace, &P::SetWorldSpace),
    }
);
} // namespace

void LinkParticleEmitterComponent()
{
}

} // namespace Geni
