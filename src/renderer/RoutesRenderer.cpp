#include "renderer/RoutesRenderer.h"

#include "algo/GridCpu.h"
#include "algo/GridGpu.h"
#include "debug/DebugOverlay.h"
#include "debug/FpsWidget.h"
#include "debug/GridDebugWidget.h"
#include "debug/LabelWidget.h"
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

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace routes_label::renderer {

namespace {

constexpr float kRibbonWidthPx = 8.0f;

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
    grid_widget_.reset();
    label_widget_.reset();
    fps_widget_.reset();
    overlay_.reset();
    grid_gpu_.reset();
    grid_cpu_.reset();
    scene_.reset();

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
    scene_ = std::make_unique<RouteScene>(std::move(data));
    const RibbonMesh mesh = scene_->build_ribbon_mesh(kRibbonWidthPx);

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

    // Routes-Select Grid 算法（doc/routes-select-grid-gpu.md）
    // - GridCpu：ground truth + 低端机 fallback，永远存在
    // - GridGpu：4-stage compute pipeline，运行期由 GridDebugWidget 切换
    grid_cpu_     = std::make_unique<algo::GridCpu>();
#if defined(ROUTES_LABEL_USE_GPU) && ROUTES_LABEL_USE_GPU
    grid_gpu_     = std::make_unique<algo::GridGpu>(device_, physical_);
#endif
    label_widget_ = std::make_unique<debug::LabelWidget>();
    grid_widget_  = std::make_unique<debug::GridDebugWidget>();

    // 把 GPU 可用状态注入 widget；如果 GPU 初始化失败 widget 会强制 CPU 模式
    const bool gpu_ok = (grid_gpu_ != nullptr) && grid_gpu_->is_available();
    grid_widget_->SetGpuAvailable(gpu_ok);
    if (gpu_ok) {
        grid_widget_->SetGpuStatusText("available");
    } else if (grid_gpu_ == nullptr) {
        grid_widget_->SetGpuStatusText("disabled at build time");
    } else {
        grid_widget_->SetGpuStatusText(grid_gpu_->last_error().empty()
            ? std::string("unavailable")
            : grid_gpu_->last_error());
    }

    // 把场景配色传给 label widget
    if (scene_) {
        std::array<std::array<float, 3>, algo::kRouteCount> colors{};
        const auto& styles = scene_->data().styles;
        for (size_t i = 0; i < algo::kRouteCount && i < styles.size(); ++i) {
            colors[i][0] = styles[i].color[0];
            colors[i][1] = styles[i].color[1];
            colors[i][2] = styles[i].color[2];
        }
        label_widget_->SetRouteColors(colors);
    }

    // 初始化 grid 参数：screen_w/h = mapViewRect 尺寸（map-world px，doc/map-world-space.md）。
    algo::GridParams p_init{};
    if (scene_) {
        const auto& mr = scene_->data().map_context.mapViewRect;
        p_init.screen_w = static_cast<float>(mr.w);
        p_init.screen_h = static_cast<float>(mr.h);
    } else {
        p_init.screen_w = static_cast<float>(swapchain_->extent().width);
        p_init.screen_h = static_cast<float>(swapchain_->extent().height);
    }
    grid_widget_->InitParams(p_init);
}

// -----------------------------------------------------------------------------
// MapView 计算（每帧一次，O(1) mat4 mul + 几个 lerp）
// -----------------------------------------------------------------------------

MapView RoutesRenderer::compute_map_view(VkExtent2D extent) const {
    // mapViewRect 是 ground truth 的 world 空间矩形（doc/map-world-space.md）
    const auto& mr = (scene_ ? scene_->data().map_context.mapViewRect
                             : core::Rect{ 0, 0, static_cast<double>(extent.width),
                                                 static_cast<double>(extent.height) });
    const ImGuiIO& io = ImGui::GetIO();
    const float fb_scale_x = (io.DisplayFramebufferScale.x > 0.0f) ? io.DisplayFramebufferScale.x : 1.0f;
    const float fb_scale_y = (io.DisplayFramebufferScale.y > 0.0f) ? io.DisplayFramebufferScale.y : 1.0f;
    return MapView::compute(
        static_cast<float>(mr.x), static_cast<float>(mr.y),
        static_cast<float>(mr.w), static_cast<float>(mr.h),
        extent.width, extent.height,
        fb_scale_x, fb_scale_y);
}

// -----------------------------------------------------------------------------
// Per-frame UBO update
// -----------------------------------------------------------------------------

void RoutesRenderer::update_ubo_for_frame(uint32_t frame_index, VkExtent2D extent, const MapView& map_view) {
    // World-space (mapViewRect) → clip via contain fit-to-window letterbox。
    // 仅 extent 变化时打一行日志，避免 spam。
    if (extent.width != last_logged_extent_.width || extent.height != last_logged_extent_.height) {
        LOG_INFO("[RoutesRenderer] viewport: fb=" << extent.width << "x" << extent.height
                 << "  scale=" << map_view.scale
                 << "  letterbox_offset=(" << map_view.offset_fb.x << ", "
                 << map_view.offset_fb.y << ")");
        last_logged_extent_ = extent;
    }
    UboGlobals u = {};
    u.projection = map_view.world_to_clip;
    std::memcpy(ubo_per_frame_[frame_index].mapped(), &u, sizeof(UboGlobals));
}

// -----------------------------------------------------------------------------
// Routes-Select Grid 算法重算（事件驱动）
// 触发条件：调试参数变化 / panel **world-space** AABB 变化 / 首次启动。
// 注意：grid 切分基于 mapViewRect（world 空间，固定）；但 panel 是 logical-px 浮窗，
// 窗口缩放会改变 panel 在 world 空间的 AABB（即使 panel 的 logical rect 不变），
// 此时必须重算，否则 label 与 panel 在屏幕上会 overlap（doc/map-world-space.md §7）。
// -----------------------------------------------------------------------------

void RoutesRenderer::recompute_label_layout(const MapView& map_view) {
    if (!grid_cpu_ || !scene_ || !grid_widget_) return;

    // World 空间 ground truth（mapViewRect 尺寸，与窗口尺寸解耦）
    const auto& mr = scene_->data().map_context.mapViewRect;
    const float world_w = static_cast<float>(mr.w);
    const float world_h = static_cast<float>(mr.h);
    grid_widget_->UpdateScreen(world_w, world_h);

    // 1. 计算本帧 panel 的 world-space AABB（来自本帧 panel logical rect + 本帧 MapView）
    algo::AABBf panel_world{};
    bool        panel_world_valid = false;
    const auto  pr = grid_widget_->panel_rect_logical();
    if (pr.valid) {
        const glm::vec2 mn_w = map_view.logical_to_world({ pr.x, pr.y });
        const glm::vec2 mx_w = map_view.logical_to_world({ pr.x + pr.w, pr.y + pr.h });
        panel_world.mn.x = mn_w.x;
        panel_world.mn.y = mn_w.y;
        panel_world.mx.x = mx_w.x;
        panel_world.mx.y = mx_w.y;
        panel_world_valid = true;
    }

    // 2. 检测 panel world AABB 是否相对上次发生 ≥ 1 world-px 的变化（抖动忽略）
    auto panel_world_changed = [&]() -> bool {
        if (panel_world_valid != have_last_panel_world_) return true;
        if (!panel_world_valid) return false;
        constexpr float kWorldChangeThr = 1.0f;   // 1 world-px
        return std::abs(panel_world.mn.x - last_panel_world_.mn.x) > kWorldChangeThr
            || std::abs(panel_world.mn.y - last_panel_world_.mn.y) > kWorldChangeThr
            || std::abs(panel_world.mx.x - last_panel_world_.mx.x) > kWorldChangeThr
            || std::abs(panel_world.mx.y - last_panel_world_.mx.y) > kWorldChangeThr;
    };

    const bool widget_dirty = grid_widget_->dirty_and_clear();
    const bool panel_dirty  = panel_world_changed();
    if (!widget_dirty && !panel_dirty && grid_result_.compute_ms > 0.0f) {
        return;   // 无事件，跳过重算
    }

    // 3. 拼参数 + 调算法
    algo::GridParams params = grid_widget_->params_overrides();
    params.screen_w = world_w;
    params.screen_h = world_h;
    params.panel_rect = panel_world_valid ? panel_world : algo::AABBf{};

    const algo::Polylines polys = scene_->to_algo_polylines();

    // 按 widget.backend() 派发：CPU 路径永远可用；GPU 路径在不可用 / 失败时
    // 自动 fallback CPU 并 ForceCpuMode 防止 UI 继续选 GPU。
    const debug::AlgoBackend bk = grid_widget_->backend();
    bool used_gpu = false;
    if (bk == debug::AlgoBackend::Gpu && grid_gpu_ && grid_gpu_->is_available()) {
        const bool collect_debug = grid_widget_->collect_gpu_debug();
        algo::GridResult r = grid_gpu_->compute(polys, params, collect_debug);
        // 任何 label PoolOverflow → 视为 GPU 失效，自动降级
        bool overflow = false;
        for (int i = 0; i < algo::kRouteCount; ++i) {
            if (r.labels[i].status == algo::LabelStatus::PoolOverflow) { overflow = true; break; }
        }
        if (overflow) {
            LOG_WARN("[RoutesRenderer] GPU pool overflow, falling back to CPU");
            grid_widget_->SetGpuStatusText("pool overflow, fallback CPU");
            grid_widget_->ForceCpuMode();
        } else {
            last_gpu_result_ = r;
            grid_result_     = r;
            grid_widget_->SetGpuComputeMs(r.compute_ms);
            grid_widget_->SetGpuWorkMs(r.gpu_work_ms);
            grid_widget_->SetGpuRoundTripMs(r.gpu_round_trip_ms);
            used_gpu = true;
        }
    }

    if (!used_gpu) {
        algo::GridResult r = grid_cpu_->compute(polys, params, /*collect_debug=*/true);
        last_cpu_result_ = r;
        grid_result_     = r;
        grid_widget_->SetCpuComputeMs(r.compute_ms);
    }

    grid_widget_->SetSnapshot(grid_result_.debug);

    // 4. 记录本次 panel world AABB 作为下次对比基准
    last_panel_world_      = panel_world;
    have_last_panel_world_ = panel_world_valid;
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
    // 本帧 MapView：window/extent → contain fit-to-window VP（map-world → clip）
    const VkExtent2D extent = swapchain_->extent();
    const MapView map_view = compute_map_view(extent);

    // ---- Debug HUD: 帧开始 ----
    // 设计顺序（关键）：
    //   1. begin_frame
    //   2. 把本帧 MapView 注入到 widgets，使其 world→logical 变换可用
    //   3. fps_widget + grid_widget 渲染 panel（在 Begin/End 内取本帧实际 rect）
    //   4. recompute_label_layout：把 panel rect 作为 obstructing rect 注入 GridParams，
    //      在事件驱动条件下重算 label 摆放（用本帧 panel rect，零延迟）
    //   5. label_widget 渲染（用刚算出的 label_center / leader）
    //   6. end_frame
    // 这样 panel 被 label 误盖的问题在视觉上不可能出现。
    if (overlay_) {
        overlay_->begin_frame();
        if (grid_widget_) grid_widget_->SetMapView(map_view);
        if (label_widget_) label_widget_->SetMapView(map_view);

        if (fps_widget_) {
            fps_widget_->tick();
            fps_widget_->render();
        }
        if (grid_widget_) {
            grid_widget_->tick();
            grid_widget_->render();
        }

        // ---- 算法侧重算（事件驱动，使用本帧 panel rect）----
        // 注意：窗口缩放本身**不再**触发重算，仅 panel rect / 参数变化时才重算。
        recompute_label_layout(map_view);

        if (label_widget_) {
            // Travel times：固定 35 / 30 / 20 分钟（demo 模拟，不可调）
            algo::TravelTimes fixed_times;
            fixed_times.minutes_a = 35;
            fixed_times.minutes_b = 30;
            fixed_times.minutes_c = 20;
            label_widget_->SetTravelTimes(fixed_times);
            label_widget_->SetResults(grid_result_.labels);
            label_widget_->tick();
            label_widget_->render();
        }
        overlay_->end_frame();
    } else {
        // 没有 overlay 的极端情形：仍要做一次算法重算
        recompute_label_layout(map_view);
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
    update_ubo_for_frame(fi, swapchain_->extent(), map_view);

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
