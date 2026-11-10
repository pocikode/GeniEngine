#include "rhi/Device.h"
#include "rhi/Handle.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <doctest/doctest.h>

#include "rhi_triangle_frag.spv.h"
#include "rhi_triangle_vert.spv.h"

#include <GLFW/glfw3.h>

namespace
{
struct FakeTag;
using FakeHandle = Geni::rhi::Handle<FakeTag>;
} // namespace

TEST_SUITE("rhi handles")
{
    TEST_CASE("a default handle is invalid")
    {
        FakeHandle handle;
        CHECK_FALSE(handle.IsValid());
    }

    TEST_CASE("a pool hands out valid handles and finds their objects")
    {
        Geni::rhi::HandlePool<int, FakeTag> pool;
        auto a = pool.Add(10);
        auto b = pool.Add(20);
        CHECK(a.IsValid());
        CHECK(b.IsValid());
        CHECK(a != b);
        CHECK(*pool.Get(a) == 10);
        CHECK(*pool.Get(b) == 20);
        CHECK(pool.LiveCount() == 2);
    }

    TEST_CASE("a removed handle goes stale and its slot is reused with a new generation")
    {
        Geni::rhi::HandlePool<int, FakeTag> pool;
        auto first = pool.Add(1);
        int removed = 0;
        CHECK(pool.Remove(first, removed));
        CHECK(removed == 1);
        CHECK_FALSE(pool.Contains(first));
        CHECK_FALSE(pool.Remove(first, removed));

        auto second = pool.Add(2);
        CHECK(second.index == first.index);
        CHECK(second.generation != first.generation);
        CHECK_FALSE(pool.Contains(first));
        CHECK(pool.Contains(second));
        CHECK(pool.LiveCount() == 1);
    }
}

TEST_SUITE("gpu rhi")
{
    // Exercises the RHI without the engine: device, buffers, a bind group with a dynamic
    // offset, a pipeline, a render pass with explicit load ops and backbuffer read-back.
    TEST_CASE("a triangle drawn through the RHI reads back with the expected colors")
    {
        using namespace Geni::rhi;

        PrepareWindowSystem();
        REQUIRE(glfwInit());
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_FALSE);
        glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, GLFW_FALSE);
        GLFWwindow *window = glfwCreateWindow(64, 64, "rhi test", nullptr, nullptr);
        glfwDefaultWindowHints();
        REQUIRE(window != nullptr);

        const uint64_t errorsBefore = GetValidationErrorCount();
        {
            DeviceConfig config;
            config.vsync = false;
            config.validation = true;
            auto device = CreateDevice(window, config);
            REQUIRE(device != nullptr);

            // Upper-left half of the screen: a triangle with clip-space Y pointing up.
            const float vertices[] = {
                -1.0f, 1.0f, 1.0f, 1.0f, 1.0f, //
                1.0f, 1.0f, 1.0f, 1.0f, 1.0f,  //
                -1.0f, -1.0f, 1.0f, 1.0f, 1.0f,
            };
            BufferHandle vertexBuffer =
                device->CreateBuffer({sizeof(vertices), BufferUsage::Vertex, MemoryUsage::GpuOnly, "triangle"}, vertices);
            REQUIRE(vertexBuffer.IsValid());

            // Two tints in one buffer; the dynamic offset picks the second (red).
            const uint32_t alignment = device->GetUniformBufferAlignment() < 16 ? 16 : device->GetUniformBufferAlignment();
            BufferHandle tints = device->CreateBuffer(
                {alignment * 2, BufferUsage::Uniform, MemoryUsage::CpuToGpu, "tints"}, nullptr);
            auto *mapped = static_cast<uint8_t *>(device->MapBuffer(tints));
            REQUIRE(mapped != nullptr);
            const float green[4] = {0.0f, 1.0f, 0.0f, 1.0f};
            const float red[4] = {1.0f, 0.0f, 0.0f, 1.0f};
            std::memcpy(mapped, green, sizeof(green));
            std::memcpy(mapped + alignment, red, sizeof(red));

            BindGroupLayoutDesc layoutDesc;
            layoutDesc.entries.push_back({0, BindingType::DynamicUniformBuffer, ShaderStage::Vertex});
            BindGroupLayoutHandle layout = device->CreateBindGroupLayout(layoutDesc);
            BindGroupDesc groupDesc;
            groupDesc.layout = layout;
            groupDesc.entries.push_back({0, tints, 0, 16});
            BindGroupHandle group = device->CreateBindGroup(groupDesc);
            REQUIRE(group.IsValid());

            ShaderModuleHandle vs = device->CreateShaderModule(
                {ShaderStage::Vertex, geni_spirv_rhi_triangle_vert, sizeof(geni_spirv_rhi_triangle_vert), "vs"});
            ShaderModuleHandle fs = device->CreateShaderModule(
                {ShaderStage::Fragment, geni_spirv_rhi_triangle_frag, sizeof(geni_spirv_rhi_triangle_frag), "fs"});

            PipelineDesc pipelineDesc;
            pipelineDesc.vertexShader = vs;
            pipelineDesc.fragmentShader = fs;
            pipelineDesc.vertexLayout.stride = 5 * sizeof(float);
            pipelineDesc.vertexLayout.attributes = {{0, VertexFormat::Float2, 0},
                                                    {1, VertexFormat::Float3, 2 * sizeof(float)}};
            pipelineDesc.bindGroupLayouts = {layout};
            PipelineHandle pipeline = device->CreatePipeline(pipelineDesc);
            REQUIRE(pipeline.IsValid());
            // Modules are only needed while creating pipelines.
            device->DestroyShaderModule(vs);
            device->DestroyShaderModule(fs);

            BackbufferCapture capture;
            // Several frames, so frame slots and deferred destruction are exercised too.
            for (int frame = 0; frame < 4; ++frame)
            {
                CommandList *cmd = device->BeginFrame();
                REQUIRE(cmd != nullptr);
                RenderPassDesc pass;
                pass.clearColor[0] = 0.0f;
                pass.clearColor[1] = 0.0f;
                pass.clearColor[2] = 1.0f;
                cmd->BeginRenderPass(pass);
                cmd->SetPipeline(pipeline);
                const uint32_t offset = alignment;
                cmd->SetBindGroup(0, group, &offset, 1);
                cmd->SetVertexBuffer(vertexBuffer);
                cmd->Draw(3);
                cmd->EndRenderPass();
                device->EndFrame(frame == 3 ? &capture : nullptr);
            }

            REQUIRE(capture.width == 64);
            REQUIRE(capture.height == 64);
            auto pixel = [&](int x, int y) {
                const uint8_t *p = &capture.rgb[(size_t(y) * capture.width + x) * 3];
                return std::array<int, 3>{p[0], p[1], p[2]};
            };
            // Top-left corner is inside the triangle, bottom-right shows the clear color.
            CHECK(pixel(4, 4) == std::array<int, 3>{255, 0, 0});
            CHECK(pixel(59, 59) == std::array<int, 3>{0, 0, 255});

            device->DestroyPipeline(pipeline);
            device->DestroyBindGroup(group);
            device->DestroyBindGroupLayout(layout);
            device->DestroyBuffer(tints);
            device->DestroyBuffer(vertexBuffer);
        }
        CHECK(GetValidationErrorCount() == errorsBefore);

        glfwDestroyWindow(window);
    }
}
