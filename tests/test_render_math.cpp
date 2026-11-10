#include "render/Cascades.h"
#include "render/ClusterGrid.h"
#include "render/Frustum.h"
#include "render/ShaderInterface.h"

#include <cmath>
#include <cstddef>
#include <doctest/doctest.h>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>

using namespace Geni;

TEST_SUITE("frustum")
{
    TEST_CASE("a unit box around the origin is inside a default camera frustum")
    {
        const glm::mat4 viewProjection = glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 100.0f);
        const Frustum frustum = Frustum::FromViewProjection(viewProjection);
        CHECK(frustum.IntersectsSphere(glm::vec3(0.0f, 0.0f, -5.0f), 0.5f));
        CHECK(frustum.IntersectsAABB(glm::vec3(-0.5f, -0.5f, -5.5f), glm::vec3(0.5f, 0.5f, -4.5f)));
    }

    TEST_CASE("spheres behind the camera or beyond the far plane are outside")
    {
        const glm::mat4 viewProjection = glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);
        const Frustum frustum = Frustum::FromViewProjection(viewProjection);
        CHECK_FALSE(frustum.IntersectsSphere(glm::vec3(0.0f, 0.0f, 5.0f), 1.0f));
        CHECK_FALSE(frustum.IntersectsSphere(glm::vec3(0.0f, 0.0f, -200.0f), 1.0f));
        CHECK_FALSE(frustum.IntersectsAABB(glm::vec3(100.0f, 100.0f, -5.0f), glm::vec3(101.0f, 101.0f, -4.0f)));
    }

    TEST_CASE("a sphere straddling a plane still intersects")
    {
        const glm::mat4 viewProjection = glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);
        const Frustum frustum = Frustum::FromViewProjection(viewProjection);
        // Just outside the left plane, but close enough that the radius reaches in. At 5 m
        // with a 60 degree vertical FOV and aspect 1, the half-width is 2.89 m.
        CHECK(frustum.IntersectsSphere(glm::vec3(-4.0f, 0.0f, -5.0f), 2.0f));
    }
}

TEST_SUITE("shadow cascades")
{
    TEST_CASE("splits increase and stop at the shadow distance")
    {
        const glm::mat4 view(1.0f);
        const glm::mat4 projection = glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 1000.0f);
        const CascadeSetup setup =
            ComputeCascades(view, projection, 0.1f, 1000.0f, glm::vec3(0.0f, -1.0f, 0.0f), 60.0f, 4, 2048);

        REQUIRE(setup.count == 4);
        CHECK(setup.cascades[0].splitNear == doctest::Approx(0.1f));
        CHECK(setup.cascades[0].splitFar > setup.cascades[0].splitNear);
        CHECK(setup.cascades[1].splitFar > setup.cascades[0].splitFar);
        CHECK(setup.cascades[2].splitFar > setup.cascades[1].splitFar);
        CHECK(setup.cascades[3].splitFar > setup.cascades[2].splitFar);
        CHECK(setup.cascades[3].splitFar <= 60.0f + 1e-3f);
        CHECK(setup.splits[3] == doctest::Approx(setup.cascades[3].splitFar));
    }

    TEST_CASE("the cascade count is clamped to four and never zero")
    {
        const glm::mat4 projection = glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);
        CHECK(
            ComputeCascades(glm::mat4(1.0f), projection, 0.1f, 100.0f, glm::vec3(0, -1, 0), 50.0f, 0, 1024).count == 1
        );
        CHECK(
            ComputeCascades(glm::mat4(1.0f), projection, 0.1f, 100.0f, glm::vec3(0, -1, 0), 50.0f, 9, 1024).count == 4
        );
    }

    TEST_CASE("a point on each split is inside that cascade's light frustum")
    {
        // Camera at the origin looking down -Z.
        const glm::mat4 view = glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::mat4 projection = glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);
        const CascadeSetup setup =
            ComputeCascades(view, projection, 0.1f, 100.0f, glm::vec3(-0.3f, -1.0f, -0.2f), 50.0f, 4, 1024);

        for (uint32_t i = 0; i < setup.count; ++i)
        {
            // The point in the middle of this slice, on the camera axis.
            const float depth = (setup.cascades[i].splitNear + setup.cascades[i].splitFar) * 0.5f;
            const glm::vec4 clip = setup.cascades[i].viewProjection * glm::vec4(0.0f, 0.0f, -depth, 1.0f);
            const glm::vec3 ndc = glm::vec3(clip) / clip.w;
            CHECK(ndc.x >= -1.001f);
            CHECK(ndc.x <= 1.001f);
            CHECK(ndc.y >= -1.001f);
            CHECK(ndc.y <= 1.001f);
            CHECK(ndc.z >= -1.001f);
            CHECK(ndc.z <= 1.001f);
        }
    }

    TEST_CASE("moving the camera shifts each cascade by whole shadow texels")
    {
        const glm::mat4 projection = glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 500.0f);
        const glm::vec3 sun(-0.4f, -1.0f, -0.3f);
        const uint32_t size = 1024;
        const glm::vec3 eye(3.0f, 2.0f, 5.0f);
        auto setupAt = [&](const glm::vec3 &position) {
            const glm::mat4 view = glm::lookAt(position, position + glm::vec3(0.2f, -0.1f, -1.0f), glm::vec3(0, 1, 0));
            return ComputeCascades(view, projection, 0.1f, 500.0f, sun, 80.0f, 4, size);
        };
        const CascadeSetup before = setupAt(eye);
        const CascadeSetup after = setupAt(eye + glm::vec3(0.0137f, 0.0f, -0.0291f));
        for (uint32_t i = 0; i < before.count; ++i)
        {
            // A fixed world point moves by an integer number of texels (2/size in NDC).
            const glm::vec4 a = before.cascades[i].viewProjection * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
            const glm::vec4 b = after.cascades[i].viewProjection * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
            const float dx = (b.x - a.x) * static_cast<float>(size) * 0.5f;
            const float dy = (b.y - a.y) * static_cast<float>(size) * 0.5f;
            CHECK(std::abs(dx - std::round(dx)) < 0.01f);
            CHECK(std::abs(dy - std::round(dy)) < 0.01f);
        }
    }

    TEST_CASE("the caster distance extends each cascade towards the sun")
    {
        const glm::mat4 view = glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::mat4 projection = glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);
        const glm::vec3 sun(0.0f, -1.0f, 0.0f);
        const CascadeSetup without = ComputeCascades(view, projection, 0.1f, 100.0f, sun, 50.0f, 4, 1024, 0.0f);
        const CascadeSetup with = ComputeCascades(view, projection, 0.1f, 100.0f, sun, 50.0f, 4, 1024, 200.0f);
        // A roof 60 m above the middle of the first slice.
        const float depth = (with.cascades[0].splitNear + with.cascades[0].splitFar) * 0.5f;
        const glm::vec4 roof(0.0f, 60.0f, -depth, 1.0f);
        const glm::vec4 clipWithout = without.cascades[0].viewProjection * roof;
        const glm::vec4 clipWith = with.cascades[0].viewProjection * roof;
        CHECK(clipWithout.z / clipWithout.w < -1.0f);
        CHECK(clipWith.z / clipWith.w >= -1.0f);
        CHECK(clipWith.z / clipWith.w <= 1.0f);
    }
}

TEST_SUITE("clustered lighting")
{
    TEST_CASE("a light without a range gets one from its brightness")
    {
        LightData light;
        light.range = 7.5f;
        CHECK(EffectiveLightRange(light) == doctest::Approx(7.5f));
        light.range = 0.0f;
        light.color = glm::vec3(1.0f, 4.0f, 0.5f);
        // 4 / d^2 = 0.01 at d = 20.
        CHECK(EffectiveLightRange(light) == doctest::Approx(20.0f));
        light.range = -1.0f;
        light.color = glm::vec3(0.0f);
        CHECK(EffectiveLightRange(light) > 0.0f);
    }

    TEST_CASE("no clustered lights leaves every cluster empty")
    {
        std::vector<LightData> lights;
        LightData sun;
        sun.type = LightType::Directional;
        lights.push_back(sun);

        ClusterSettings settings;
        settings.countX = 4;
        settings.countY = 3;
        settings.countZ = 6;
        const glm::mat4 projection = glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);
        const ClusterAssignment assignment = AssignLightsToClusters(glm::mat4(1.0f), projection, settings, lights);

        REQUIRE(assignment.ranges.size() == 4 * 3 * 6);
        CHECK(assignment.lightIndices.empty());
        for (const auto &range : assignment.ranges)
        {
            CHECK(range.count == 0);
        }
    }

    TEST_CASE("a point light lands in the clusters along the camera axis")
    {
        std::vector<LightData> lights;
        LightData light;
        light.type = LightType::Point;
        light.position = glm::vec3(0.0f, 0.0f, -5.0f);
        light.range = 2.0f;
        lights.push_back(light);

        ClusterSettings settings;
        settings.countX = 4;
        settings.countY = 3;
        settings.countZ = 6;
        settings.nearPlane = 0.1f;
        settings.farPlane = 100.0f;
        const glm::mat4 projection = glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);
        const ClusterAssignment assignment = AssignLightsToClusters(glm::mat4(1.0f), projection, settings, lights);

        // Center tile (2, 1), depth slice where log(5 / 0.1) / log(1000) * 6 = 3.39 -> 3.
        const uint32_t cluster = (3 * 3 + 1) * 4 + 2;
        CHECK(assignment.ranges[cluster].count >= 1);
        CHECK(assignment.ranges[cluster].offset + assignment.ranges[cluster].count <= assignment.lightIndices.size());

        // A cluster at the far plane, outside the light's range, must not see it.
        const uint32_t distant = (5 * 3 + 1) * 4 + 2;
        CHECK(assignment.ranges[distant].count == 0);

        // Every listed index refers to the one clustered light.
        for (uint32_t index : assignment.lightIndices)
        {
            CHECK(index == 0);
        }
    }

    TEST_CASE("the per-cluster cap reports overflow instead of writing out of range")
    {
        std::vector<LightData> lights;
        for (int i = 0; i < 4; ++i)
        {
            LightData light;
            light.type = LightType::Point;
            light.position = glm::vec3(0.0f, 0.0f, -5.0f);
            light.range = 200.0f;
            lights.push_back(light);
        }

        ClusterSettings settings;
        settings.countX = 2;
        settings.countY = 2;
        settings.countZ = 2;
        settings.maxLightsPerCluster = 2;
        const glm::mat4 projection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 100.0f);
        const ClusterAssignment assignment = AssignLightsToClusters(glm::mat4(1.0f), projection, settings, lights);

        CHECK(assignment.overflowLights > 0);
        for (const auto &range : assignment.ranges)
        {
            CHECK(range.count <= 2);
            CHECK(range.offset + range.count <= assignment.lightIndices.size());
        }
    }

    TEST_CASE("the shader structures keep their std140/std430 sizes")
    {
        CHECK(sizeof(ShaderInterface::FrameUniforms) == 688);
        CHECK(sizeof(ShaderInterface::GpuLight) == 64);
        CHECK(sizeof(ShaderInterface::ClusterRange) == 8);

        // The members after the arrays must sit where the GLSL block puts them.
        using Frame = ShaderInterface::FrameUniforms;
        CHECK(offsetof(Frame, cameraPosition) == 256);
        CHECK(offsetof(Frame, shadowMatrices) == 352);
        CHECK(offsetof(Frame, cascadeSplits) == 608);
        CHECK(offsetof(Frame, shadowParams) == 624);
        CHECK(offsetof(Frame, clusterGrid) == 640);
        CHECK(offsetof(Frame, clusterDepth) == 656);
        CHECK(offsetof(Frame, screenParams) == 672);
    }
}