#pragma once
//
// debug/IDebugWidget.h
// Debug HUD 中可插拔的"小部件"抽象。每帧两阶段：
//   - tick()   : 更新内部状态（不调用任何 ImGui::*），可在 ImGui::NewFrame 之前调用
//   - render() : 在 ImGui::NewFrame() 之后被调用，用 ImGui API 输出 UI
// DebugOverlay 持有一组 widgets，本身只负责 imgui 后端生命周期，不感知任何 widget 内部细节。
//

namespace routes_label::debug {

class IDebugWidget {
public:
    virtual ~IDebugWidget() = default;

    // 每帧统计/状态更新（不允许调任何 ImGui::*）
    virtual void tick() = 0;

    // 每帧 ImGui 输出（可调任意 ImGui::Begin/.../End）
    virtual void render() = 0;
};

}  // namespace routes_label::debug
