#include "renderer/RoutesRenderer.h"

#include "debug/DebugOverlay.h"
#include "debug/FpsWidget.h"
#include "renderer/RouteScene.h"
#include "rhi/CommandPool.h"
#include "rhi/Descriptor.h"
#include "rhi/Device.h"
#include "rhi/Framebuffer.h"
#include "rhi/FrameSync.h"
#include "rhi/GraphicsPipeline.h"
#include "rhi/Instance.h"
#include "rhi/PhysicalDevice.h"
#include "rhi/RenderPass.h"
#include "rhi/Swapchain.h"
#include "utils/FileSystem.h"
#include "utils/Json.h"
#include "utils/Log.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cstring>
#include <stdexcept>

namespace routes_label::renderer {

namespace {

constexpr float kRibbonWidthPx = 6.0f;

struct UboGlobals {
    glm::mat4 projection;
};

std::filesystem::path shader_path(const char* filename) {
    return utils::executable_dir() / "shaders" / filename;
}

}  // namespace

RoutesRenderer::RoutesRenderer(rhi::Instance& instance,
                               rhi::PhysicalDevice& physical,
                               rhi::Device& device,
                               platform::Window& window,
                               VkSurfaceKHR surface)
    : instance_(instance),
      physical_(physical),
      device_(device),
      window_(window),
      surface_(surface) {

    command_pool_ = std::make_unique<rhi::CommandPool>(device_, device_.graphics_family());
    frame_sync_   = std::make_unique<rhi::FrameSync>(device_);
    command_buffers_ = command_pool_->allocate_primary(rhi::kMaxFramesInFlight);

    // 1. swapchain / render pass / framebuffers / per-image semaphore
    create_swapchain_dependent();

    // 2. 构建 routes ribbon mesh 并一次性 device-local 上传
    load_scene_and_upload();

    // 3. descriptor 三件套 + per-frame UBO
    create_descriptors();

    // 4. pipeline
    create_pipeline();

    // 5. debug overlay（imgui） + 默认 FPS widget
    create_debug_overlay();
}

RoutesRenderer::~RoutesRenderer() {
    device_.wait_idle();
    // 先释放 debug overlay（内部还会 wait_idle 一次，安全）
    fps_widget_.reset();
    overlay_.reset();

    pipeline_.reset();
    desc_sets_.reset();
    desc_pool_.reset();
    set_layout_.reset();
    ubo_per_frame_.clear();
    ibo_.reset();
    vbo_.reset();
    destroy_swapchain_dependent();
    frame_sync_.reset();
    command_pool_.reset();
}

void RoutesRenderer::wait_idle() const {
    device_.wait_idle();
}

void RoutesRenderer::on_framebuffer_resize() {
    // 仅占位，实际重建由 draw_frame 检测到 OUT_OF_DATE / SUBOPTIMAL 触发
}

// -----------------------------------------------------------------------------
// Swapchain-dependent objects
// -----------------------------------------------------------------------------

void RoutesRenderer::create_swapchain_dependent() {
    uint32_t w = 0, h = 0;
    window_.framebuffer_size(w, h);
    swapchain_   = std::make_unique<rhi::Swapchain>(physical_, device_, surface_, w, h);
    render_pass_ = std::make_unique<rhi::RenderPass>(device_, swapchain_->image_format());
    framebuffers_ = std::make_unique<rhi::Framebuffers>(
        device_, *render_pass_, swapchain_->image_views(), swapchain_->extent());

    VkSemaphoreCreateInfo sci = {};
    sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    render_finished_.resize(swapchain_->image_count(), VK_NULL_HANDLE);
    for (auto& s : render_finished_) {
        VK_CHECK(vkCreateSemaphore(device_.handle(), &sci, nullptr, &s));
    }
}

void RoutesRenderer::destroy_swapchain_dependent() {
    for (auto s : render_finished_) {
        if (s != VK_NULL_HANDLE) vkDestroySemaphore(device_.handle(), s, nullptr);
    }
    render_finished_.clear();

    framebuffers_.reset();
    render_pass_.reset();
    swapchain_.reset();
}

void RoutesRenderer::recreate_swapchain() {
    window_.wait_if_minimized();
    if (window_.should_close()) return;

    device_.wait_idle();

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

    LOG_INFO("[RoutesRenderer] swapchain recreated " << w << "x" << h);
}

// -----------------------------------------------------------------------------
// Scene & static buffers
// -----------------------------------------------------------------------------

void RoutesRenderer::load_scene_and_upload() {
    const auto json_path = utils::assets_dir() / "routes_demo.json";
    auto data = utils::load_route_scene_from_json(json_path);
    RouteScene scene(std::move(data));
    const RibbonMesh mesh = scene.build_ribbon_mesh(kRibbonWidthPx);

    if (mesh.vertices.empty() || mesh.indices.empty()) {
        throw std::runtime_error("[RoutesRenderer] empty ribbon mesh");
    }

    const VkDeviceSize vbo_size =
        static_cast<VkDeviceSize>(mesh.vertices.size()) * sizeof(RibbonVertex);
    const VkDeviceSize ibo_size =
        static_cast<VkDeviceSize>(mesh.indices.size())  * sizeof(uint32_t);

    vbo_ = std::make_unique<rhi::Buffer>(rhi::Buffer::CreateDeviceLocal(
        device_, physical_, vbo_size,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        mesh.vertices.data()));
    ibo_ = std::make_unique<rhi::Buffer>(rhi::Buffer::CreateDeviceLocal(
        device_, physical_, ibo_size,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        mesh.indices.data()));

    index_count_ = static_cast<uint32_t>(mesh.indices.size());

    LOG_INFO("[RoutesRenderer] static VBO/IBO uploaded: "
             << mesh.vertices.size() << " verts (" << vbo_size << " B), "
             << mesh.indices.size()  << " idx ("   << ibo_size << " B)");
}

// -----------------------------------------------------------------------------
// Descriptors & UBO
// -----------------------------------------------------------------------------

void RoutesRenderer::create_descriptors() {
    // set=0, binding=0: UBO (vertex stage)
    VkDescriptorSetLayoutBinding b = {};
    b.binding         = 0;
    b.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    b.descriptorCount = 1;
    b.stageFlags      = VK_SHADER_STAGE_VERTEX_BIT;
    set_layout_ = std::make_unique<rhi::DescriptorSetLayout>(
        device_, std::vector<VkDescriptorSetLayoutBinding>{ b });

    std::vector<VkDescriptorPoolSize> sizes = {
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, rhi::kMaxFramesInFlight },
    };
    desc_pool_ = std::make_unique<rhi::DescriptorPool>(
        device_, rhi::kMaxFramesInFlight, sizes);

    desc_sets_ = std::make_unique<rhi::DescriptorSets>(
        device_, *desc_pool_, *set_layout_, rhi::kMaxFramesInFlight);

    // 每帧一个 host-visible UBO
    ubo_per_frame_.reserve(rhi::kMaxFramesInFlight);
    for (uint32_t i = 0; i < rhi::kMaxFramesInFlight; ++i) {
        ubo_per_frame_.emplace_back(rhi::Buffer::CreateHostVisible(
            device_, physical_, sizeof(UboGlobals),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT));
        desc_sets_->update_uniform_buffer(
            /*set=*/i, /*binding=*/0,
            ubo_per_frame_[i].handle(), 0, sizeof(UboGlobals));
    }
}

void RoutesRenderer::create_pipeline() {
    rhi::GraphicsPipelineDesc desc;
    desc.vert_spv = shader_path("routes.vert.spv");
    desc.frag_spv = shader_path("routes.frag.spv");

    VkVertexInputBindingDescription bind = {};
    bind.binding   = 0;
    bind.stride    = sizeof(RibbonVertex);
    bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    desc.vertex_bindings = { bind };

    VkVertexInputAttributeDescription a_pos = {};
    a_pos.location = 0;
    a_pos.binding  = 0;
    a_pos.format   = VK_FORMAT_R32G32_SFLOAT;
    a_pos.offset   = static_cast<uint32_t>(offsetof(RibbonVertex, pos));

    VkVertexInputAttributeDescription a_col = {};
    a_col.location = 1;
    a_col.binding  = 0;
    a_col.format   = VK_FORMAT_R32G32B32_SFLOAT;
    a_col.offset   = static_cast<uint32_t>(offsetof(RibbonVertex, color));

    desc.vertex_attributes = { a_pos, a_col };

    desc.topology          = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    desc.primitive_restart = VK_TRUE;

    desc.descriptor_set_layouts = { set_layout_->handle() };

    pipeline_ = std::make_unique<rhi::GraphicsPipeline>(device_, *render_pass_, desc);
}

void RoutesRenderer::create_debug_overlay() {
    overlay_ = std::make_unique<debug::DebugOverlay>(
        instance_, physical_, device_, window_,
        render_pass_->handle(),
        swapchain_->image_count());
    fps_widget_ = std::make_unique<debug::FpsWidget>();
}

// -----------------------------------------------------------------------------
// Per-frame UBO update
// -----------------------------------------------------------------------------

void RoutesRenderer::update_ubo_for_frame(uint32_t frame_index, VkExtent2D extent) {
    // 像素坐标，原点左上、y 朝下；目标：把 (0,0) 映射到 framebuffer top-left, (w,h) 到 bottom-right。
    // Vulkan clip y 朝下：用 ortho(0, w, 0, h)，y=0→clip.y=-1（顶），y=h→clip.y=+1（底）。
    UboGlobals u = {};
    u.projection = glm::ortho(
        0.0f, static_cast<float>(extent.width),
        0.0f, static_cast<float>(extent.height),
        -1.0f, 1.0f);
    std::memcpy(ubo_per_frame_[frame_index].mapped(), &u, sizeof(UboGlobals));
}

// -----------------------------------------------------------------------------
// Command buffer
// -----------------------------------------------------------------------------

void RoutesRenderer::record_command_buffer(VkCommandBuffer cmd, uint32_t image_index) {
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

    const VkExtent2D ext = swapchain_->extent();

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

    // ---- 业务几何：routes ribbon ----
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_->handle());

    const uint32_t fi = frame_sync_->current_index();
    VkDescriptorSet ds = desc_sets_->at(fi);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipeline_->layout(), 0, 1, &ds, 0, nullptr);

    VkBuffer     vbufs[1] = { vbo_->handle() };
    VkDeviceSize voffs[1] = { 0 };
    vkCmdBindVertexBuffers(cmd, 0, 1, vbufs, voffs);
    vkCmdBindIndexBuffer(cmd, ibo_->handle(), 0, VK_INDEX_TYPE_UINT32);

    vkCmdDrawIndexed(cmd, index_count_, 1, 0, 0, 0);

    // ---- Debug HUD（叠加在同一 subpass 之上）----
    if (overlay_) {
        overlay_->record_draw_data(cmd);
    }

    vkCmdEndRenderPass(cmd);
    VK_CHECK(vkEndCommandBuffer(cmd));
}

// -----------------------------------------------------------------------------
// Frame loop
// -----------------------------------------------------------------------------

void RoutesRenderer::draw_frame() {
    // ---- Debug HUD: 帧开始 + widgets 更新（在录制 cmd 之前完成 ImGui::Render）----
    if (overlay_) {
        overlay_->begin_frame();
        if (fps_widget_) {
            fps_widget_->tick();
            fps_widget_->render();
        }
        overlay_->end_frame();
    }

    // ---- Vulkan 帧驱动 ----
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

    const uint32_t fi = frame_sync_->current_index();
    update_ubo_for_frame(fi, swapchain_->extent());

    VkCommandBuffer cmd = command_buffers_[fi];
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
