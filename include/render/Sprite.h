#pragma once

#include <cstdint>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace Geni
{

class Texture;

// How a sprite's texture is sampled (docs/design/0017). Nearest keeps pixel art sharp.
enum class SpriteFilter : uint8_t
{
    Linear,
    Nearest
};

// How a sprite's colour combines with what is behind it.
enum class SpriteBlend : uint8_t
{
    // Straight alpha: src * a + dst * (1 - a).
    Alpha,
    // src * a + dst: light that only brightens (fire, sparks, glows); needs no sorting.
    Additive
};

// One textured rectangle in its object's XY plane.
struct SpriteQuad
{
    // Corners in the object's space (min is bottom-left).
    glm::vec2 min{-0.5f};
    glm::vec2 max{0.5f};
    // Texture coordinates of the top-left and bottom-right corners (V points down the
    // image). Swap them to flip.
    glm::vec2 uvTopLeft{0.0f, 0.0f};
    glm::vec2 uvBottomRight{1.0f, 1.0f};
    // Linear colour and alpha the texel is multiplied by.
    glm::vec4 color{1.0f};
    // Billboard commands only (docs/design/0019): the quad's centre in the command's model
    // space, and its rotation in radians about the view direction. min/max are then
    // offsets along the camera's right and up axes, in world units.
    glm::vec3 center{0.0f};
    float rotation = 0.0f;
};

// Quads that share a texture, a transform and a sort layer, submitted with
// RenderQueue::SubmitSprites. `quads` must stay valid until the frame is drawn.
struct SpriteCommand
{
    // nullptr: white.
    const Texture *texture = nullptr;
    SpriteFilter filter = SpriteFilter::Linear;
    const SpriteQuad *quads = nullptr;
    uint32_t quadCount = 0;
    glm::mat4 modelMatrix{1.0f};
    // Bounding sphere in the object's space; radius 0 is never culled.
    glm::vec3 boundsCenter{0.0f};
    float boundsRadius = 0.0f;
    // Lower layers draw first (behind). Within a layer, farther sprites draw first, then
    // in submission order.
    int32_t layer = 0;
    SpriteBlend blend = SpriteBlend::Alpha;
    // Each quad faces the camera at its `center` instead of lying in the object's XY plane
    // (particles). Alpha-blended billboard quads are sorted back to front.
    bool billboard = false;
    // Lit by the sun, ambient and clustered lights as a flat surface (docs/design/0021);
    // unlit sprites show their colours exactly.
    bool lit = false;
};

// sRGB (as painted) to linear, per channel; alpha unchanged.
glm::vec4 SrgbToLinear(const glm::vec4 &srgb);

} // namespace Geni
