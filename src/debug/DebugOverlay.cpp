//
// debug/DebugOverlay.cpp
// Dear ImGui 后端生命周期实现。
//

#include "debug/DebugOverlay.h"

#include "platform/Window.h"
#include "rhi/Device.h"
#include "rhi/Instance.h"
#include "rhi/PhysicalDevice.h"
#include "rhi/VulkanCommon.h"
#include "utils/Log.h"

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>

#include <stdexcept>

namespace routes_label::debug {

namespace {

// imgui_impl_vulkan 推荐的"够用"descriptor pool 容量。所有 type 各给 1000 个，
// 在 free 时按 set 释放（FREE_DESCRIPTOR_SET_BIT 必须开）。
// 来源：official imgui example_glfw_vulkan/main.cpp。
VkDescriptorPool create_imgui_descriptor_pool(VkDevice device) {
    constexpr VkDescriptorPoolSize kSizes[] = {
        { VK_DESCRIPTOR_TYPE_SAMPLER,                1000 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,          1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,   1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,   1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,       1000 },
    };
    VkDescriptorPoolCreateInfo ci = {};
    ci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    ci.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    ci.maxSets       = 1000 * static_cast<uint32_t>(sizeof(kSizes) / sizeof(kSizes[0]));
    ci.poolSizeCount = static_cast<uint32_t>(sizeof(kSizes) / sizeof(kSizes[0]));
    ci.pPoolSizes    = kSizes;

    VkDescriptorPool pool = VK_NULL_HANDLE;
    VK_CHECK(vkCreateDescriptorPool(device, &ci, nullptr, &pool));
    return pool;
}

void check_vk_result(VkResult err) {
    if (err == VK_SUCCESS) return;
    LOG_INFO("[DebugOverlay] imgui_impl_vulkan VkResult=" << err);
    if (err < 0) {
        // imgui 的官方 example 是 abort，这里把它降级为日志（避免一时 surface lost 直接挂）。
        // 重大错误会在外部 vkQueueSubmit 等调用处再次以 VK_CHECK 抛出。
    }
}

}  // namespace

DebugOverlay::DebugOverlay(rhi::Instance&        instance,
                           rhi::PhysicalDevice&  physical,
                           rhi::Device&          device,
                           platform::Window&     window,
                           VkRenderPass          render_pass,
                           uint32_t              image_count)
    : device_(device) {
    // 1) imgui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    // 启用 v1.92+ 后端动态字体 atlas 路径（NewFrame 内部按需上传纹理，无需手动 CreateFontsTexture）。
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    ImGui::StyleColorsDark();

    // 2) GLFW 后端（install_callbacks=true：让 imgui 链式接管输入回调）
    if (!ImGui_ImplGlfw_InitForVulkan(window.handle(), /*install_callbacks=*/true)) {
        ImGui::DestroyContext();
        throw std::runtime_error("[DebugOverlay] ImGui_ImplGlfw_InitForVulkan failed");
    }

    // 3) descriptor pool（独立于业务 pool）
    imgui_pool_ = create_imgui_descriptor_pool(device_.handle());

    // 4) Vulkan 后端
    ImGui_ImplVulkan_InitInfo init = {};
    init.ApiVersion        = VK_API_VERSION_1_2;
    init.Instance          = instance.handle();
    init.PhysicalDevice    = physical.handle();
    init.Device            = device_.handle();
    init.QueueFamily       = device_.graphics_family();
    init.Queue             = device_.graphics_queue();
    init.DescriptorPool    = imgui_pool_;
    init.DescriptorPoolSize = 0;
    init.MinImageCount     = image_count;
    init.ImageCount        = image_count;
    init.PipelineCache     = VK_NULL_HANDLE;
    init.UseDynamicRendering = false;
    init.Allocator         = nullptr;
    init.CheckVkResultFn   = check_vk_result;
    init.MinAllocationSize = 1024 * 1024;
    init.PipelineInfoMain.RenderPass  = render_pass;
    init.PipelineInfoMain.Subpass     = 0;
    init.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    if (!ImGui_ImplVulkan_Init(&init)) {
        ImGui_ImplGlfw_Shutdown();
        vkDestroyDescriptorPool(device_.handle(), imgui_pool_, nullptr);
        imgui_pool_ = VK_NULL_HANDLE;
        ImGui::DestroyContext();
        throw std::runtime_error("[DebugOverlay] ImGui_ImplVulkan_Init failed");
    }

    LOG_INFO("[DebugOverlay] imgui ready (image_count=" << image_count
             << ", render_pass=" << render_pass << ")");
}

DebugOverlay::~DebugOverlay() {
    // 等待 GPU 把所有引用 imgui 资源的命令都跑完
    device_.wait_idle();

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    if (imgui_pool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_.handle(), imgui_pool_, nullptr);
        imgui_pool_ = VK_NULL_HANDLE;
    }
    LOG_INFO("[DebugOverlay] shutdown");
}

void DebugOverlay::begin_frame() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void DebugOverlay::end_frame() {
    ImGui::Render();
}

void DebugOverlay::record_draw_data(VkCommandBuffer cmd) {
    ImDrawData* draw_data = ImGui::GetDrawData();
    if (draw_data == nullptr) return;
    if (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f) return;
    ImGui_ImplVulkan_RenderDrawData(draw_data, cmd);
}

}  // namespace routes_label::debug
