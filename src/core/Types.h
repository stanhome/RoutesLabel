#pragma once
//
// core/Types.h
// 项目核心数据结构（按 doc/routes-select.md §1.1 原文落地，保留 double 精度）。
// 屏幕像素坐标系，原点在左上角，x 朝右，y 朝下。
//
// 注意：渲染端会把 double 转成 float 再上传 GPU；算法侧（独占段提取 / 候选生成 /
// 全局布局）一律使用 double，避免几何运算（特别是 Buffer 差集与距离比较）的精度抖动。
//

#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace routes_label::core {

// -----------------------------------------------------------------------------
// 基础几何类型
// -----------------------------------------------------------------------------
struct Point {
    double x = 0.0;
    double y = 0.0;

    // 渲染端便捷转换：用于直接写入 VkVertex 的 vec2 字段。
    [[nodiscard]] std::array<float, 2> toFloat() const {
        return { static_cast<float>(x), static_cast<float>(y) };
    }

    [[nodiscard]] double distanceTo(const Point& other) const {
        const double dx = x - other.x;
        const double dy = y - other.y;
        return std::sqrt(dx * dx + dy * dy);
    }
};

struct Size {
    double w = 0.0;
    double h = 0.0;

    [[nodiscard]] bool empty() const { return w <= 0.0 || h <= 0.0; }

    // 对角线长度，对应 routes-select.md §1.1 中 τ = ½·diag(L) + margin 的公式。
    [[nodiscard]] double diagonal() const { return std::sqrt(w * w + h * h); }
};

struct Rect {
    double x = 0.0;
    double y = 0.0;
    double w = 0.0;
    double h = 0.0;

    [[nodiscard]] bool   empty() const     { return w <= 0.0 || h <= 0.0; }
    [[nodiscard]] double left()  const     { return x; }
    [[nodiscard]] double top()   const     { return y; }
    [[nodiscard]] double right() const     { return x + w; }
    [[nodiscard]] double bottom() const    { return y + h; }
    [[nodiscard]] Point  center() const    { return { x + w * 0.5, y + h * 0.5 }; }
    [[nodiscard]] Size   size() const      { return { w, h }; }

    [[nodiscard]] bool contains(const Point& p) const {
        return p.x >= left() && p.x <= right() && p.y >= top() && p.y <= bottom();
    }

    // AABB 相交判定（边界接触视为不相交，便于 C3 互斥语义）。
    [[nodiscard]] bool intersects(const Rect& other) const {
        if (empty() || other.empty()) return false;
        return !(right()  <= other.left()
              || other.right() <= left()
              || bottom() <= other.top()
              || other.bottom() <= top());
    }
};

// -----------------------------------------------------------------------------
// 业务输入类型
// -----------------------------------------------------------------------------
struct RouteInput {
    int                routeId = 0;
    std::vector<Point> polyline;   // Route polyline (already projected to screen)
};

struct LabelInput {
    int   routeId = 0;
    Point anchor;                  // Screen position of the arrow tip
    Size  size;                    // Label width and height in pixels
};

enum class LayoutHint {
    DownRight,   // Label below-right of anchor; arrow at top-left of label
    DownLeft,
    TopRight,
    TopLeft,
};

struct MapContext {
    Rect              mapViewRect;       // Visible map area (labels should stay inside when possible)
    Point             mapCenter;         // Typically the center of mapViewRect
    Rect              carMarkerRect;     // Vehicle icon; may be empty (w = 0)
    std::vector<Rect> obstructingViews;  // Other views overlapping the map (panels, buttons, etc.)
};

// -----------------------------------------------------------------------------
// 渲染端扩展：为 demo 场景额外携带每条路径的颜色（不在用户给定的 RouteInput 里）。
// -----------------------------------------------------------------------------
struct RouteVisualStyle {
    int   routeId = 0;
    float color[3] = { 1.0f, 1.0f, 1.0f };  // RGB ∈ [0,1]
};

// 一份完整的"地图场景"输入：业务硬数据（routes + map context）+ demo 渲染样式。
// 由 utils::load_route_scene_from_json() 构造，由 renderer::RouteScene 消费。
struct RouteSceneData {
    MapContext                    map_context;
    std::vector<RouteInput>       routes;
    std::vector<RouteVisualStyle> styles;   // 与 routes 一一对应（按 index 配对）
};

}  // namespace routes_label::core
