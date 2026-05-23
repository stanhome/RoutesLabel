#pragma once
//
// TriangleRenderer.h
// 高层渲染器：封装 Swapchain / RenderPass / Pipeline / CommandPool / FrameSync 编排。
// 业务侧只需调用 draw_frame() 与 on_framebuffer_resize()。
//

#include "platform/Window.h"

#include <vulkan/vulkan.h>

#include <memory>
#include <vector>

namespace routes_label::rhi {
class Instance;
class PhysicalDevice;
class Device;
class Swapchain;
class RenderPass;
class Framebuffers;
class GraphicsPipeline;
class CommandPool;
class FrameSync;
}  // namespace routes_label::rhi

namespace routes_label::renderer {

class TriangleRenderer {
public:
    TriangleRenderer(rhi::Instance& instance,
                     rhi::PhysicalDevice& physical,
                     rhi::Device& device,
                     platform::Window& window,
                     VkSurfaceKHR surface);
    ~TriangleRenderer();

    TriangleRenderer(const TriangleRenderer&) = delete;
    TriangleRenderer& operator=(const TriangleRenderer&) = delete;

    void draw_frame();
    void on_framebuffer_resize();
    void wait_idle() const;

private:
    void create_swapchain_dependent();
    void destroy_swapchain_dependent();
    void recreate_swapchain();

    void record_command_buffer(VkCommandBuffer cmd, uint32_t image_index);

    rhi::Instance&            instance_;
    rhi::PhysicalDevice&      physical_;
    rhi::Device&              device_;
    platform::Window&         window_;
    VkSurfaceKHR              surface_;

    std::unique_ptr<rhi::Swapchain>        swapchain_;
    std::unique_ptr<rhi::RenderPass>       render_pass_;
    std::unique_ptr<rhi::Framebuffers>     framebuffers_;
    std::unique_ptr<rhi::GraphicsPipeline> pipeline_;
    std::unique_ptr<rhi::CommandPool>      command_pool_;
    std::unique_ptr<rhi::FrameSync>        frame_sync_;
    std::vector<VkCommandBuffer>           command_buffers_;
    std::vector<VkSemaphore>               render_finished_;  // per swapchain image
};

}  // namespace routes_label::renderer
