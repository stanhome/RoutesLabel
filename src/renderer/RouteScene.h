#pragma once
//
// renderer/RouteScene.h
// 渲染层场景对象：持有一份 core::RouteSceneData，并把 polyline 展成 GPU 可用的 ribbon mesh。
//
// Ribbon 展开（CPU 端）：每条折线 N 个顶点 → 2N 个顶点（沿法线左右各 ±halfWidth）。
// 多条折线共享 vertex/index buffer，用 0xFFFFFFFF primitive restart 哨兵串接，
// 一次 vkCmdDrawIndexed 渲染所有 ribbon。
//

#include "core/Types.h"

#include <cstdint>
#include <vector>

namespace routes_label::renderer {

// GPU 顶点格式：屏幕像素坐标 + RGB 颜色（每条折线整体均匀色）。
struct RibbonVertex {
    float pos[2];
    float color[3];
};

constexpr uint32_t kPrimitiveRestartIndex = 0xFFFFFFFFu;

struct RibbonMesh {
    std::vector<RibbonVertex> vertices;
    std::vector<uint32_t>     indices;
};

class RouteScene {
public:
    explicit RouteScene(core::RouteSceneData data);

    const core::RouteSceneData& data() const { return data_; }

    // 把所有 routes 的 polyline 展开为单一 triangle-strip ribbon mesh。
    // line_width_px 是 ribbon 的总宽度（左右各 halfWidth）。
    RibbonMesh build_ribbon_mesh(float line_width_px) const;

private:
    core::RouteSceneData data_;
};

}  // namespace routes_label::renderer
