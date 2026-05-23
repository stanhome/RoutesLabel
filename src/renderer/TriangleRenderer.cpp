#include "renderer/TriangleRenderer.h"

#include "rhi/CommandPool.h"
#include "rhi/Device.h"
#include "rhi/Framebuffer.h"
#include "rhi/FrameSync.h"
#include "rhi/GraphicsPipeline.h"
#include "rhi/Instance.h"
#include "rhi/PhysicalDevice.h"
#include "rhi/RenderPass.h"
#include "rhi/Swapchain.h"
#include "utils/FileSystem.h"
#include "utils/Log.h"

#include <stdexcept>

namespace routes_label::renderer {

namespace {

std::filesystem::path shader_path(const char* filename) {
    return utils::executable_dir() / "shaders" / filename;
}

}  // namespace

TriangleRenderer::TriangleRenderer(rhi::Instance& instance,
                                   rhi::PhysicalDevice& physical,
                                   rhi::Device& device,
                                   platform::Window& window,
                                   VkSurfaceKHR surface)
    : instance_(instance),
      physical_(physical),
      device_(device),
      window_(window),
      surface_(surface) {
    (void)instance_;  // 保留引用以便后续扩展（compute pipeline / 资源 staging 等）
    command_pool_ = std::make_unique<rhi::CommandPool>(device_, device_.graphics_family());
    frame_sync_   = std::make_unique<rhi::FrameSync>(device_);
    command_buffers_ = command_pool_->allocate_primary(rhi::kMaxFramesInFlight);

    create_swapchain_dependent();

    pipeline_ = std::make_unique<rhi::GraphicsPipeline>(
        device_,
        *render_pass_,
        shader_path("triangle.vert.spv"),
        shader_path("triangle.frag.spv"));
}

TriangleRenderer::~TriangleRenderer() {
    device_.wait_idle();
    pipeline_.reset();
    destroy_swapchain_dependent();
    frame_sync_.reset();
    command_pool_.reset();
}

void TriangleRenderer::wait_idle() const {
    device_.wait_idle();
}

void TriangleRenderer::on_framebuffer_resize() {
    // 仅置位由 draw_frame 在合适时机处理（也由 swapchain VK_ERROR_OUT_OF_DATE_KHR 触发）
}

void TriangleRenderer::create_swapchain_dependent() {
    uint32_t w = 0, h = 0;
    window_.framebuffer_size(w, h);
    swapchain_   = std::make_unique<rhi::Swapchain>(physical_, device_, surface_, w, h);
    render_pass_ = std::make_unique<rhi::RenderPass>(device_, swapchain_->image_format());
    framebuffers_ = std::make_unique<rhi::Framebuffers>(
        device_, *render_pass_, swapchain_->image_views(), swapchain_->extent());

    // 每张 swapchain image 一个 renderFinished semaphore，避免与 present 队列复用冲突
    VkSemaphoreCreateInfo sci = {};
    sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    render_finished_.resize(swapchain_->image_count(), VK_NULL_HANDLE);
    for (auto& s : render_finished_) {
        VK_CHECK(vkCreateSemaphore(device_.handle(), &sci, nullptr, &s));
    }
}

void TriangleRenderer::destroy_swapchain_dependent() {
    for (auto s : render_finished_) {
        if (s != VK_NULL_HANDLE) vkDestroySemaphore(device_.handle(), s, nullptr);
    }
    render_finished_.clear();

    framebuffers_.reset();
    render_pass_.reset();
    swapchain_.reset();
}

void TriangleRenderer::recreate_swapchain() {
    window_.wait_if_minimized();
    if (window_.should_close()) return;

    device_.wait_idle();

    // 销毁旧的 per-image semaphore 与 framebuffer，重建 swapchain 后再分配新的
    for (auto s : render_finished_) {
        if (s != VK_NULL_HANDLE) vkDestroySemaphore(device_.handle(), s, nullptr);
    }
    render_finished_.clear();
    framebuffers_.reset();

    uint32_t w = 0, h = 0;
    window_.framebuffer_size(w, h);
    swapchain_->recreate(w, h);

    if (render_pass_ == nullptr) {
        render_pass_ = std::make_unique<rhi::RenderPass>(device_, swapchain_->image_format());
    }
    framebuffers_ = std::make_unique<rhi::Framebuffers>(
        device_, *render_pass_, swapchain_->image_views(), swapchain_->extent());

    VkSemaphoreCreateInfo sci = {};
    sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    render_finished_.resize(swapchain_->image_count(), VK_NULL_HANDLE);
    for (auto& s : render_finished_) {
        VK_CHECK(vkCreateSemaphore(device_.handle(), &sci, nullptr, &s));
    }

    LOG_INFO("[Renderer] swapchain recreated " << w << "x" << h);
}

void TriangleRenderer::record_command_buffer(VkCommandBuffer cmd, uint32_t image_index) {
    VkCommandBufferBeginInfo bi = {};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    VK_CHECK(vkBeginCommandBuffer(cmd, &bi));

    VkClearValue clear = {};
    clear.color = {{ 0.05f, 0.05f, 0.08f, 1.0f }};

    VkRenderPassBeginInfo rpbi = {};
    rpbi.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpbi.renderPass        = render_pass_->handle();
    rpbi.framebuffer       = framebuffers_->handles()[image_index];
    rpbi.renderArea.offset = { 0, 0 };
    rpbi.renderArea.extent = swapchain_->extent();
    rpbi.clearValueCount   = 1;
    rpbi.pClearValues      = &clear;

    vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);

    VkExtent2D ext = swapchain_->extent();

    VkViewport vp = {};
    vp.x = 0.0f;
    vp.y = 0.0f;
    vp.width    = static_cast<float>(ext.width);
    vp.height   = static_cast<float>(ext.height);
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &vp);

    VkRect2D scissor = {};
    scissor.offset = { 0, 0 };
    scissor.extent = ext;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_->handle());
    vkCmdDraw(cmd, 3, 1, 0, 0);

    vkCmdEndRenderPass(cmd);
    VK_CHECK(vkEndCommandBuffer(cmd));
}

void TriangleRenderer::draw_frame() {
    const auto& f = frame_sync_->current();

    vkWaitForFences(device_.handle(), 1, &f.in_flight, VK_TRUE, UINT64_MAX);

    uint32_t image_index = 0;
    VkResult acquire = vkAcquireNextImageKHR(
        device_.handle(), swapchain_->handle(),
        UINT64_MAX,
        f.image_available, VK_NULL_HANDLE,
        &image_index);

    if (acquire == VK_ERROR_OUT_OF_DATE_KHR) {
        recreate_swapchain();
        return;
    }
    if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("vkAcquireNextImageKHR failed");
    }

    vkResetFences(device_.handle(), 1, &f.in_flight);

    VkCommandBuffer cmd = command_buffers_[frame_sync_->current_index()];
    vkResetCommandBuffer(cmd, 0);
    record_command_buffer(cmd, image_index);

    VkPipelineStageFlags wait_stages[1] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    VkSemaphore render_done = render_finished_[image_index];

    VkSubmitInfo si = {};
    si.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount   = 1;
    si.pWaitSemaphores      = &f.image_available;
    si.pWaitDstStageMask    = wait_stages;
    si.commandBufferCount   = 1;
    si.pCommandBuffers      = &cmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores    = &render_done;
    VK_CHECK(vkQueueSubmit(device_.graphics_queue(), 1, &si, f.in_flight));

    VkSwapchainKHR swap_chains[1] = { swapchain_->handle() };
    VkPresentInfoKHR pi = {};
    pi.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores    = &render_done;
    pi.swapchainCount     = 1;
    pi.pSwapchains        = swap_chains;
    pi.pImageIndices      = &image_index;

    VkResult present = vkQueuePresentKHR(device_.present_queue(), &pi);
    bool need_recreate = window_.framebuffer_resized();
    if (present == VK_ERROR_OUT_OF_DATE_KHR || present == VK_SUBOPTIMAL_KHR || need_recreate) {
        window_.reset_framebuffer_resized();
        recreate_swapchain();
    } else if (present != VK_SUCCESS) {
        throw std::runtime_error("vkQueuePresentKHR failed");
    }

    frame_sync_->advance();
}

}  // namespace routes_label::renderer
