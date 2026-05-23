//
// debug/LabelWidget.cpp
//
// Map label 绘制：用 ImGui ImDrawList 在前景图层直接画 3 个有设计感的"app 风格" label：
//   - 整体尺寸 400×100 px（**map-world px**，与 algo::GridParams 的 label_w/h 一致），
//     通过 MapView::world_to_logical 上屏，随窗口缩放（doc/map-world-space.md）
//   - 左侧彩色徽章（路径色 + 大字号 A/B/C），右侧白色信息卡（路径名 + 通行时间双行）
//   - 圆角 + 阴影 + 白色细边，map pin 风格 anchor，路径色 leader line
//

#include "debug/LabelWidget.h"

#include "algo/GridCommon.h"

#include <imgui.h>

#include <cmath>
#include <cstdio>

namespace routes_label::debug {

namespace {

// label 整体尺寸（**map-world px**，与 algo::GridParams 默认 label_w/label_h 严格一致；
// 算法侧 §3.2 的 grid-AABB 校验也用同样的尺寸做 C2 相交校验，必须保持同步）。
// world px 单位 → 通过 MapView::world_to_logical_scale_uniform() 上屏，随窗口缩放
// 自动缩放，与 ribbon、grid 视觉比例稳定（doc/map-world-space.md）。
constexpr float kLabelW = 400.0f;
constexpr float kLabelH = 100.0f;

// 内部布局参数（map-world px）
constexpr float kBadgeW       = 90.0f;   // 左侧彩色徽章宽度
constexpr float kCornerRadius = 18.0f;   // label 整体圆角（更柔和）
constexpr float kShadowExpand = 6.0f;    // 阴影向外扩展像素
constexpr float kBorderPx     = 2.0f;    // 外描边
constexpr float kBadgeCircleR = 28.0f;   // 徽章内部白色圆形半径

// Anchor pin 几何（framebuffer 像素，整体放大与 label 比例匹配）
constexpr float kPinOuter     = 14.0f;
constexpr float kPinMid       = 10.5f;
constexpr float kPinInner     = 7.0f;

constexpr float kLeaderWidth  = 3.5f;    // leader line 宽度（framebuffer 像素）

ImU32 rgb_to_u32(const std::array<float, 3>& rgb, float alpha = 1.0f) {
    return IM_COL32(
        static_cast<int>(std::clamp(rgb[0], 0.0f, 1.0f) * 255.0f + 0.5f),
        static_cast<int>(std::clamp(rgb[1], 0.0f, 1.0f) * 255.0f + 0.5f),
        static_cast<int>(std::clamp(rgb[2], 0.0f, 1.0f) * 255.0f + 0.5f),
        static_cast<int>(std::clamp(alpha,  0.0f, 1.0f) * 255.0f + 0.5f));
}

// 让路径色更深（用于徽章里大字母的描边等）：HSV 上把 V 拉低 30%
std::array<float, 3> darken(const std::array<float, 3>& rgb, float amount = 0.65f) {
    return { rgb[0] * amount, rgb[1] * amount, rgb[2] * amount };
}

}  // namespace

LabelWidget::LabelWidget() = default;

void LabelWidget::render() {
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    if (!dl) return;
    if (!have_map_view_) return;   // RoutesRenderer 必须每帧 SetMapView

    // 算法侧 anchor / label_center 是 **world (map) px**，与 polyline 同坐标系；
    // 通过 MapView 的 world→logical 复合变换上屏（contain fit-to-window + fb→logical）。
    // label 几何尺寸（kLabelW / kLabelH）也定义在 world px：随地图整体缩放，
    // 视觉上与 ribbon、grid 保持比例稳定（doc/map-world-space.md "Decision: label 尺寸沿用 world px"）。
    auto world_to_logical = [&](float wx, float wy) {
        const glm::vec2 l = map_view_.world_to_logical({ wx, wy });
        return ImVec2(l.x, l.y);
    };
    // world → logical 的均匀 scale（用于把 label 几何长度上屏）
    const float to_logical_uniform = map_view_.world_to_logical_scale_uniform();

    const int   travel[algo::kRouteCount]     = { travel_.minutes_a, travel_.minutes_b, travel_.minutes_c };
    const char  route_char[algo::kRouteCount] = { 'A', 'B', 'C' };
    const char* route_name[algo::kRouteCount] = { "Route A", "Route B", "Route C" };

    // label 尺寸（logical 像素）
    const float w_l       = kLabelW * to_logical_uniform;
    const float h_l       = kLabelH * to_logical_uniform;
    const float badge_w_l = kBadgeW * to_logical_uniform;
    const float radius_l  = kCornerRadius * to_logical_uniform;
    const float shadow_l  = kShadowExpand * to_logical_uniform;
    const float border_l  = kBorderPx * to_logical_uniform;

    for (int i = 0; i < algo::kRouteCount; ++i) {
        const auto& r = results_[i];
        if (r.status == algo::LabelStatus::Infeasible) continue;

        const ImVec2 anchor_l = world_to_logical(r.anchor.x, r.anchor.y);
        const ImVec2 center_l = world_to_logical(r.label_center.x, r.label_center.y);

        const ImVec2 rect_min{ center_l.x - 0.5f * w_l, center_l.y - 0.5f * h_l };
        const ImVec2 rect_max{ center_l.x + 0.5f * w_l, center_l.y + 0.5f * h_l };

        const float fallback_alpha = (r.status == algo::LabelStatus::FallbackUsed) ? 0.85f : 1.0f;

        const auto& rgb = colors_[i];
        const auto  rgb_dark = darken(rgb, 0.55f);

        // ============================================================
        // 0. leader line（先画，让 label 矩形盖在上面）
        // ============================================================
        // 渐变效果：用两段拼接（路径色实心 → 白色尖端）模拟
        {
            const ImVec2 dir{ center_l.x - anchor_l.x, center_l.y - anchor_l.y };
            const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
            if (len > 1e-3f) {
                const ImVec2 unit{ dir.x / len, dir.y / len };
                // leader 真正终点：label 矩形边界上（不戳进 label 内部）
                const float half_w = 0.5f * w_l;
                const float half_h = 0.5f * h_l;
                // 把 unit 缩放到矩形边界 —— 取 x、y 限制中的较小者
                const float t_x = (std::abs(unit.x) > 1e-6f) ? half_w / std::abs(unit.x) : 1e9f;
                const float t_y = (std::abs(unit.y) > 1e-6f) ? half_h / std::abs(unit.y) : 1e9f;
                const float t_edge = std::min(t_x, t_y);
                const ImVec2 end_at_rect{
                    center_l.x - unit.x * t_edge,
                    center_l.y - unit.y * t_edge,
                };
                const ImU32 leader_col = rgb_to_u32(rgb, 0.85f * fallback_alpha);
                dl->AddLine(anchor_l, end_at_rect, leader_col,
                            kLeaderWidth * to_logical_uniform);
            }
        }

        // ============================================================
        // 1. label 阴影：多层半透明黑色，向下偏移制造层次感
        // ============================================================
        for (int s = 0; s < 3; ++s) {
            const float expand = shadow_l * (1.0f + s * 0.4f);
            const float offset_y = shadow_l * 0.3f * (s + 1);
            dl->AddRectFilled(
                { rect_min.x - expand, rect_min.y - expand + offset_y },
                { rect_max.x + expand, rect_max.y + expand + offset_y },
                IM_COL32(0, 0, 0, 24),
                radius_l + expand);
        }

        // ============================================================
        // 2. label 背景：左侧路径色徽章 + 右侧白色信息区
        //    用整体白色填充打底 + 左侧再叠加路径色（用 PathStroke 自定义圆角更复杂，
        //    这里用两次 AddRectFilled 拼接：完整白底 + 左侧带左圆角的彩色块。
        //    ImDrawList 的 AddRectFilled 支持指定 rounding_corners flag，
        //    完美实现"只左两角圆"或"只右两角圆"。）
        // ============================================================
        const ImU32 col_bg_white = IM_COL32(255, 255, 255,
                                            static_cast<int>(252 * fallback_alpha));
        const ImU32 col_badge    = rgb_to_u32(rgb, fallback_alpha);

        // 白色整体背景（先画，4 角圆）
        dl->AddRectFilled(rect_min, rect_max, col_bg_white,
                          radius_l, ImDrawFlags_RoundCornersAll);

        // 左侧彩色徽章（只左两角圆）
        const ImVec2 badge_min = rect_min;
        const ImVec2 badge_max{ rect_min.x + badge_w_l, rect_max.y };
        dl->AddRectFilled(badge_min, badge_max, col_badge, radius_l,
                          ImDrawFlags_RoundCornersLeft);

        // 徽章中圆形装饰 + 大字母（pin/map app 风格的视觉锚点）
        const ImVec2 badge_center{
            0.5f * (badge_min.x + badge_max.x),
            0.5f * (badge_min.y + badge_max.y),
        };
        const float circle_r_l = kBadgeCircleR * to_logical_uniform;
        // 内圆：白色半透明背景
        dl->AddCircleFilled(badge_center, circle_r_l,
                            IM_COL32(255, 255, 255, static_cast<int>(72 * fallback_alpha)));
        // 大字母 A/B/C：徽章内大字号，深色
        const float base_font_size = ImGui::GetFontSize();   // 1.92+ 兼容：不再用 ImFont::FontSize
        {
            char single[2] = { route_char[i], '\0' };
            ImFont* font = ImGui::GetFont();
            const float font_size = base_font_size * 2.4f;
            const ImVec2 ts = font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, single);
            const ImVec2 tp{
                badge_center.x - 0.5f * ts.x,
                badge_center.y - 0.5f * ts.y,
            };
            dl->AddText(font, font_size, tp,
                        rgb_to_u32(rgb_dark, fallback_alpha), single);
        }

        // ============================================================
        // 3. 右侧信息区：两行文字
        //    第一行：路径名 "Route A"（灰色小字号）
        //    第二行：通行时间 "35 min"（深色大字号粗体感觉）
        // ============================================================
        {
            const float text_pad_x = 22.0f * to_logical_uniform;
            const float info_x0    = badge_max.x + text_pad_x;
            ImFont* font           = ImGui::GetFont();

            // 第一行（路径名）
            const float name_size = base_font_size * 1.15f;
            const ImVec2 name_pos{
                info_x0,
                center_l.y - 0.5f * h_l + 18.0f * to_logical_uniform,
            };
            const ImU32 col_name = IM_COL32(110, 115, 130,
                                            static_cast<int>(245 * fallback_alpha));
            dl->AddText(font, name_size, name_pos, col_name, route_name[i]);

            // 第二行（通行时间）
            char time_buf[16];
            std::snprintf(time_buf, sizeof(time_buf), "%d min", travel[i]);
            const float time_size = base_font_size * 2.1f;
            const ImVec2 time_pos{
                info_x0,
                center_l.y - 0.5f * h_l + 42.0f * to_logical_uniform,
            };
            // 数字主色：路径深色（让 label 自带"配色一致性"）
            dl->AddText(font, time_size, time_pos,
                        rgb_to_u32(rgb_dark, fallback_alpha),
                        time_buf);
        }

        // ============================================================
        // 4. 外描边（在所有内容之上画一圈细白边，制造"卡片"质感）
        // ============================================================
        dl->AddRect(rect_min, rect_max,
                    IM_COL32(0, 0, 0, static_cast<int>(35 * fallback_alpha)),
                    radius_l, ImDrawFlags_RoundCornersAll, border_l);

        // ============================================================
        // 5. Anchor pin：地图大头针风格（外白 + 中深色 + 内彩）
        // ============================================================
        {
            const float r_outer = kPinOuter * to_logical_uniform;
            const float r_mid   = kPinMid   * to_logical_uniform;
            const float r_inner = kPinInner * to_logical_uniform;
            // 外白光晕
            dl->AddCircleFilled(anchor_l, r_outer,
                                IM_COL32(255, 255, 255, 230));
            // 中描边（深路径色，让 anchor 更醒目）
            dl->AddCircleFilled(anchor_l, r_mid,
                                rgb_to_u32(rgb_dark, fallback_alpha));
            // 内彩色实心
            dl->AddCircleFilled(anchor_l, r_inner,
                                rgb_to_u32(rgb, fallback_alpha));
        }
    }
}

}  // namespace routes_label::debug
