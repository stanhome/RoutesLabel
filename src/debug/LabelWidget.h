#pragma once
//
// debug/LabelWidget.h
// 在屏幕上渲染 3 个路径的 label 矩形 + leader line + 通行时间文本。
//
// 不属于"调试 HUD"严格意义——这是产品功能的可视化呈现。但作为 demo，复用 ImGui
// 的 ImDrawList 是最快的工程路径（无需自建 graphics pipeline）。
//
// 用法（每帧）：
//   label_widget.SetTravelTimes(35, 30, 20);
//   label_widget.SetResults(grid_result.labels);
//   label_widget.tick();
//   label_widget.render();
//

#include "algo/GridCommon.h"
#include "debug/IDebugWidget.h"
#include "renderer/MapView.h"

#include <array>
#include <cstdint>

namespace routes_label::debug {

class LabelWidget : public IDebugWidget {
public:
    LabelWidget();

    void SetTravelTimes(const algo::TravelTimes& t) { travel_ = t; }
    void SetResults(const std::array<algo::LabelResult, algo::kRouteCount>& results) {
        results_ = results;
    }
    // 路径配色（与 ribbon mesh 颜色保持一致）。RGB ∈ [0, 1]。
    void SetRouteColors(const std::array<std::array<float, 3>, algo::kRouteCount>& colors) {
        colors_ = colors;
    }
    // 每帧由 RoutesRenderer 注入：world(map-px) → logical/fb 的统一坐标变换。
    void SetMapView(const renderer::MapView& mv) { map_view_ = mv; have_map_view_ = true; }

    void tick()   override {}    // 无内部状态，render() 直接消费最新 results_
    void render() override;

private:
    algo::TravelTimes                                              travel_{};
    std::array<algo::LabelResult, algo::kRouteCount>               results_{};
    std::array<std::array<float, 3>, algo::kRouteCount>            colors_{
        std::array<float, 3>{ 0.95f, 0.4f, 0.4f },   // A 红
        std::array<float, 3>{ 0.45f, 0.85f, 0.5f },  // B 绿
        std::array<float, 3>{ 0.45f, 0.65f, 1.0f },  // C 蓝
    };
    renderer::MapView                                              map_view_{};
    bool                                                           have_map_view_ = false;
};

}  // namespace routes_label::debug
