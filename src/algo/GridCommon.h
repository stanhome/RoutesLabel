#pragma once
//
// algo/GridCommon.h
// Routes-Select Grid GPU/CPU 算法共用数据结构。
//
// 对应文档：doc/routes-select-grid-gpu.md（GPU 工程落地版）
//          + doc/routes-select-grid-variance.md（数学定义版）
//
// 设计要点：
//   - 双 payload Grid：cells_subseg 用于 §2 PCA 评分；cells_aabb 用于 §3 label 摆放校验。
//   - 算法侧统一用 float（fp32）：1080p 像素坐标，协方差量级 ~ 4e6，距 fp32 mantissa 上限
//     还有一个量级裕度，无需 Kahan 补偿（grid-gpu 文档 §4.2 已论证）。
//   - 与 GPU SSBO 等价：所有结构都对齐到 std430 友好的偏移（vec2 = 8B、float = 4B、uint = 4B）。
//
// 命名空间：routes_label::algo
//

#include <array>
#include <cstdint>
#include <vector>

namespace routes_label::algo {

// -----------------------------------------------------------------------------
// 基础类型
// -----------------------------------------------------------------------------

struct Vec2f {
    float x = 0.0f;
    float y = 0.0f;
};

struct AABBf {
    Vec2f mn{};   // min(x, y)
    Vec2f mx{};   // max(x, y)

    [[nodiscard]] bool empty() const {
        return mx.x <= mn.x || mx.y <= mn.y;
    }
    // 边界接触视为不相交，便于 C3 互斥语义。
    [[nodiscard]] bool intersects(const AABBf& o) const {
        if (empty() || o.empty()) return false;
        return !(mx.x <= o.mn.x || o.mx.x <= mn.x
              || mx.y <= o.mn.y || o.mx.y <= mn.y);
    }
};

// -----------------------------------------------------------------------------
// Grid 输入：三路径屏幕像素 polyline
// -----------------------------------------------------------------------------

constexpr int kRouteCount = 3;   // 本算法专为 3 路径独占段问题设计

struct Polylines {
    std::array<std::vector<Vec2f>, kRouteCount> routes;   // 每条路径的屏幕像素 polyline
};

// -----------------------------------------------------------------------------
// 算法参数（与 doc §3.3 / §10.0 一致）
// -----------------------------------------------------------------------------

struct GridParams {
    // ---- 屏幕与 grid ----
    // screen_w/screen_h 语义 = **map-world 空间尺寸**（即 MapContext::mapViewRect 的 w/h），
    // 与 framebuffer 像素 / window 像素**解耦**。窗口缩放只触发 view-projection 重算，
    // 不会触发本算法重算（doc/map-world-space.md "screen_w/h semantics"）。
    float screen_w = 0.0f;
    float screen_h = 0.0f;
    int   n_grid   = 40;           // 短边切几格（doc §2.2 默认 40）

    // ---- label 几何（doc §3.3，加大版以呈现 app 风格 UI；LabelWidget 使用同一尺寸）----
    // 单位 = map-world 像素（与 polyline / panel_rect / label anchor 一致）。
    // 在屏上的视觉尺寸 = label_w/h × MapView::world_to_logical scale，随地图缩放。
    float label_w   = 400.0f;      // px
    float label_h   = 100.0f;      // px
    float leader_len = 36.0f;      // px

    // ---- 评分阈值（doc §2.7 / §2.8）----
    float separationThreshold = 0.5f;     // separation < 0.5 的 tile 视为未独占
    float arclength_balance   = 0.2f;     // min/max 弧长比下限（防"无人区"）
    int   nms_grid_distance   = 2;        // 选中 tile 互相至少隔 1 格
    float anisotropy_min      = 4.0f;     // λ1/λ2 最小值（防各向同性 PCA）

    // ---- UI obstructing rect（doc/routes-select.md §1.1 MapContext.obstructingViews 概念）----
    // 调试面板等"压在地图上的浮窗"作为 label 摆放的硬约束；label AABB 不应与该 rect 相交。
    // 单位：**map-world 像素**（与 polyline 同坐标系）。由 RoutesRenderer 通过
    // MapView::logical_to_world 把 ImGui 面板的 logical rect 转换后注入。
    // 空 rect（panel_w/h <= 0）表示无约束。
    AABBf panel_rect{};   // 当前 mn=mx=0 表示空
};

// -----------------------------------------------------------------------------
// Grid Cell payload 1：tile-clipped sub_seg（PCA 用，doc §2.3-2.5）
// -----------------------------------------------------------------------------

struct SubSeg {
    Vec2f start{};
    Vec2f end{};
    float arclength = 0.0f;       // = ‖end - start‖
    int   route_id  = 0;          // 0..2
};

// -----------------------------------------------------------------------------
// Grid Cell payload 2：完整线段 AABB（label 摆放校验用，doc §2.1）
// -----------------------------------------------------------------------------

struct SegAABB {
    AABBf bb{};                  // 完整线段（不是 tile-clipped）AABB
    Vec2f seg_a{};               // 用于 label 摆放阶段做 SAT 精确校验
    Vec2f seg_b{};
    int   route_id  = 0;
};

// -----------------------------------------------------------------------------
// PCA 中间结果（doc §2.4）：每 tile 一份
// -----------------------------------------------------------------------------

struct TilePca {
    Vec2f mu{};                  // 加权重心
    Vec2f axis_u{};              // 主轴（单位向量）
    Vec2f axis_v{};              // 次轴（单位向量，= rot90(u)）
    float lambda1 = 0.0f;
    float lambda2 = 0.0f;
    float arclength[kRouteCount] = { 0.0f, 0.0f, 0.0f };   // 每路径在 tile 内的总弧长
    bool  valid   = false;       // false 表示退化（覆盖路径 < 3 / 各向同性）
};

// 三路径在主轴 u 上的 1D 投影区间（doc §2.5）
struct ProjInterval {
    float t_min = 0.0f;
    float t_max = 0.0f;
    bool  valid = false;
};

// -----------------------------------------------------------------------------
// Tile 评分（doc §2.7）
// -----------------------------------------------------------------------------

struct TileScore {
    float separationScore = 0.0f;
    float density         = 0.0f;
    float score           = 0.0f;
    bool  feasible        = false;
    ProjInterval intervals[kRouteCount];
};

// -----------------------------------------------------------------------------
// 最终输出：每条路径一个 label 摆放结果（doc §6.2）
// -----------------------------------------------------------------------------

enum class LabelStatus : uint32_t {
    Ok            = 0,
    FallbackUsed  = 1,
    Infeasible    = 2,   // 没找到任何不与他路相交的 slot
    PoolOverflow  = 3,   // GPU 路径专用
};

enum class LabelSlot : uint32_t {
    N = 0, NE = 1, E = 2, SE = 3, S = 4, SW = 5, W = 6, NW = 7,
};

struct LabelResult {
    Vec2f       anchor{};                            // 路径上的锚点
    Vec2f       label_center{};                      // label 矩形中心
    int         route_id     = 0;                    // 0=A, 1=B, 2=C
    LabelSlot   slot         = LabelSlot::E;
    LabelStatus status       = LabelStatus::Infeasible;
};

// -----------------------------------------------------------------------------
// 调试快照（debug overlay 用：tile 热图 / PCA 主轴 / 选中 tile）
// -----------------------------------------------------------------------------

struct GridDebugSnapshot {
    GridParams params{};
    int n_x = 0;
    int n_y = 0;
    float tile_size = 0.0f;            // 单位：**map-world 像素**（与 polyline 一致）
    std::vector<TileScore> tile_scores;   // size = n_x * n_y
    std::vector<TilePca>   tile_pca;      // size = n_x * n_y, mu / axis_u 单位 = world px
    std::array<int, kRouteCount> selected_tile_index = { -1, -1, -1 };  // 选中 tile 在 grid 中的 index（-1 = 无）
};

// -----------------------------------------------------------------------------
// GridCpu 输出（一份完整重算结果）
// -----------------------------------------------------------------------------

struct GridResult {
    std::array<LabelResult, kRouteCount> labels{};   // 每路径 1 个 label
    GridDebugSnapshot                    debug{};    // 仅 debug=true 时填充

    // 时间戳（CPU 单线程实测 ms，便于 overlay 显示）
    float compute_ms = 0.0f;

    // GPU timestamp 测量（仅 GPU 路径填充；CPU 路径或设备不支持 timestamp 时保持 -1）
    //   - gpu_work_ms       = 5 个 dispatch 的纯 GPU 端执行时间（不含 fill / staging copy / submit / fence wait）
    //   - gpu_round_trip_ms = cmd buffer 内 GPU 端总耗时（含 zero-init fill + 5 dispatch + staging copy）
    // CPU 端 compute_ms 还会额外包含 host memcpy 上传 + vkQueueSubmit + vkWaitForFences + readback 解码，
    // 三者形成完整剖分：compute_ms ≥ gpu_round_trip_ms ≥ gpu_work_ms。
    float gpu_work_ms       = -1.0f;
    float gpu_round_trip_ms = -1.0f;
};

// -----------------------------------------------------------------------------
// 实用工具：网格索引
// -----------------------------------------------------------------------------

inline int tile_index(int cx, int cy, int n_x) { return cy * n_x + cx; }

// 把 1080p 屏的 routes-demo 默认通行时间作为 demo 默认值（doc §3.3）
struct TravelTimes {
    int minutes_a = 35;
    int minutes_b = 30;
    int minutes_c = 20;
};

}  // namespace routes_label::algo
