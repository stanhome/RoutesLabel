#pragma once
//
// debug/FpsWidget.h
// 一个独立可复用的 debug 小部件：在屏幕左上角浮窗显示帧率与帧时（ms）。
//
// 用法：
//   FpsWidget fps;
//   每帧：fps.tick(); fps.render();
//
// 设计要点：
//   - tick() 用 std::chrono::steady_clock 测量帧间 dt，EMA 平滑；不调任何 ImGui::*。
//   - render() 用 ImGui::SetNextWindowPos + Begin(NoTitleBar|NoResize|NoMove|AlwaysAutoResize)
//     画一个半透明小浮窗，绝不抢业务输入焦点。
//   - 不持有任何 GPU 资源，所有渲染交给 imgui。
//

#include "debug/IDebugWidget.h"

#include <chrono>

namespace routes_label::debug {

class FpsWidget : public IDebugWidget {
public:
    FpsWidget() = default;

    // EMA 系数：值越小越平滑（默认 0.05 ≈ 20 帧时间常数）。
    void set_smoothing_alpha(float alpha) { alpha_ = alpha; }

    // 屏幕左上角的偏移量（像素）；默认 (10, 10)。
    void set_screen_offset(float x, float y) { offset_x_ = x; offset_y_ = y; }

    // IDebugWidget
    void tick()   override;
    void render() override;

private:
    using clock      = std::chrono::steady_clock;
    using time_point = std::chrono::steady_clock::time_point;

    time_point last_tp_{};
    bool       have_last_tp_ = false;
    float      fps_ema_      = 0.0f;
    float      ms_ema_       = 0.0f;
    float      alpha_        = 0.05f;
    float      offset_x_     = 10.0f;
    float      offset_y_     = 10.0f;
};

}  // namespace routes_label::debug
