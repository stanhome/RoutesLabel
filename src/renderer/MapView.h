#pragma once
//
// renderer/MapView.h
// Map-world 2D 空间 ↔ framebuffer / logical / clip 空间的"无状态"坐标变换 helper。
//
// 设计动机（详见 doc/map-world-space.md）：
//   * RouteScene 持有的 polyline 顶点是 **map-world px**（与 MapContext::mapViewRect 同坐标系），
//     原点左上、y 朝下、单位"地图像素"，与窗口尺寸无关。
//   * 渲染时，每帧根据 framebuffer extent (fb_w, fb_h) 计算一个 contain 模式的 fit-to-window
//     变换：保持长宽比，长边贴 fb 边、短边居中 letterbox。
//   * 与正交投影合成得到 world → clip MVP，写入 UBO；vertex shader 直接乘以 world-space `a_pos`。
//   * 算法侧（GridCpu / LabelWidget / GridDebugWidget）也统一在 world 空间下计算与输出，
//     debug 可视化时再用 world_to_logical 上屏。
//
// 这个结构体是"每帧一次性算出，多处读取"的纯数据；不持有任何 GPU 资源，可随意拷贝。
//

#include <glm/glm.hpp>

#include <cstdint>

namespace routes_label::renderer {

struct MapView {
    // ----- shader UBO 用 -----
    glm::mat4 world_to_clip = glm::mat4(1.0f);

    // ----- 坐标变换基础量 -----
    float     scale = 1.0f;             // world px → fb px 的均匀缩放（contain 模式：min(fb_w/world_w, fb_h/world_h)）
    glm::vec2 offset_fb = glm::vec2(0); // letterbox 居中偏移（fb px）
    glm::vec2 world_origin = glm::vec2(0); // mapViewRect.{x,y}

    // ----- fb ↔ logical（来自 ImGuiIO.DisplayFramebufferScale 的倒数）-----
    float     fb_to_logical_x = 1.0f;
    float     fb_to_logical_y = 1.0f;

    // 派生：framebuffer 尺寸（用于调试 / 日志）
    uint32_t  fb_w = 0;
    uint32_t  fb_h = 0;

    // -----------------------------------------------------------------
    // 构造（纯函数）：根据 mapViewRect、fb extent、ImGui DisplayFramebufferScale 计算所有派生量
    // -----------------------------------------------------------------
    static MapView compute(float    map_x,
                           float    map_y,
                           float    map_w,
                           float    map_h,
                           uint32_t fb_w,
                           uint32_t fb_h,
                           float    fb_scale_x,
                           float    fb_scale_y);

    // -----------------------------------------------------------------
    // 内联坐标转换
    // -----------------------------------------------------------------
    [[nodiscard]] glm::vec2 world_to_fb(glm::vec2 w) const {
        return offset_fb + (w - world_origin) * scale;
    }
    [[nodiscard]] glm::vec2 fb_to_world(glm::vec2 f) const {
        const float inv = (scale > 1e-6f) ? (1.0f / scale) : 0.0f;
        return world_origin + (f - offset_fb) * inv;
    }
    [[nodiscard]] glm::vec2 fb_to_logical(glm::vec2 f) const {
        return { f.x * fb_to_logical_x, f.y * fb_to_logical_y };
    }
    [[nodiscard]] glm::vec2 logical_to_fb(glm::vec2 l) const {
        const float inv_x = (fb_to_logical_x > 1e-6f) ? (1.0f / fb_to_logical_x) : 1.0f;
        const float inv_y = (fb_to_logical_y > 1e-6f) ? (1.0f / fb_to_logical_y) : 1.0f;
        return { l.x * inv_x, l.y * inv_y };
    }
    [[nodiscard]] glm::vec2 world_to_logical(glm::vec2 w) const {
        return fb_to_logical(world_to_fb(w));
    }
    [[nodiscard]] glm::vec2 logical_to_world(glm::vec2 l) const {
        return fb_to_world(logical_to_fb(l));
    }

    // 标量 world→logical 的均匀 scale（适用于"label 尺寸随地图缩放"等场景）。
    // contain 模式下 x/y scale 同步，但 fb→logical 在某些 dpi 配置下 x/y 略有差异，
    // 这里取均值保持视觉对称。
    [[nodiscard]] float world_to_logical_scale_uniform() const {
        return scale * 0.5f * (fb_to_logical_x + fb_to_logical_y);
    }
};

}  // namespace routes_label::renderer
