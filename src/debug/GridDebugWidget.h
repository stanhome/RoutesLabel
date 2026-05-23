#pragma once
//
// debug/GridDebugWidget.h
// Routes-Select Grid 算法调试面板：
//   - Algorithm backend 切换（CPU / GPU），GPU 不可用时置灰强制 CPU
//   - "Force recompute every frame" 开关（让 CPU/GPU 帧率差异直观可见）
//   - 显示 CPU 与 GPU 各自最近一次 compute_ms
//   - GPU 状态行：available / fallback reason
//   - 编辑 grid 参数（n_grid / separation 阈值 / arclength_balance / nms_dist）
//   - 开关：PCA 主轴可视化 / 选中 tile 高亮
//   - 把面板本帧的实际矩形（logical 像素）暴露给外部，供算法将其作为
//     obstructing rect 加入 label 摆放硬约束（doc routes-select.md §1.1）
//
// 不持有算法实例：状态由调用方（RoutesRenderer）维护，本 widget 仅提供 UI。
//

#include "algo/GridCommon.h"
#include "debug/IDebugWidget.h"
#include "renderer/MapView.h"

#include <array>
#include <string>

namespace routes_label::debug {

enum class AlgoBackend : int {
    Cpu = 0,
    Gpu = 1,
};

class GridDebugWidget : public IDebugWidget {
public:
    GridDebugWidget() = default;

    // ---- inputs ----
    void SetSnapshot(const algo::GridDebugSnapshot& s) { snapshot_ = s; have_snapshot_ = true; }

    // 分别注入 CPU / GPU 最近一次 compute 时间（用于面板上的双行对比）
    void SetCpuComputeMs(float ms) { last_cpu_ms_ = ms; }
    void SetGpuComputeMs(float ms) { last_gpu_ms_ = ms; }

    // GPU 端 timestamp 精测值（仅 GPU 路径有意义；< 0 表示 N/A）
    //   - gpu_work_ms       = 5 个 dispatch 的纯 GPU 端执行时间（不含 fill / copy / submit / fence wait）
    //   - gpu_round_trip_ms = cmd buffer 内 GPU 端总耗时（含 zero-init fill + 5 dispatch + staging copy）
    void SetGpuWorkMs(float ms)      { last_gpu_work_ms_       = ms; }
    void SetGpuRoundTripMs(float ms) { last_gpu_round_trip_ms_ = ms; }

    // GPU backend 是否可用（对应 GridGpu::is_available()）。
    // GPU 可用 → 自动切到 GPU 模式（让用户开机就看到 GPU 性能）；
    // GPU 不可用 → 强制 CPU 模式。
    void SetGpuAvailable(bool ok) {
        gpu_available_ = ok;
        backend_ = ok ? AlgoBackend::Gpu : AlgoBackend::Cpu;
        dirty_ = true;
    }
    void SetGpuStatusText(std::string s) { gpu_status_text_ = std::move(s); }

    // 上层无法初始化 GPU 或运行时 fallback → 强制留在 CPU 模式
    void ForceCpuMode() {
        backend_       = AlgoBackend::Cpu;
        gpu_available_ = false;
        dirty_         = true;
    }

    // 每帧由 RoutesRenderer 注入：snapshot 中所有几何（tile_size / pca.mu / pca.axis_u）
    // 单位为 **map-world px**，需通过 MapView 上屏。
    void SetMapView(const renderer::MapView& mv) { map_view_ = mv; have_map_view_ = true; }

    // ---- outputs (调用方每帧读取) ----
    [[nodiscard]] algo::GridParams  params_overrides() const { return params_; }
    [[nodiscard]] bool show_pca_axis()   const { return show_pca_axis_; }
    [[nodiscard]] bool show_selected()   const { return show_selected_; }
    [[nodiscard]] AlgoBackend backend()  const { return backend_; }
    [[nodiscard]] bool force_recompute() const { return force_recompute_; }
    // 是否要求 GPU 路径回拉 PCA / TileScore 调试 staging（关闭可显著减少 readback 开销，
    // 用于直观验证 readback 在 GPU 模式 round-trip 时间中的占比）。
    [[nodiscard]] bool collect_gpu_debug() const { return collect_gpu_debug_; }

    // dirty 检测：force_recompute=true 时永远返回 true，让上层每帧都重算。
    [[nodiscard]] bool dirty_and_clear() {
        if (force_recompute_) return true;
        bool d = dirty_;
        dirty_ = false;
        return d;
    }

    // 上一帧面板矩形（logical 像素，左上为 mn）。!valid → 面板未渲染或被折叠。
    struct PanelRectLogical {
        float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
        bool  valid = false;
    };
    [[nodiscard]] PanelRectLogical panel_rect_logical() const { return last_panel_rect_; }

    // 初始化默认 grid 参数（屏幕尺寸 + 默认值）。
    void InitParams(const algo::GridParams& p) {
        if (!params_inited_) { params_ = p; params_inited_ = true; }
    }
    // 屏幕尺寸变化时同步（不影响其他参数）
    void UpdateScreen(float w, float h) {
        if (params_.screen_w != w || params_.screen_h != h) {
            params_.screen_w = w;
            params_.screen_h = h;
            dirty_ = true;
        }
    }

    void tick()   override {}
    void render() override;

private:
    algo::GridParams         params_{};
    algo::GridDebugSnapshot  snapshot_{};
    bool params_inited_     = false;
    bool have_snapshot_     = false;
    bool show_pca_axis_     = false;
    bool show_selected_     = true;
    bool dirty_             = true;   // 初始化时标记 dirty 以触发首次计算
    PanelRectLogical last_panel_rect_{};
    renderer::MapView map_view_{};
    bool have_map_view_ = false;

    // backend 切换状态
    AlgoBackend backend_         = AlgoBackend::Cpu;   // 默认 CPU；GPU 初始化成功后由上层切到 Gpu
    bool        gpu_available_   = false;
    bool        force_recompute_ = false;   // 默认事件驱动；用户可在面板勾选打开以观察 CPU/GPU 帧率差异
    bool        collect_gpu_debug_ = true;  // GPU 路径是否回拉 PCA/TileScore staging（关闭后 PCA/Selected 可视化失效）
    float       last_cpu_ms_     = 0.0f;
    float       last_gpu_ms_     = 0.0f;
    float       last_gpu_work_ms_       = -1.0f;   // < 0 = N/A
    float       last_gpu_round_trip_ms_ = -1.0f;
    std::string gpu_status_text_ = "Initializing...";
};

}  // namespace routes_label::debug
