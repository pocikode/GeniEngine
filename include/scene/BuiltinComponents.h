#pragma once

// The engine is a static library. A component's reflection registration lives in its
// own translation unit, and the linker drops that object unless something references it.
// RegisterBuiltinComponents() names every built-in component so the whole set is linked
// into any program that embeds the engine. Engine::Init calls it before Application::Init.

namespace Geni
{

void LinkMeshComponent();
void LinkSkinnedMeshComponent();
void LinkCameraComponent();
void LinkLightComponent();
void LinkAnimationComponent();
void LinkPhysicsComponent();
void LinkOrbitCameraComponent();
void LinkPlayerControllerComponent();
void LinkFlyCameraComponent();

inline void RegisterBuiltinComponents()
{
    LinkMeshComponent();
    LinkSkinnedMeshComponent();
    LinkCameraComponent();
    LinkLightComponent();
    LinkAnimationComponent();
    LinkPhysicsComponent();
    LinkOrbitCameraComponent();
    LinkPlayerControllerComponent();
    LinkFlyCameraComponent();
}

} // namespace Geni
