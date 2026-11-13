#pragma once

#include "assets/AssetHandle.h"
#include "render/Sprite.h"
#include "scene/Component.h"

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstdint>
#include <memory>
#include <vector>

namespace Geni
{

class Texture;

enum class EmitterShape : uint8_t
{
    Point,
    Sphere,
    Box
};

// Emits, simulates and draws camera-facing particles from one object (docs/design/0019).
// The simulation runs in Update (play mode); EditUpdate draws what exists without
// simulating. All particles of one emitter are one billboard SpriteCommand.
class ParticleEmitterComponent : public Component
{
    COMPONENT(ParticleEmitterComponent);

  public:
    struct Particle
    {
        // Scene space with worldSpace, else the object's space.
        glm::vec3 position{0.0f};
        glm::vec3 velocity{0.0f};
        float age = 0.0f;
        float lifetime = 1.0f;
        // Radians and radians per second.
        float rotation = 0.0f;
        float spin = 0.0f;
        uint16_t frame = 0;
    };

    void Update(float deltaTime) override;
    void EditUpdate() override;
    void ResolveAssets(SerializationContext &context) override;
    void OnOriginShift(const glm::vec3 &offset) override;

    // Spawns `count` particles now (up to maxParticles).
    void Emit(int count);
    // Removes every particle.
    void Clear();
    // Clears, rewinds the emission clock and the random sequence, and fires the burst
    // again on the next Update.
    void Restart();
    // Advances the simulation without drawing (tests, warming up an effect).
    void Simulate(float deltaTime);

    const std::vector<Particle> &GetParticles() const
    {
        return m_particles;
    }
    int GetParticleCount() const
    {
        return static_cast<int>(m_particles.size());
    }
    // The quads last built for drawing (for tests and tools).
    const std::vector<SpriteQuad> &GetQuads() const
    {
        return m_quads;
    }

    // Texture (an atlas of columns x rows frames; nullptr: white squares).
    Texture *GetTexture() const;
    void SetTexture(const std::shared_ptr<Texture> &texture);
    void SetTexture(const AssetHandle<Texture> &texture);
    AssetID GetTextureID() const
    {
        return m_textureID;
    }
    void SetTextureID(AssetID id);
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
    // True: each particle shows one random frame; false: frames play over its life.
    bool GetRandomFrame() const
    {
        return m_randomFrame;
    }
    void SetRandomFrame(bool random)
    {
        m_randomFrame = random;
    }

    // Emission.
    bool GetEmitting() const
    {
        return m_emitting;
    }
    void SetEmitting(bool emitting)
    {
        m_emitting = emitting;
    }
    float GetRate() const
    {
        return m_rate;
    }
    void SetRate(float perSecond);
    int GetBurst() const
    {
        return m_burst;
    }
    void SetBurst(int count)
    {
        m_burst = count < 0 ? 0 : count;
    }
    int GetMaxParticles() const
    {
        return m_maxParticles;
    }
    void SetMaxParticles(int count);
    // Seconds of emission (0: forever). With loop the burst repeats every duration.
    float GetDuration() const
    {
        return m_duration;
    }
    void SetDuration(float seconds);
    bool GetLoop() const
    {
        return m_loop;
    }
    void SetLoop(bool loop)
    {
        m_loop = loop;
    }
    int GetSeed() const
    {
        return static_cast<int>(m_seed);
    }
    void SetSeed(int seed);

    // Where particles start, in the object's space.
    EmitterShape GetShape() const
    {
        return m_shape;
    }
    void SetShape(EmitterShape shape)
    {
        m_shape = shape;
    }
    float GetRadius() const
    {
        return m_radius;
    }
    void SetRadius(float radius);
    glm::vec3 GetBoxSize() const
    {
        return m_boxSize;
    }
    void SetBoxSize(const glm::vec3 &size);

    // Initial velocity: `direction` (object space) turned up to `spread` degrees, times a
    // speed in speedMin..speedMax.
    glm::vec3 GetDirection() const
    {
        return m_direction;
    }
    void SetDirection(const glm::vec3 &direction);
    float GetSpread() const
    {
        return m_spread;
    }
    void SetSpread(float degrees);
    float GetSpeedMin() const
    {
        return m_speedMin;
    }
    void SetSpeedMin(float speed);
    float GetSpeedMax() const
    {
        return m_speedMax;
    }
    void SetSpeedMax(float speed);
    float GetLifetimeMin() const
    {
        return m_lifetimeMin;
    }
    void SetLifetimeMin(float seconds);
    float GetLifetimeMax() const
    {
        return m_lifetimeMax;
    }
    void SetLifetimeMax(float seconds);

    // Motion: acceleration (world units per second squared, in the space particles live
    // in) and drag (velocity *= 1 / (1 + drag * dt)).
    glm::vec3 GetGravity() const
    {
        return m_gravity;
    }
    void SetGravity(const glm::vec3 &gravity)
    {
        m_gravity = gravity;
    }
    float GetDrag() const
    {
        return m_drag;
    }
    void SetDrag(float drag);
    // Particles below floorY are put back on it with their vertical speed reversed and
    // scaled by bounce (0: they slide).
    bool GetCollideFloor() const
    {
        return m_collideFloor;
    }
    void SetCollideFloor(bool collide)
    {
        m_collideFloor = collide;
    }
    float GetFloorY() const
    {
        return m_floorY;
    }
    void SetFloorY(float y)
    {
        m_floorY = y;
    }
    float GetBounce() const
    {
        return m_bounce;
    }
    void SetBounce(float bounce);

    // Over life: size (world units, full width) and sRGB colour + alpha, start to end.
    float GetStartSize() const
    {
        return m_startSize;
    }
    void SetStartSize(float size);
    float GetEndSize() const
    {
        return m_endSize;
    }
    void SetEndSize(float size);
    glm::vec4 GetStartColor() const
    {
        return m_startColor;
    }
    void SetStartColor(const glm::vec4 &color)
    {
        m_startColor = color;
    }
    glm::vec4 GetEndColor() const
    {
        return m_endColor;
    }
    void SetEndColor(const glm::vec4 &color)
    {
        m_endColor = color;
    }
    // Spin in degrees per second, random in spinMin..spinMax; the start angle is random.
    float GetSpinMin() const
    {
        return m_spinMin;
    }
    void SetSpinMin(float degrees)
    {
        m_spinMin = degrees;
    }
    float GetSpinMax() const
    {
        return m_spinMax;
    }
    void SetSpinMax(float degrees)
    {
        m_spinMax = degrees;
    }

    // Drawing.
    SpriteBlend GetBlend() const
    {
        return m_blend;
    }
    void SetBlend(SpriteBlend blend)
    {
        m_blend = blend;
    }
    SpriteFilter GetFilter() const
    {
        return m_filter;
    }
    void SetFilter(SpriteFilter filter)
    {
        m_filter = filter;
    }
    int GetLayer() const
    {
        return m_layer;
    }
    void SetLayer(int layer)
    {
        m_layer = layer;
    }
    // True: particles stay where they were emitted when the object moves.
    bool GetWorldSpace() const
    {
        return m_worldSpace;
    }
    void SetWorldSpace(bool worldSpace);

  private:
    float Random01();
    float RandomRange(float a, float b);
    void Spawn(const glm::mat4 &world);
    void Step(float deltaTime);
    void Submit();

    std::vector<Particle> m_particles;
    std::vector<SpriteQuad> m_quads;

    AssetHandle<Texture> m_textureHandle;
    std::shared_ptr<Texture> m_texture;
    AssetID m_textureID;
    int m_columns = 1;
    int m_rows = 1;
    bool m_randomFrame = true;

    bool m_emitting = true;
    float m_rate = 20.0f;
    int m_burst = 0;
    int m_maxParticles = 1000;
    float m_duration = 0.0f;
    bool m_loop = true;
    uint32_t m_seed = 1;

    EmitterShape m_shape = EmitterShape::Point;
    float m_radius = 0.5f;
    glm::vec3 m_boxSize{1.0f};
    glm::vec3 m_direction{0.0f, 1.0f, 0.0f};
    float m_spread = 15.0f;
    float m_speedMin = 1.0f;
    float m_speedMax = 2.0f;
    float m_lifetimeMin = 1.0f;
    float m_lifetimeMax = 2.0f;

    glm::vec3 m_gravity{0.0f};
    float m_drag = 0.0f;
    bool m_collideFloor = false;
    float m_floorY = 0.0f;
    float m_bounce = 0.3f;

    float m_startSize = 0.2f;
    float m_endSize = 0.2f;
    glm::vec4 m_startColor{1.0f};
    glm::vec4 m_endColor{1.0f, 1.0f, 1.0f, 0.0f};
    float m_spinMin = 0.0f;
    float m_spinMax = 0.0f;

    SpriteBlend m_blend = SpriteBlend::Alpha;
    SpriteFilter m_filter = SpriteFilter::Linear;
    int m_layer = 0;
    bool m_worldSpace = true;

    // Runtime state.
    uint32_t m_random = 1;
    float m_clock = 0.0f;
    float m_emitDebt = 0.0f;
    bool m_burstPending = true;
};

} // namespace Geni
