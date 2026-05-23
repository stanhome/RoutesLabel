#pragma once
//
// renderer/RoutesRenderer.h
// 高层渲染器：把 RouteScene 的 ribbon mesh 上传到 GPU 并每帧绘制；并通过
// debug::DebugOverlay 把 FPS 等 debug HUD 叠加到同一帧。
//
// 资源生命周期：
//   - VBO / IBO：device-local，构造时一次性 staging 上传，析构时销毁。
//   - UBO：每帧一个 host-visible 持久映射，draw_frame 写入 ortho 矩阵。
//   - DescriptorSetLayout / Pool / Sets：UBO binding=0，每帧一个 set，构造时一次性 update。
//   - DebugOverlay / FpsWidget：debug HUD，复用 RoutesRenderer 的 VkRenderPass。
//

#include "algo/GridCommon.h"
#include "platform/Window.h"
#include "renderer/MapView.h"
#include "renderer/RouteScene.h"
#include "rhi/Buffer.h"

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
class DescriptorSetLayout;
class DescriptorPool;
class DescriptorSets;
}  // namespace routes_label::rhi

namespace routes_label::algo {
class GridCpu;
class GridGpu;
}  // namespace routes_label::algo

namespace routes_label::debug {
class DebugOverlay;
class FpsWidget;
class LabelWidget;
class GridDebugWidget;
}  // namespace routes_label::debug

namespace routes_label::renderer {

class RoutesRenderer {
public:
    RoutesRenderer(rhi::Instance& instance,
                   rhi::PhysicalDevice& physical,
                   rhi::Device& device,
                   platform::Window& window,
                   VkSurfaceKHR surface);
    ~RoutesRenderer();

    RoutesRenderer(const RoutesRenderer&)            = delete;
    RoutesRenderer& operator=(const RoutesRenderer&) = delete;

    void draw_frame();
    void on_framebuffer_resize();
    void wait_idle() const;

private:
    void create_swapchain_dependent();
    void destroy_swapchain_dependent();
    void recreate_swapchain();

    void load_scene_and_upload();
    void create_descriptors();
    void create_pipeline();
    void create_debug_overlay();

    void update_ubo_for_frame(uint32_t frame_index, VkExtent2D extent, const MapView& map_view);
    void record_command_buffer(VkCommandBuffer cmd, uint32_t image_index);

    // 在主线程更新 algo state（事件驱动：屏幕变化 / 参数变化时调用）。
    void recompute_label_layout(const MapView& map_view);

    // 计算本帧的 MapView（contain fit-to-window）。
    MapView compute_map_view(VkExtent2D extent) const;

    rhi::Instance&        instance_;
    rhi::PhysicalDevice&  physical_;
    rhi::Device&          device_;
    platform::Window&     window_;
    VkSurfaceKHR          surface_;

    std::unique_ptr<rhi::Swapchain>           swapchain_;
    std::unique_ptr<rhi::RenderPass>          render_pass_;
    std::unique_ptr<rhi::Framebuffers>        framebuffers_;
    std::unique_ptr<rhi::DescriptorSetLayout> set_layout_;
    std::unique_ptr<rhi::DescriptorPool>      desc_pool_;
    std::unique_ptr<rhi::DescriptorSets>      desc_sets_;
    std::unique_ptr<rhi::GraphicsPipeline>    pipeline_;
    std::unique_ptr<rhi::CommandPool>         command_pool_;
    std::unique_ptr<rhi::FrameSync>           frame_sync_;

    std::vector<VkCommandBuffer>              command_buffers_;
    std::vector<VkSemaphore>                  render_finished_;  // per swapchain image

    // 静态 routes 几何（构造期一次性 build + device-local 上传）
    std::unique_ptr<rhi::Buffer>              vbo_;
    std::unique_ptr<rhi::Buffer>              ibo_;
    uint32_t                                  index_count_ = 0;

    std::vector<rhi::Buffer>                  ubo_per_frame_;    // size == kMaxFramesInFlight

    // Debug HUD（imgui 容器 + 各 widget）；析构在主资源之前完成 wait_idle + shutdown
    std::unique_ptr<debug::DebugOverlay>      overlay_;
    std::unique_ptr<debug::FpsWidget>         fps_widget_;

    // Routes-Select Grid 算法状态（doc/routes-select-grid-gpu.md）
    // CPU 实现作为 ground truth + 低端机 fallback，GPU 实现作为高性能路径，运行期由
    // GridDebugWidget 的 backend toggle 切换。
    std::unique_ptr<algo::GridCpu>            grid_cpu_;
    std::unique_ptr<algo::GridGpu>            grid_gpu_;
    std::unique_ptr<RouteScene>               scene_;            // 持有原始 polyline，用于事件驱动重算
    algo::GridResult                          grid_result_;      // 当前展示用结果（来自 active backend）
    algo::GridResult                          last_cpu_result_;  // 用于 backend 切换时即时回放
    algo::GridResult                          last_gpu_result_;
    std::unique_ptr<debug::LabelWidget>       label_widget_;
    std::unique_ptr<debug::GridDebugWidget>   grid_widget_;
    // 仅日志去重，不作为算法重算依据。算法 grid 在 map-world 空间切分（参见
    // doc/map-world-space.md），窗口缩放仅触发 view-projection 重算，不触发 algo。
    VkExtent2D                                last_logged_extent_ = { 0, 0 };

    // 上次注入算法的 panel world-space AABB（mn.x, mn.y, mx.x, mx.y）。
    // 关键：当窗口缩放但 panel 的 logical rect 不变时（如仅改变窗口高度），
    // panel 在 world 空间的投影 AABB 仍会变 —— 此时必须重算 label 摆放，
    // 否则 label 与 panel 在屏幕上会出现 overlap（issue 修复）。
    bool                                      have_last_panel_world_ = false;
    algo::AABBf                               last_panel_world_{};
};

}  // namespace routes_label::renderer
