//
// debug/GridDebugWidget.cpp
//

#include "debug/GridDebugWidget.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>

namespace routes_label::debug {

namespace {

// score → HSV→RGB（粗糙）：低分蓝、中分绿、高分红
ImU32 score_to_color(float s, float alpha) {
    s = std::clamp(s, 0.0f, 1.0f);
    // 直接 lerp on RGB （快糙好）
    const float r = s;
    const float g = 1.0f - std::abs(s - 0.5f) * 2.0f;
    const float b = 1.0f - s;
    return IM_COL32(
        static_cast<int>(r * 255.0f),
        static_cast<int>(g * 255.0f),
        static_cast<int>(b * 255.0f),
        static_cast<int>(alpha * 255.0f));
}

}  // namespace

void GridDebugWidget::render() {
    // ---- 1. 控制面板（屏幕右上角）----
    ImGuiIO& io = ImGui::GetIO();
    const float pad = 12.0f;
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - pad, pad),
                            ImGuiCond_Once, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(320.0f, 0.0f), ImGuiCond_Once);
    ImGui::SetNextWindowBgAlpha(0.85f);

    constexpr ImGuiWindowFlags kFlags =
        ImGuiWindowFlags_NoResize        |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_AlwaysAutoResize;

    PanelRectLogical pr{};
    if (ImGui::Begin("Routes Grid Algorithm", nullptr, kFlags)) {
        ImGui::Text("Compute time: %.3f ms (CPU)", last_compute_ms_);
        ImGui::Text("Grid: %d x %d  tile=%.1f px",
                    snapshot_.n_x, snapshot_.n_y, snapshot_.tile_size);

        ImGui::Separator();
        ImGui::TextDisabled("Grid parameters");
        if (ImGui::SliderInt("n_grid", &params_.n_grid, 10, 80)) dirty_ = true;
        if (ImGui::SliderFloat("sep_thr", &params_.sep_threshold, 0.1f, 0.95f, "%.2f")) dirty_ = true;
        if (ImGui::SliderFloat("arclen_bal", &params_.arclength_balance, 0.0f, 0.6f, "%.2f")) dirty_ = true;
        if (ImGui::SliderInt("nms_dist", &params_.nms_grid_distance, 1, 5)) dirty_ = true;

        ImGui::Separator();
        ImGui::TextDisabled("Visualization");
        ImGui::Checkbox("Tile heatmap",      &show_heatmap_);
        ImGui::Checkbox("PCA principal axis", &show_pca_axis_);
        ImGui::Checkbox("Selected tile",     &show_selected_);

        if (have_snapshot_) {
            ImGui::Separator();
            int feasible = 0, total = 0;
            float max_score = 0.0f;
            for (const auto& ts : snapshot_.tile_scores) {
                ++total;
                if (ts.feasible) {
                    ++feasible;
                    if (ts.score > max_score) max_score = ts.score;
                }
            }
            ImGui::Text("Feasible tiles: %d / %d", feasible, total);
            ImGui::Text("Max score: %.3f", max_score);
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
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    if (!dl) return;

    const float fb_scale_x = (io.DisplayFramebufferScale.x > 0.0f) ? io.DisplayFramebufferScale.x : 1.0f;
    const float fb_scale_y = (io.DisplayFramebufferScale.y > 0.0f) ? io.DisplayFramebufferScale.y : 1.0f;
    const float to_logical_x = 1.0f / fb_scale_x;
    const float to_logical_y = 1.0f / fb_scale_y;
    auto fb_to_logical = [&](float fx, float fy) {
        return ImVec2(fx * to_logical_x, fy * to_logical_y);
    };

    const float s_fb = snapshot_.tile_size;
    const ImVec2 tile_size_logical{ s_fb * to_logical_x, s_fb * to_logical_y };

    // 2a. 热图
    if (show_heatmap_) {
        for (int cy = 0; cy < snapshot_.n_y; ++cy) {
            for (int cx = 0; cx < snapshot_.n_x; ++cx) {
                const int idx = cy * snapshot_.n_x + cx;
                if (idx >= static_cast<int>(snapshot_.tile_scores.size())) continue;
                const auto& ts = snapshot_.tile_scores[idx];
                if (ts.score <= 0.0f) continue;
                const ImVec2 mn = fb_to_logical(cx * s_fb, cy * s_fb);
                const ImVec2 mx{ mn.x + tile_size_logical.x, mn.y + tile_size_logical.y };
                dl->AddRectFilled(mn, mx, score_to_color(ts.score, 0.30f));
            }
        }
    }

    // 2b. PCA 主轴
    if (show_pca_axis_) {
        for (int cy = 0; cy < snapshot_.n_y; ++cy) {
            for (int cx = 0; cx < snapshot_.n_x; ++cx) {
                const int idx = cy * snapshot_.n_x + cx;
                if (idx >= static_cast<int>(snapshot_.tile_pca.size())) continue;
                const auto& pca = snapshot_.tile_pca[idx];
                if (!pca.valid) continue;
                const float half_len_fb = 0.5f * s_fb;
                const ImVec2 a = fb_to_logical(
                    pca.mu.x - pca.axis_u.x * half_len_fb,
                    pca.mu.y - pca.axis_u.y * half_len_fb);
                const ImVec2 b = fb_to_logical(
                    pca.mu.x + pca.axis_u.x * half_len_fb,
                    pca.mu.y + pca.axis_u.y * half_len_fb);
                dl->AddLine(a, b, IM_COL32(255, 255, 255, 130), 1.0f);
            }
        }
    }

    // 2c. 选中 tile 高亮
    if (show_selected_) {
        for (int r = 0; r < algo::kRouteCount; ++r) {
            const int idx = snapshot_.selected_tile_index[r];
            if (idx < 0 || idx >= static_cast<int>(snapshot_.tile_scores.size())) continue;
            const int cx = idx % snapshot_.n_x;
            const int cy = idx / snapshot_.n_x;
            const ImVec2 mn = fb_to_logical(cx * s_fb, cy * s_fb);
            const ImVec2 mx{ mn.x + tile_size_logical.x, mn.y + tile_size_logical.y };
            dl->AddRect(mn, mx, IM_COL32(255, 220, 60, 220), 2.0f, 0, 2.0f);
        }
    }
}

}  // namespace routes_label::debug
