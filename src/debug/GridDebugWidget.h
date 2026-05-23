#pragma once
//
// debug/GridDebugWidget.h
// Routes-Select Grid 算法调试面板：
//   - 显示算法计算时间（CPU 单线程 ms）
//   - 编辑 grid 参数（n_grid / separation 阈值 / arclength_balance / nms_dist）
//   - 开关：PCA 主轴可视化 / 选中 tile 高亮
//   - 把面板本帧的实际矩形（logical 像素）暴露给外部，供算法将其作为
//     obstructing rect 加入 label 摆放硬约束（doc routes-select.md §1.1 MapContext.obstructingViews）
//
// 不持有算法实例：状态由调用方（RoutesRenderer）维护，本 widget 仅提供 UI。
//

#include "algo/GridCommon.h"
#include "debug/IDebugWidget.h"
#include "renderer/MapView.h"

#include <array>

namespace routes_label::debug {

class GridDebugWidget : public IDebugWidget {
public:
    GridDebugWidget() = default;

    // ---- inputs ----
    void SetSnapshot(const algo::GridDebugSnapshot& s) { snapshot_ = s; have_snapshot_ = true; }
    void SetComputeMs(float ms) { last_compute_ms_ = ms; }
    // 每帧由 RoutesRenderer 注入：snapshot 中所有几何（tile_size / pca.mu / pca.axis_u）
    // 单位为 **map-world px**，需通过 MapView 上屏。
    void SetMapView(const renderer::MapView& mv) { map_view_ = mv; have_map_view_ = true; }

    // ---- outputs (调用方每帧读取) ----
    [[nodiscard]] algo::GridParams  params_overrides() const { return params_; }
    [[nodiscard]] bool show_pca_axis()   const { return show_pca_axis_; }
    [[nodiscard]] bool show_selected()   const { return show_selected_; }
    [[nodiscard]] bool dirty_and_clear()       { bool d = dirty_; dirty_ = false; return d; }

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
    float last_compute_ms_  = 0.0f;
    PanelRectLogical last_panel_rect_{};
    renderer::MapView map_view_{};
    bool have_map_view_ = false;
};

}  // namespace routes_label::debug
