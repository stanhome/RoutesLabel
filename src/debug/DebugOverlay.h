#pragma once
//
// debug/DebugOverlay.h
// Dear ImGui 后端容器：负责 imgui context + GLFW/Vulkan 后端 + 独立 descriptor pool
// 的生命周期，并提供"每帧三段式"接口给业务渲染器调用。
//
// 集成方式：
//   1) 构造时复用 RoutesRenderer 已创建的 VkRenderPass（同一个 subpass，叠加在业务几何之上）。
//   2) draw_frame() 主体里：
//        overlay.begin_frame();        // ImGui_ImplVulkan_NewFrame + ImGui_ImplGlfw_NewFrame + ImGui::NewFrame
//        // ... widget.tick() / widget.render() ...
//        overlay.end_frame();          // ImGui::Render
//   3) record_command_buffer 主 render pass 内、vkCmdEndRenderPass 之前：
//        overlay.record_draw_data(cmd);// ImGui_ImplVulkan_RenderDrawData
//
// 析构顺序：vkDeviceWaitIdle → ImGui_ImplVulkan_Shutdown → ImGui_ImplGlfw_Shutdown
//          → ImGui::DestroyContext → vkDestroyDescriptorPool。
//
// 不持有任何 widget；widgets 由调用方（RoutesRenderer）持有，便于按需开关。
//

#include <vulkan/vulkan.h>

namespace routes_label::rhi      { class Instance; class PhysicalDevice; class Device; }
namespace routes_label::platform { class Window; }

namespace routes_label::debug {

class DebugOverlay {
public:
    // image_count: 与 swapchain 一致；imgui_impl_vulkan 用它管理 per-frame 顶点 ring buffer。
    DebugOverlay(rhi::Instance&        instance,
                 rhi::PhysicalDevice&  physical,
                 rhi::Device&          device,
                 platform::Window&     window,
                 VkRenderPass          render_pass,
                 uint32_t              image_count);

    ~DebugOverlay();

    DebugOverlay(const DebugOverlay&)            = delete;
    DebugOverlay& operator=(const DebugOverlay&) = delete;

    // 帧开始：必须在任何 widget.render() 之前调用
    void begin_frame();

    // 帧结束（CPU 端）：在所有 widget.render() 完成后、record_draw_data 之前调用
    void end_frame();

    // 在主 render pass 已经 Begin 的命令缓冲内调用，向 cmd 录入 ImGui 的 draw 命令
    void record_draw_data(VkCommandBuffer cmd);

private:
    rhi::Device&     device_;
    VkDescriptorPool imgui_pool_ = VK_NULL_HANDLE;
};

}  // namespace routes_label::debug
