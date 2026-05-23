//
// algo/GridCpu.cpp
// CPU 实现，对应 doc/routes-select-grid-gpu.md 全部 4 个 stage。
//

#include "algo/GridCpu.h"

#include "utils/Log.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace routes_label::algo {

namespace {

// -----------------------------------------------------------------------------
// 几何工具
// -----------------------------------------------------------------------------

inline Vec2f add(const Vec2f& a, const Vec2f& b) { return { a.x + b.x, a.y + b.y }; }
inline Vec2f mul(const Vec2f& v, float s)        { return { v.x * s, v.y * s }; }

// 单位方向 8 方位（对应 LabelSlot enum 顺序：N, NE, E, SE, S, SW, W, NW）
// 屏幕坐标系 y 朝下：N = (0, -1)。
constexpr float kInvSqrt2 = 0.7071067811865475f;
constexpr Vec2f kSlotDir[8] = {
    { 0.0f,        -1.0f       },   // N
    { kInvSqrt2,   -kInvSqrt2  },   // NE
    { 1.0f,         0.0f       },   // E
    { kInvSqrt2,    kInvSqrt2  },   // SE
    { 0.0f,         1.0f       },   // S
    { -kInvSqrt2,   kInvSqrt2  },   // SW
    { -1.0f,        0.0f       },   // W
    { -kInvSqrt2,  -kInvSqrt2  },   // NW
};

inline AABBf seg_aabb(const Vec2f& a, const Vec2f& b) {
    AABBf bb;
    bb.mn.x = std::min(a.x, b.x);
    bb.mn.y = std::min(a.y, b.y);
    bb.mx.x = std::max(a.x, b.x);
    bb.mx.y = std::max(a.y, b.y);
    return bb;
}

inline AABBf rect_aabb(const Vec2f& center, float w, float h) {
    AABBf bb;
    bb.mn.x = center.x - w * 0.5f;
    bb.mn.y = center.y - h * 0.5f;
    bb.mx.x = center.x + w * 0.5f;
    bb.mx.y = center.y + h * 0.5f;
    return bb;
}

// 线段-AABB 相交（Cohen-Sutherland 风格的 clip 测试）。
// 返回 true 当且仅当线段与 AABB 内部或边界有公共点（边界接触视为相交，比 AABB::intersects
// 严格——这里是 label 摆放硬约束 C2，宁严勿宽）。
bool segment_intersects_aabb(const Vec2f& p0, const Vec2f& p1, const AABBf& bb) {
    // 1. 端点在内部
    auto in = [&](const Vec2f& p) {
        return p.x >= bb.mn.x && p.x <= bb.mx.x
            && p.y >= bb.mn.y && p.y <= bb.mx.y;
    };
    if (in(p0) || in(p1)) return true;

    // 2. 线段 BBox 与 AABB 不相交则一定不交
    const float seg_min_x = std::min(p0.x, p1.x);
    const float seg_max_x = std::max(p0.x, p1.x);
    const float seg_min_y = std::min(p0.y, p1.y);
    const float seg_max_y = std::max(p0.y, p1.y);
    if (seg_max_x < bb.mn.x || seg_min_x > bb.mx.x
     || seg_max_y < bb.mn.y || seg_min_y > bb.mx.y) {
        return false;
    }

    // 3. 与 4 条边的相交检测（参数化 t ∈ [0,1]）
    const float dx = p1.x - p0.x;
    const float dy = p1.y - p0.y;

    // Liang-Barsky clip
    float t0 = 0.0f, t1 = 1.0f;
    auto clip = [&](float p, float q) {
        if (std::abs(p) < 1e-12f) {
            // 线段与该边平行
            return q >= 0.0f;
        }
        const float r = q / p;
        if (p < 0.0f) {
            if (r > t1) return false;
            if (r > t0) t0 = r;
        } else {
            if (r < t0) return false;
            if (r < t1) t1 = r;
        }
        return true;
    };

    if (!clip(-dx, p0.x - bb.mn.x)) return false;
    if (!clip( dx, bb.mx.x - p0.x)) return false;
    if (!clip(-dy, p0.y - bb.mn.y)) return false;
    if (!clip( dy, bb.mx.y - p0.y)) return false;

    return t0 <= t1;
}

// -----------------------------------------------------------------------------
// Stage A: Amanatides-Woo voxel traversal + 双 payload 入箱
// -----------------------------------------------------------------------------
//
// 把线段 (p0,p1) 切成"穿过的 tile"列表，并对每个 tile 记录 sub-seg（tile-clipped）
// 同时把整条线段的 AABB 写入它"覆盖"的所有 tile。
//
struct Cell {
    std::vector<SubSeg>  subsegs;  // 三路径混合存储，用 SubSeg::route_id 区分
    std::vector<SegAABB> aabbs;    // 整线段 AABB
    float arclen[kRouteCount] = { 0.0f, 0.0f, 0.0f };  // 每路径在该 tile 内的累计弧长
};

struct Grid {
    int   n_x = 0;
    int   n_y = 0;
    float s   = 1.0f;        // tile 边长 (px)
    std::vector<Cell> cells; // size = n_x * n_y

    [[nodiscard]] inline int idx(int cx, int cy) const { return cy * n_x + cx; }
    [[nodiscard]] inline bool inside(int cx, int cy) const {
        return cx >= 0 && cx < n_x && cy >= 0 && cy < n_y;
    }
};

void voxel_traverse_segment(const Vec2f& p0, const Vec2f& p1,
                            int route_id, Grid& g) {
    const float s = g.s;
    const float dx = p1.x - p0.x;
    const float dy = p1.y - p0.y;
    const float seg_len = std::sqrt(dx * dx + dy * dy);
    if (seg_len < 1e-6f) return;

    // 1. 完整线段 AABB 入"AABB-grid"
    const AABBf bb = seg_aabb(p0, p1);
    const int cx_lo = static_cast<int>(std::floor(bb.mn.x / s));
    const int cx_hi = static_cast<int>(std::floor(bb.mx.x / s));
    const int cy_lo = static_cast<int>(std::floor(bb.mn.y / s));
    const int cy_hi = static_cast<int>(std::floor(bb.mx.y / s));
    SegAABB sa;
    sa.bb = bb;
    sa.seg_a = p0;
    sa.seg_b = p1;
    sa.route_id = route_id;
    for (int cy = cy_lo; cy <= cy_hi; ++cy) {
        for (int cx = cx_lo; cx <= cx_hi; ++cx) {
            if (!g.inside(cx, cy)) continue;
            g.cells[g.idx(cx, cy)].aabbs.push_back(sa);
        }
    }

    // 2. tile-clipped sub_seg：Amanatides-Woo voxel traversal
    //    起点 cell
    int cx = static_cast<int>(std::floor(p0.x / s));
    int cy = static_cast<int>(std::floor(p0.y / s));
    const int cx_end = static_cast<int>(std::floor(p1.x / s));
    const int cy_end = static_cast<int>(std::floor(p1.y / s));

    // 步进方向
    const int step_x = (dx > 0.0f) ? 1 : ((dx < 0.0f) ? -1 : 0);
    const int step_y = (dy > 0.0f) ? 1 : ((dy < 0.0f) ? -1 : 0);

    // 到下一条 x 边 / y 边的参数 t（[0,1]，t=1 即终点）
    auto next_boundary = [&](float p, int c, int step) -> float {
        if (step == 0) return std::numeric_limits<float>::infinity();
        const float bx = (step > 0) ? (c + 1) * s : c * s;
        return bx - p;
    };
    const float inv_dx = (std::abs(dx) > 1e-12f) ? 1.0f / dx : 0.0f;
    const float inv_dy = (std::abs(dy) > 1e-12f) ? 1.0f / dy : 0.0f;

    float t_max_x = (step_x == 0) ? std::numeric_limits<float>::infinity()
                                  : next_boundary(p0.x, cx, step_x) * inv_dx;
    float t_max_y = (step_y == 0) ? std::numeric_limits<float>::infinity()
                                  : next_boundary(p0.y, cy, step_y) * inv_dy;
    const float t_delta_x = (step_x == 0) ? std::numeric_limits<float>::infinity()
                                          : s * std::abs(inv_dx);
    const float t_delta_y = (step_y == 0) ? std::numeric_limits<float>::infinity()
                                          : s * std::abs(inv_dy);

    auto emit_subseg = [&](int ix, int iy, float t_a, float t_b) {
        if (!g.inside(ix, iy)) return;
        if (t_b <= t_a) return;
        SubSeg ss;
        ss.start = { p0.x + dx * t_a, p0.y + dy * t_a };
        ss.end   = { p0.x + dx * t_b, p0.y + dy * t_b };
        ss.arclength = (t_b - t_a) * seg_len;
        ss.route_id  = route_id;
        const int id = g.idx(ix, iy);
        g.cells[id].subsegs.push_back(ss);
        g.cells[id].arclen[route_id] += ss.arclength;
    };

    // 边界保护：起点/终点都在 grid 外的极端线段直接降级（demo 场景里几乎不会发生）
    constexpr int kMaxSteps = 4096;
    int  steps = 0;
    float t_prev = 0.0f;
    while (true) {
        const bool at_end = (cx == cx_end && cy == cy_end);
        const float t_next_x = t_max_x;
        const float t_next_y = t_max_y;
        const float t_next   = std::min({ 1.0f, t_next_x, t_next_y });

        emit_subseg(cx, cy, t_prev, t_next);

        if (at_end || t_next >= 1.0f) break;
        if (++steps > kMaxSteps) break;

        if (t_next_x < t_next_y) {
            cx += step_x;
            t_prev   = t_max_x;
            t_max_x += t_delta_x;
        } else if (t_next_y < t_next_x) {
            cy += step_y;
            t_prev   = t_max_y;
            t_max_y += t_delta_y;
        } else {
            // 角点 corner case：x、y 边同时跨越，对角进
            cx += step_x;
            cy += step_y;
            t_prev   = t_max_x;
            t_max_x += t_delta_x;
            t_max_y += t_delta_y;
        }
    }
}

// -----------------------------------------------------------------------------
// Stage B: 加权 PCA（2x2 闭式特征分解）
// -----------------------------------------------------------------------------

void compute_tile_pca(const Cell& cell, TilePca& out) {
    out = {};
    out.arclen[0] = cell.arclen[0];
    out.arclen[1] = cell.arclen[1];
    out.arclen[2] = cell.arclen[2];

    // 至少 3 路径都覆盖 tile（doc §2.4 退化处理：W_total < 3 个有效 sub_seg）
    int routes_with_data = 0;
    for (int i = 0; i < kRouteCount; ++i) {
        if (cell.arclen[i] > 1e-3f) ++routes_with_data;
    }
    if (routes_with_data < kRouteCount) return;
    if (cell.subsegs.empty())          return;

    // 加权重心：用 sub_seg 中点 + 弧长权重（doc §2.4）
    double w_total = 0.0;
    double mx = 0.0, my = 0.0;
    for (const auto& ss : cell.subsegs) {
        const double w = ss.arclength;
        if (w <= 0.0) continue;
        const double cx = 0.5 * (ss.start.x + ss.end.x);
        const double cy = 0.5 * (ss.start.y + ss.end.y);
        mx += w * cx;
        my += w * cy;
        w_total += w;
    }
    if (w_total < 1e-3) return;
    mx /= w_total;
    my /= w_total;

    // 加权协方差 (a, b, c)：
    //   [a, b]
    //   [b, c]
    double a = 0.0, b = 0.0, c = 0.0;
    for (const auto& ss : cell.subsegs) {
        const double w = ss.arclength;
        if (w <= 0.0) continue;
        const double cx = 0.5 * (ss.start.x + ss.end.x) - mx;
        const double cy = 0.5 * (ss.start.y + ss.end.y) - my;
        a += w * cx * cx;
        b += w * cx * cy;
        c += w * cy * cy;
    }
    a /= w_total;
    b /= w_total;
    c /= w_total;

    // 闭式特征分解（doc §2.4）
    const double tr   = a + c;
    const double det  = a * c - b * b;
    const double disc = std::sqrt(std::max(0.25 * tr * tr - det, 0.0));
    const double l1   = 0.5 * tr + disc;
    const double l2   = 0.5 * tr - disc;

    Vec2f u{};
    if (std::abs(b) > 1e-9) {
        const double ux = b;
        const double uy = l1 - a;
        const double ul = std::sqrt(ux * ux + uy * uy);
        u = { static_cast<float>(ux / ul), static_cast<float>(uy / ul) };
    } else {
        u = (a >= c) ? Vec2f{ 1.0f, 0.0f } : Vec2f{ 0.0f, 1.0f };
    }
    const Vec2f v = { -u.y, u.x };

    out.mu     = { static_cast<float>(mx), static_cast<float>(my) };
    out.axis_u = u;
    out.axis_v = v;
    out.lambda1 = static_cast<float>(l1);
    out.lambda2 = static_cast<float>(l2);
    out.valid  = true;
}

// -----------------------------------------------------------------------------
// Stage C: sep / density / score
// -----------------------------------------------------------------------------

void compute_tile_score(const Cell& cell,
                        const TilePca& pca,
                        const GridParams& params,
                        TileScore& out) {
    out = {};
    if (!pca.valid) return;

    // 各向异性检查（doc §2.4 退化处理 + §7 共线兜底）
    if (pca.lambda2 > 1e-6f
        && pca.lambda1 / pca.lambda2 < params.anisotropy_min) {
        return;
    }

    // 三路径分别在主轴 u 上的投影区间
    constexpr float kPosInf = std::numeric_limits<float>::infinity();
    constexpr float kNegInf = -std::numeric_limits<float>::infinity();
    float t_min[kRouteCount] = { kPosInf, kPosInf, kPosInf };
    float t_max[kRouteCount] = { kNegInf, kNegInf, kNegInf };

    for (const auto& ss : cell.subsegs) {
        const int i = ss.route_id;
        // 投影两端点（doc §2.5：用 sub_seg 端点而非中点，关心 1D 覆盖范围）
        for (const Vec2f& p : { ss.start, ss.end }) {
            const float t = (p.x - pca.mu.x) * pca.axis_u.x
                          + (p.y - pca.mu.y) * pca.axis_u.y;
            if (t < t_min[i]) t_min[i] = t;
            if (t > t_max[i]) t_max[i] = t;
        }
    }

    // 闭区间长度
    float len[kRouteCount] = { 0.0f, 0.0f, 0.0f };
    for (int i = 0; i < kRouteCount; ++i) {
        if (t_min[i] > t_max[i]) return;
        len[i] = t_max[i] - t_min[i];
        out.intervals[i].t_min = t_min[i];
        out.intervals[i].t_max = t_max[i];
        out.intervals[i].valid = true;
    }
    const float total_len = len[0] + len[1] + len[2];
    if (total_len < 1e-3f) return;

    // 两两 overlap（doc §2.6）
    auto overlap = [&](int i, int j) {
        const float lo = std::max(t_min[i], t_min[j]);
        const float hi = std::min(t_max[i], t_max[j]);
        return std::max(0.0f, hi - lo);
    };
    const float ov = overlap(0, 1) + overlap(0, 2) + overlap(1, 2);
    const float sep = std::clamp(1.0f - ov / total_len, 0.0f, 1.0f);

    // density：三路径在 tile 内的弧长总和 / tile 边长（doc §2.7）
    const float total_arclen = pca.arclen[0] + pca.arclen[1] + pca.arclen[2];
    const float density = std::clamp(total_arclen / std::max(params.label_w, 1.0f), 0.0f, 1.0f);

    // arclength_balance（doc §2.7）：min/max < 0.2 视为"无人区"
    float al_min = pca.arclen[0];
    float al_max = pca.arclen[0];
    for (int i = 1; i < kRouteCount; ++i) {
        al_min = std::min(al_min, pca.arclen[i]);
        al_max = std::max(al_max, pca.arclen[i]);
    }
    const bool arclen_ok = (al_max > 1e-3f)
                       && (al_min / al_max >= params.arclength_balance);

    out.sep      = sep;
    out.density  = density;
    out.score    = arclen_ok ? (sep * density) : 0.0f;
    out.feasible = arclen_ok && (sep >= params.sep_threshold);
}

// -----------------------------------------------------------------------------
// Stage D: Top-3 + NMS（doc §2.8）+ 路径分配（§3.1）
// -----------------------------------------------------------------------------

struct ScoredTile {
    int   tile_idx = 0;
    int   cx       = 0;
    int   cy       = 0;
    float score    = 0.0f;
};

// 在 score 排序后的 tile 中按 NMS 取 top-3
std::vector<ScoredTile> select_top3(const Grid& g,
                                    const std::vector<TileScore>& scores,
                                    const GridParams& params) {
    std::vector<ScoredTile> all;
    all.reserve(scores.size());
    for (int cy = 0; cy < g.n_y; ++cy) {
        for (int cx = 0; cx < g.n_x; ++cx) {
            const int id = g.idx(cx, cy);
            if (!scores[id].feasible) continue;
            all.push_back({ id, cx, cy, scores[id].score });
        }
    }
    std::sort(all.begin(), all.end(), [](const ScoredTile& a, const ScoredTile& b) {
        return a.score > b.score;
    });

    std::vector<ScoredTile> selected;
    selected.reserve(kRouteCount);
    const int min_dist = params.nms_grid_distance;
    for (const auto& t : all) {
        bool ok = true;
        for (const auto& s : selected) {
            const int dx = std::abs(t.cx - s.cx);
            const int dy = std::abs(t.cy - s.cy);
            if (std::max(dx, dy) < min_dist) { ok = false; break; }
        }
        if (ok) selected.push_back(t);
        if (static_cast<int>(selected.size()) >= kRouteCount) break;
    }
    return selected;
}

// 每 tile 评估"三路径分别的 per-route 适合度"，返回 [route0_score, route1_score, route2_score]
std::array<float, kRouteCount> tile_per_route_score(const TileScore& ts,
                                                    const TilePca&   pca) {
    std::array<float, kRouteCount> r = { 0.0f, 0.0f, 0.0f };
    for (int i = 0; i < kRouteCount; ++i) {
        if (!ts.intervals[i].valid) continue;
        const float t_mid_i = 0.5f * (ts.intervals[i].t_min + ts.intervals[i].t_max);
        // 间距：与他路投影区间的"不重叠距离"（>0 表示完全分离）
        float gap_total = 0.0f;
        for (int j = 0; j < kRouteCount; ++j) {
            if (j == i) continue;
            const float a = ts.intervals[j].t_min - ts.intervals[i].t_max;
            const float b = ts.intervals[i].t_min - ts.intervals[j].t_max;
            const float gap = std::max({ a, b, 0.0f });
            gap_total += gap;
        }
        // 居中性：t_mid_i 越靠 0（即 PCA 重心）越好
        const float center_pen = std::abs(t_mid_i);
        r[i] = gap_total - 0.1f * center_pen;
        // 还要要求路径 i 在 tile 内有显著弧长（不然就是"被擦边的路径"）
        if (pca.arclen[i] < 1.0f) r[i] = -1e9f;
    }
    return r;
}

// 把 Top-3 tile 分配给 3 条路径（暴力枚举 6 种排列，N=3 完全可接受）
std::array<int, kRouteCount> assign_routes(
    const std::vector<ScoredTile>& tiles,
    const std::vector<TileScore>&  scores,
    const std::vector<TilePca>&    pcas)
{
    // tile_idx_for_route[r] = 该路径分配到的 tile 在 tiles 数组中的下标
    std::array<int, kRouteCount> result = { -1, -1, -1 };
    if (tiles.size() < kRouteCount) return result;

    std::array<std::array<float, kRouteCount>, kRouteCount> per_route_score;
    for (int t = 0; t < kRouteCount; ++t) {
        per_route_score[t] = tile_per_route_score(scores[tiles[t].tile_idx],
                                                  pcas[tiles[t].tile_idx]);
    }

    // 3! = 6 种排列
    static const int perms[6][3] = {
        {0,1,2}, {0,2,1}, {1,0,2}, {1,2,0}, {2,0,1}, {2,1,0}
    };
    float best = -1e30f;
    int   best_idx = 0;
    for (int p = 0; p < 6; ++p) {
        const int r0 = perms[p][0], r1 = perms[p][1], r2 = perms[p][2];
        // tile 0 → route r0, tile 1 → route r1, tile 2 → route r2
        const float total = per_route_score[0][r0]
                          + per_route_score[1][r1]
                          + per_route_score[2][r2];
        if (total > best) { best = total; best_idx = p; }
    }
    result[perms[best_idx][0]] = 0;
    result[perms[best_idx][1]] = 1;
    result[perms[best_idx][2]] = 2;
    return result;
}

// -----------------------------------------------------------------------------
// Label 摆放（doc §3.2）：grid-AABB 加速 8 slot 校验
// -----------------------------------------------------------------------------

bool label_aabb_intersects_other_routes(const AABBf& L,
                                        int          self_route,
                                        const Grid&  g) {
    const int cx_lo = std::max(0,
        static_cast<int>(std::floor(L.mn.x / g.s)));
    const int cx_hi = std::min(g.n_x - 1,
        static_cast<int>(std::floor(L.mx.x / g.s)));
    const int cy_lo = std::max(0,
        static_cast<int>(std::floor(L.mn.y / g.s)));
    const int cy_hi = std::min(g.n_y - 1,
        static_cast<int>(std::floor(L.mx.y / g.s)));

    // 注意：单条线段可能跨越多个 tile，会被重复登记为 SegAABB；
    // 用 (route_id, &SegAABB) 直接对比指针即可，因为 SegAABB 在 vector 里地址稳定
    // —— 但 cells_aabb[t] 跨 tile 可能多次出现同一线段。为了避免对同一线段重复 SAT，
    // 用 (seg_a, seg_b, route_id) 简易去重（线段数小，set 足够轻量）。
    // 实操：直接做重复 SAT 也只是常数倍代价，这里为了简单不去重。
    for (int cy = cy_lo; cy <= cy_hi; ++cy) {
        for (int cx = cx_lo; cx <= cx_hi; ++cx) {
            const auto& cell = g.cells[g.idx(cx, cy)];
            for (const auto& sa : cell.aabbs) {
                if (sa.route_id == self_route) continue;
                if (!L.intersects(sa.bb))      continue;   // AABB-AABB 快速排除
                if (segment_intersects_aabb(sa.seg_a, sa.seg_b, L)) {
                    return true;
                }
            }
        }
    }
    return false;
}

// 给定 anchor 与 route_id，枚举 8 slot，返回最优结果（status=Ok）。
// already_placed: 之前已经摆放成功的 label 列表（用于 C3 互不重叠硬约束）。
// 若所有 slot 都失败，返回 status=Infeasible 的"原地" label（CPU 兜底，仍便于 debug 显示）。
LabelResult place_label(const Vec2f& anchor,
                        int          route_id,
                        const Grid&  g,
                        const GridParams& params,
                        const std::vector<AABBf>& already_placed_aabbs) {
    const float diag = std::sqrt(params.label_w * params.label_w
                               + params.label_h * params.label_h);
    const float dist_anchor_to_center = params.leader_len + 0.5f * diag;

    // 给已选 label AABB 加一点 padding（视觉缓冲），让 label 之间有间隔
    constexpr float kLabelPadPx = 12.0f;
    auto inflate = [](const AABBf& a, float p) -> AABBf {
        AABBf b = a;
        b.mn.x -= p; b.mn.y -= p; b.mx.x += p; b.mx.y += p;
        return b;
    };

    LabelResult best{};
    best.anchor   = anchor;
    best.route_id = route_id;
    best.status   = LabelStatus::Infeasible;
    float best_cost = std::numeric_limits<float>::infinity();

    auto overlaps_already_placed = [&](const AABBf& L) {
        for (const auto& a : already_placed_aabbs) {
            if (L.intersects(inflate(a, kLabelPadPx))) return true;
        }
        return false;
    };

    // UI panel rect 作为额外的"obstructing view"硬约束（doc §1.1 C2 推广）
    // 带 8 px padding 视觉缓冲，避免 label 贴边 panel
    constexpr float kPanelPadPx = 8.0f;
    const bool has_panel = !params.panel_rect.empty();
    const AABBf panel_inflated = has_panel ? inflate(params.panel_rect, kPanelPadPx) : AABBf{};

    auto overlaps_panel = [&](const AABBf& L) {
        return has_panel && L.intersects(panel_inflated);
    };

    for (int s = 0; s < 8; ++s) {
        const Vec2f dir = kSlotDir[s];
        const Vec2f cen = add(anchor, mul(dir, dist_anchor_to_center));
        const AABBf L   = rect_aabb(cen, params.label_w, params.label_h);

        // 边界约束：label 不能超出屏幕
        constexpr float kEdge = 8.0f;
        if (L.mn.x < kEdge || L.mx.x > params.screen_w - kEdge
         || L.mn.y < kEdge || L.mx.y > params.screen_h - kEdge) {
            continue;
        }

        // C2：与他路线段不相交
        if (label_aabb_intersects_other_routes(L, route_id, g)) continue;
        // C2'：与 UI panel 不相交
        if (overlaps_panel(L)) continue;
        // C3：与已放置 label 不相交（硬约束）
        if (overlaps_already_placed(L)) continue;

        // 软目标：偏好引线短 + 远离屏幕中心（避免堆到中央）
        const float dx_anc = cen.x - anchor.x;
        const float dy_anc = cen.y - anchor.y;
        const float lead_dist = std::sqrt(dx_anc * dx_anc + dy_anc * dy_anc);
        const float dx_c = cen.x - 0.5f * params.screen_w;
        const float dy_c = cen.y - 0.5f * params.screen_h;
        const float radial = std::sqrt(dx_c * dx_c + dy_c * dy_c);
        const float cost = lead_dist - 0.05f * radial;

        if (cost < best_cost) {
            best_cost = cost;
            best.anchor       = anchor;
            best.label_center = cen;
            best.route_id     = route_id;
            best.slot         = static_cast<LabelSlot>(s);
            best.status       = LabelStatus::Ok;
        }
    }

    // 全失败 → 放宽 C3（允许 label 间小重叠）再试，但仍保留"不压 panel"硬约束
    if (best.status == LabelStatus::Infeasible) {
        for (int s = 0; s < 8; ++s) {
            const Vec2f dir = kSlotDir[s];
            const Vec2f cen = add(anchor, mul(dir, dist_anchor_to_center));
            const AABBf L   = rect_aabb(cen, params.label_w, params.label_h);
            constexpr float kEdge = 8.0f;
            if (L.mn.x < kEdge || L.mx.x > params.screen_w - kEdge
             || L.mn.y < kEdge || L.mx.y > params.screen_h - kEdge) {
                continue;
            }
            if (label_aabb_intersects_other_routes(L, route_id, g)) continue;
            if (overlaps_panel(L)) continue;   // panel 仍是硬约束
            // 允许 label 间重叠
            best.anchor       = anchor;
            best.label_center = cen;
            best.route_id     = route_id;
            best.slot         = static_cast<LabelSlot>(s);
            best.status       = LabelStatus::FallbackUsed;
            break;
        }
    }
    if (best.status == LabelStatus::Infeasible) {
        // 最后兜底：放宽屏幕边界 + panel，避免完全无解
        for (int s = 0; s < 8; ++s) {
            const Vec2f dir = kSlotDir[s];
            const Vec2f cen = add(anchor, mul(dir, dist_anchor_to_center));
            const AABBf L   = rect_aabb(cen, params.label_w, params.label_h);
            if (label_aabb_intersects_other_routes(L, route_id, g)) continue;
            best.anchor       = anchor;
            best.label_center = cen;
            best.route_id     = route_id;
            best.slot         = static_cast<LabelSlot>(s);
            best.status       = LabelStatus::FallbackUsed;
            break;
        }
    }

    return best;
}

// 互不重叠校验（C3，doc §1.2）：3 个 label 矩形两两不相交。
// 若失败，把后选的 label 标记为 FallbackUsed 但不强制移动（demo 可视化层面允许小重叠）。
void check_label_mutual_overlap(std::array<LabelResult, kRouteCount>& labels,
                                const GridParams& params) {
    for (int i = 0; i < kRouteCount; ++i) {
        if (labels[i].status == LabelStatus::Infeasible) continue;
        const AABBf L_i = rect_aabb(labels[i].label_center, params.label_w, params.label_h);
        for (int j = 0; j < i; ++j) {
            if (labels[j].status == LabelStatus::Infeasible) continue;
            const AABBf L_j = rect_aabb(labels[j].label_center, params.label_w, params.label_h);
            if (L_i.intersects(L_j)) {
                labels[i].status = LabelStatus::FallbackUsed;
                break;
            }
        }
    }
}

// -----------------------------------------------------------------------------
// Per-route exclusive tile（fallback）
//
// doc §10.3 "Fallback_C" 思路：当 §2 主算法（三路径在同一 tile 内沿 PCA 主轴各占一段）
// 找不到 3 个 tile（例如三路径明显空间分叉，几乎不进入同一 36 px tile），降级到这里。
//
// 核心：对每条路径 i，在 grid 中扫描"i 进入但 j、k 不进入"的 tile，按 i 在 tile 内的弧长
// 取最大值。这等价于 doc/routes-select.md 候选 1 的"独占段中心点"概念，但走 grid 加速。
// -----------------------------------------------------------------------------

struct ExclusiveCandidate {
    int   tile_idx = -1;
    int   cx       = 0;
    int   cy       = 0;
    Vec2f anchor{};      // 该路径在该 tile 内 sub_seg 的中点
    float self_len = 0.0f;
};

std::array<ExclusiveCandidate, kRouteCount>
find_per_route_exclusive_tiles(const Grid& g) {
    constexpr float kOtherEpsilon = 0.5f;   // 他路弧长 ≤ 0.5 px 视为"未进入"
    std::array<ExclusiveCandidate, kRouteCount> best{};

    for (int cy = 0; cy < g.n_y; ++cy) {
        for (int cx = 0; cx < g.n_x; ++cx) {
            const int id = g.idx(cx, cy);
            const Cell& cell = g.cells[id];
            for (int i = 0; i < kRouteCount; ++i) {
                const float self_len = cell.arclen[i];
                if (self_len <= kOtherEpsilon) continue;

                bool exclusive = true;
                for (int j = 0; j < kRouteCount; ++j) {
                    if (j == i) continue;
                    if (cell.arclen[j] > kOtherEpsilon) { exclusive = false; break; }
                }
                if (!exclusive) continue;

                if (self_len > best[i].self_len) {
                    // anchor = 该路径在该 tile 内首个 sub_seg 的中点（最稳定）
                    Vec2f anchor{};
                    for (const auto& ss : cell.subsegs) {
                        if (ss.route_id == i) {
                            anchor = { 0.5f * (ss.start.x + ss.end.x),
                                       0.5f * (ss.start.y + ss.end.y) };
                            break;
                        }
                    }
                    best[i] = { id, cx, cy, anchor, self_len };
                }
            }
        }
    }
    return best;
}

}  // namespace

// -----------------------------------------------------------------------------
// GridCpu::compute
// -----------------------------------------------------------------------------

GridResult GridCpu::compute(const Polylines& polylines,
                            const GridParams& params,
                            bool collect_debug) const {
    const auto t0 = std::chrono::steady_clock::now();
    GridResult out{};

    // ---- 0. Grid 初始化 ----
    if (params.screen_w <= 0.0f || params.screen_h <= 0.0f || params.n_grid <= 1) {
        return out;
    }
    Grid g;
    g.s   = std::max(1.0f,
        std::floor(std::min(params.screen_w, params.screen_h)
                   / static_cast<float>(params.n_grid)));
    g.n_x = std::max(1, static_cast<int>(std::ceil(params.screen_w / g.s)));
    g.n_y = std::max(1, static_cast<int>(std::ceil(params.screen_h / g.s)));
    g.cells.assign(static_cast<size_t>(g.n_x) * static_cast<size_t>(g.n_y), Cell{});

    // ---- Stage A：双 payload 入箱 ----
    int total_segs = 0;
    for (int r = 0; r < kRouteCount; ++r) {
        const auto& poly = polylines.routes[r];
        if (poly.size() < 2) continue;
        for (size_t k = 0; k + 1 < poly.size(); ++k) {
            voxel_traverse_segment(poly[k], poly[k + 1], r, g);
            ++total_segs;
        }
    }

    // ---- Stage B：per-tile PCA ----
    std::vector<TilePca> tile_pcas(g.cells.size());
    for (size_t t = 0; t < g.cells.size(); ++t) {
        compute_tile_pca(g.cells[t], tile_pcas[t]);
    }

    // ---- Stage C：sep / density / score ----
    std::vector<TileScore> tile_scores(g.cells.size());
    for (size_t t = 0; t < g.cells.size(); ++t) {
        compute_tile_score(g.cells[t], tile_pcas[t], params, tile_scores[t]);
    }

    // ---- Stage D-1：Top-3 + NMS ----
    auto top3 = select_top3(g, tile_scores, params);

    // ---- Stage D-2：路径分配 ----
    std::array<int, kRouteCount> tile_for_route = { -1, -1, -1 };
    bool used_fallback = false;
    if (top3.size() >= kRouteCount) {
        const auto assign = assign_routes(top3, tile_scores, tile_pcas);
        for (int r = 0; r < kRouteCount; ++r) {
            if (assign[r] >= 0 && assign[r] < static_cast<int>(top3.size())) {
                tile_for_route[r] = assign[r];
            }
        }
    }

    // ---- Stage D-3：每条路径反投影 anchor + label 摆放 ----
    // 主算法路径：从 PCA tile 反投影
    std::array<Vec2f, kRouteCount> anchors{};
    std::array<bool,  kRouteCount> anchors_valid{ false, false, false };
    for (int r = 0; r < kRouteCount; ++r) {
        if (tile_for_route[r] < 0) continue;
        const ScoredTile& st = top3[tile_for_route[r]];
        const TilePca&    pca = tile_pcas[st.tile_idx];
        const TileScore&  ts  = tile_scores[st.tile_idx];
        const float t_mid = 0.5f * (ts.intervals[r].t_min + ts.intervals[r].t_max);
        anchors[r] = {
            pca.mu.x + t_mid * pca.axis_u.x,
            pca.mu.y + t_mid * pca.axis_u.y,
        };
        anchors_valid[r] = true;
    }

    // Fallback：主算法没覆盖的路径，走 per-route 独占 tile 扫描（doc §10.3 + §7）
    bool any_missing = false;
    for (int r = 0; r < kRouteCount; ++r) {
        if (!anchors_valid[r]) { any_missing = true; break; }
    }
    if (any_missing) {
        used_fallback = true;
        const auto excl = find_per_route_exclusive_tiles(g);
        for (int r = 0; r < kRouteCount; ++r) {
            if (anchors_valid[r]) continue;
            if (excl[r].tile_idx >= 0) {
                anchors[r]       = excl[r].anchor;
                anchors_valid[r] = true;
                if (collect_debug) {
                    out.debug.selected_tile_index[r] = excl[r].tile_idx;
                }
            }
        }
    }

    // 摆 label：按 anchor 离屏幕中心远近顺序（远的先摆，因为外圈空间更大）
    // 这样可以最大化避免 C3 冲突
    std::array<int, kRouteCount> place_order = { 0, 1, 2 };
    {
        std::array<float, kRouteCount> rad_dist{};
        for (int r = 0; r < kRouteCount; ++r) {
            const float dx = anchors[r].x - 0.5f * params.screen_w;
            const float dy = anchors[r].y - 0.5f * params.screen_h;
            rad_dist[r] = std::sqrt(dx * dx + dy * dy);
        }
        std::sort(place_order.begin(), place_order.end(),
                  [&](int a, int b) { return rad_dist[a] > rad_dist[b]; });
    }

    std::vector<AABBf> placed_aabbs;
    placed_aabbs.reserve(kRouteCount);
    for (int idx = 0; idx < kRouteCount; ++idx) {
        const int r = place_order[idx];
        if (!anchors_valid[r]) {
            out.labels[r].route_id = r;
            out.labels[r].status   = LabelStatus::Infeasible;
            continue;
        }
        out.labels[r] = place_label(anchors[r], r, g, params, placed_aabbs);
        if (used_fallback && out.labels[r].status == LabelStatus::Ok) {
            out.labels[r].status = LabelStatus::FallbackUsed;
        }
        if (out.labels[r].status != LabelStatus::Infeasible) {
            placed_aabbs.push_back(
                rect_aabb(out.labels[r].label_center,
                          params.label_w, params.label_h));
        }
    }

    // ---- C3 互不重叠校验（兜底标记，不强制重摆）----
    check_label_mutual_overlap(out.labels, params);

    // ---- Debug snapshot ----
    if (collect_debug) {
        out.debug.params      = params;
        out.debug.n_x         = g.n_x;
        out.debug.n_y         = g.n_y;
        out.debug.tile_size   = g.s;
        out.debug.tile_scores = std::move(tile_scores);
        out.debug.tile_pca    = std::move(tile_pcas);
        for (int r = 0; r < kRouteCount; ++r) {
            if (tile_for_route[r] >= 0) {
                out.debug.selected_tile_index[r] = top3[tile_for_route[r]].tile_idx;
            }
        }
    }

    const auto t1 = std::chrono::steady_clock::now();
    out.compute_ms = std::chrono::duration<float, std::milli>(t1 - t0).count();
    int placed = 0;
    int infeasible = 0;
    for (int r = 0; r < kRouteCount; ++r) {
        if (out.labels[r].status == LabelStatus::Infeasible) ++infeasible;
        else                                                 ++placed;
    }
    LOG_INFO("[GridCpu] computed " << total_segs << " segs, grid=" << g.n_x << "x" << g.n_y
             << " (s=" << g.s << " px), tiles=" << g.cells.size()
             << ", top=" << top3.size() << ", placed=" << placed
             << ", infeasible=" << infeasible
             << (used_fallback ? " (fallback)" : "")
             << ", time=" << out.compute_ms << " ms");
    return out;
}

}  // namespace routes_label::algo
