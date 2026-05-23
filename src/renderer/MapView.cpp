//
// renderer/MapView.cpp
//
// contain 模式 fit-to-window 的 world → fb → clip 矩阵推导（每帧 O(1)）。
//

#include "renderer/MapView.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>

namespace routes_label::renderer {

MapView MapView::compute(float    map_x,
                         float    map_y,
                         float    map_w,
                         float    map_h,
                         uint32_t fb_w,
                         uint32_t fb_h,
                         float    fb_scale_x,
                         float    fb_scale_y) {
    MapView mv;
    mv.fb_w = fb_w;
    mv.fb_h = fb_h;
    mv.world_origin = glm::vec2(map_x, map_y);

    const float fbf_w = static_cast<float>(fb_w);
    const float fbf_h = static_cast<float>(fb_h);

    // 退化保护
    const float w = std::max(map_w, 1.0f);
    const float h = std::max(map_h, 1.0f);

    // contain：保持长宽比，取 min。长边贴 fb 边，短边居中 letterbox。
    const float sx = (fbf_w > 0.0f) ? (fbf_w / w) : 1.0f;
    const float sy = (fbf_h > 0.0f) ? (fbf_h / h) : 1.0f;
    mv.scale = std::min(sx, sy);
    if (mv.scale <= 0.0f) mv.scale = 1.0f;

    mv.offset_fb = glm::vec2(
        (fbf_w - w * mv.scale) * 0.5f,
        (fbf_h - h * mv.scale) * 0.5f);

    // fb→logical scale（来自 ImGui DisplayFramebufferScale 的倒数）
    mv.fb_to_logical_x = (fb_scale_x > 1e-6f) ? (1.0f / fb_scale_x) : 1.0f;
    mv.fb_to_logical_y = (fb_scale_y > 1e-6f) ? (1.0f / fb_scale_y) : 1.0f;

    // ---- 合成 world → clip MVP ----
    // 步骤：world_pos → world - origin → 乘 scale → 加 offset → 得到 fb px → ortho → clip
    // 矩阵顺序（列优先 glm，先应用最右侧）：
    //   M_fb   = T(offset_fb) * S(scale) * T(-world_origin)
    //   M_clip = ortho(0, fb_w, 0, fb_h, -1, 1) * M_fb
    glm::mat4 m = glm::mat4(1.0f);
    m = glm::translate(m, glm::vec3(mv.offset_fb, 0.0f));
    m = glm::scale(m, glm::vec3(mv.scale, mv.scale, 1.0f));
    m = glm::translate(m, glm::vec3(-mv.world_origin, 0.0f));

    const glm::mat4 ortho = glm::ortho(0.0f, fbf_w, 0.0f, fbf_h, -1.0f, 1.0f);
    mv.world_to_clip = ortho * m;
    return mv;
}

}  // namespace routes_label::renderer
