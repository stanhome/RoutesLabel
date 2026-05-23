//
// debug/FpsWidget.cpp
//

#include "debug/FpsWidget.h"

#include <imgui.h>

namespace routes_label::debug {

void FpsWidget::tick() {
    const time_point now = clock::now();
    if (have_last_tp_) {
        const float dt = std::chrono::duration<float>(now - last_tp_).count();
        if (dt > 1e-7f) {
            const float inst_fps = 1.0f / dt;
            const float inst_ms  = dt * 1000.0f;
            if (fps_ema_ == 0.0f) {
                fps_ema_ = inst_fps;
                ms_ema_  = inst_ms;
            } else {
                const float a = alpha_;
                fps_ema_ = fps_ema_ * (1.0f - a) + inst_fps * a;
                ms_ema_  = ms_ema_  * (1.0f - a) + inst_ms  * a;
            }
        }
    } else {
        have_last_tp_ = true;
    }
    last_tp_ = now;
}

void FpsWidget::render() {
    // 浮窗放在屏幕左上角，半透明、不抢输入、自动收缩
    ImGui::SetNextWindowPos(ImVec2(offset_x_, offset_y_), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.45f);

    constexpr ImGuiWindowFlags kFlags =
        ImGuiWindowFlags_NoTitleBar       |
        ImGuiWindowFlags_NoResize         |
        ImGuiWindowFlags_NoMove           |
        ImGuiWindowFlags_NoScrollbar      |
        ImGuiWindowFlags_NoSavedSettings  |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav            |
        ImGuiWindowFlags_AlwaysAutoResize;

    if (ImGui::Begin("##FpsWidget", nullptr, kFlags)) {
        ImGui::Text("FPS: %d  (%.2f ms)",
                    static_cast<int>(fps_ema_ + 0.5f),
                    ms_ema_);
    }
    ImGui::End();
}

}  // namespace routes_label::debug
