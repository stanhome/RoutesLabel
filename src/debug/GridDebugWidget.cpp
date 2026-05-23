//
// debug/GridDebugWidget.cpp
//

#include "debug/GridDebugWidget.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>

namespace routes_label::debug {

void GridDebugWidget::render() {
    // ---- 1. 控制面板（屏幕右上角，每帧重设以跟随窗口缩放）----
    ImGuiIO& io = ImGui::GetIO();
    const float pad = 12.0f;
    // 关键：用 ImGuiCond_Always 让 pos/size 每帧基于最新 io.DisplaySize 重设，
    // 否则窗口缩放后面板会"飘"在首帧位置（doc/map-world-space.md "Anchor stickiness"）。
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - pad, pad),
                            ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(320.0f, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.85f);

    constexpr ImGuiWindowFlags kFlags =
        ImGuiWindowFlags_NoResize        |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_AlwaysAutoResize;

    PanelRectLogical pr{};
    if (ImGui::Begin("Routes Grid Algorithm", nullptr, kFlags)) {
        // ---- Backend 切换（CPU / GPU）----
        ImGui::TextDisabled("Algorithm backend");
        int backend_int = static_cast<int>(backend_);
        bool prev_force = force_recompute_;
        if (!gpu_available_) ImGui::BeginDisabled();
        if (ImGui::RadioButton("CPU", &backend_int, 0)) {
            backend_ = AlgoBackend::Cpu;
            dirty_   = true;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("GPU", &backend_int, 1)) {
            backend_ = AlgoBackend::Gpu;
            dirty_   = true;
        }
        if (!gpu_available_) ImGui::EndDisabled();

        ImGui::Checkbox("Force recompute every frame", &force_recompute_);
        if (force_recompute_ != prev_force) dirty_ = true;

        ImGui::Separator();

        // ---- 双 compute_ms 显示 ----
        if (backend_ == AlgoBackend::Cpu) {
            ImGui::Text("CPU compute: %.3f ms  (active)", last_cpu_ms_);
            ImGui::TextDisabled("GPU compute: %.3f ms", last_gpu_ms_);
        } else {
            ImGui::TextDisabled("CPU compute: %.3f ms", last_cpu_ms_);
            ImGui::Text("GPU compute: %.3f ms  (active)", last_gpu_ms_);
        }
        // GPU 状态行
        if (gpu_available_) {
            ImGui::TextDisabled("GPU: available");
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f),
                "GPU: %s", gpu_status_text_.c_str());
        }

        ImGui::Separator();
        ImGui::Text("Grid: %d x %d  tile=%.1f px",
                    snapshot_.n_x, snapshot_.n_y, snapshot_.tile_size);

        ImGui::Separator();
        ImGui::TextDisabled("Grid parameters");
        if (ImGui::SliderInt("n_grid", &params_.n_grid, 10, 80)) dirty_ = true;
        if (ImGui::SliderFloat("separation_thr", &params_.separationThreshold, 0.1f, 0.95f, "%.2f")) dirty_ = true;
        if (ImGui::SliderFloat("arclength_bal", &params_.arclength_balance, 0.0f, 0.6f, "%.2f")) dirty_ = true;
        if (ImGui::SliderInt("nms_dist", &params_.nms_grid_distance, 1, 5)) dirty_ = true;

        ImGui::Separator();
        ImGui::TextDisabled("Visualization");
        ImGui::Checkbox("PCA principal axis", &show_pca_axis_);
        ImGui::Checkbox("Selected tile",     &show_selected_);

        if (have_snapshot_) {
            ImGui::Separator();
            ImGui::Text("Selected: [%d, %d, %d]",
                        snapshot_.selected_tile_index[0],
                        snapshot_.selected_tile_index[1],
                        snapshot_.selected_tile_index[2]);
        }

        // 取本帧 panel 的实际矩形（logical 像素）—— 关键：在 Begin/End 之间调用
        const ImVec2 wpos  = ImGui::GetWindowPos();
        const ImVec2 wsize = ImGui::GetWindowSize();
        pr.x = wpos.x; pr.y = wpos.y;
        pr.w = wsize.x; pr.h = wsize.y;
        pr.valid = (wsize.x > 1.0f && wsize.y > 1.0f);
    }
    ImGui::End();

    // 若 panel rect 与上次"明显不同"，触发 dirty 让算法重算 label 摆放
    if (pr.valid) {
        const float dx = std::abs(pr.x - last_panel_rect_.x);
        const float dy = std::abs(pr.y - last_panel_rect_.y);
        const float dw = std::abs(pr.w - last_panel_rect_.w);
        const float dh = std::abs(pr.h - last_panel_rect_.h);
        constexpr float kRectChangeThr = 1.0f;   // 1 logical px 抖动忽略
        if (!last_panel_rect_.valid
         || dx > kRectChangeThr || dy > kRectChangeThr
         || dw > kRectChangeThr || dh > kRectChangeThr) {
            dirty_ = true;
        }
    }
    last_panel_rect_ = pr;

    // ---- 2. 在前景图层叠加可视化 ----
    if (!have_snapshot_) return;
    if (!have_map_view_) return;   // RoutesRenderer 必须每帧 SetMapView
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    if (!dl) return;

    // snapshot 内的几何（tile_size / pca.mu / pca.axis_u）单位 = **map-world px**，
    // 通过 MapView::world_to_logical 上屏（contain fit-to-window + fb→logical）。
    auto world_to_logical = [&](float wx, float wy) {
        const glm::vec2 l = map_view_.world_to_logical({ wx, wy });
        return ImVec2(l.x, l.y);
    };

    const float s_world = snapshot_.tile_size;            // tile 边长（world px）
    // tile size 在屏幕（logical px）上的尺寸 = world_size * world→logical scale
    const float to_logical_uniform = map_view_.world_to_logical_scale_uniform();
    const ImVec2 tile_size_logical{ s_world * to_logical_uniform, s_world * to_logical_uniform };

    // 2a. PCA 主轴
    if (show_pca_axis_) {
        for (int cy = 0; cy < snapshot_.n_y; ++cy) {
            for (int cx = 0; cx < snapshot_.n_x; ++cx) {
                const int idx = cy * snapshot_.n_x + cx;
                if (idx >= static_cast<int>(snapshot_.tile_pca.size())) continue;
                const auto& pca = snapshot_.tile_pca[idx];
                if (!pca.valid) continue;
                const float half_len_w = 0.5f * s_world;
                const ImVec2 a = world_to_logical(
                    pca.mu.x - pca.axis_u.x * half_len_w,
                    pca.mu.y - pca.axis_u.y * half_len_w);
                const ImVec2 b = world_to_logical(
                    pca.mu.x + pca.axis_u.x * half_len_w,
                    pca.mu.y + pca.axis_u.y * half_len_w);
                dl->AddLine(a, b, IM_COL32(255, 255, 255, 130), 1.0f);
            }
        }
    }

    // 2b. 选中 tile 高亮
    if (show_selected_) {
        for (int r = 0; r < algo::kRouteCount; ++r) {
            const int idx = snapshot_.selected_tile_index[r];
            if (idx < 0 || idx >= static_cast<int>(snapshot_.tile_scores.size())) continue;
            const int cx = idx % snapshot_.n_x;
            const int cy = idx / snapshot_.n_x;
            const ImVec2 mn = world_to_logical(cx * s_world, cy * s_world);
            const ImVec2 mx{ mn.x + tile_size_logical.x, mn.y + tile_size_logical.y };
            dl->AddRect(mn, mx, IM_COL32(255, 220, 60, 220), 2.0f, 0, 2.0f);
        }
    }
}

}  // namespace routes_label::debug
