// shaders/grid_common.glsl
//
// Routes-Select Grid GPU pipeline 的共享 binding / struct / 工具函数。
// 5 个 compute shader（grid_clip / grid_scan / grid_pca / grid_score / grid_label）
// 都通过 #include 引入此文件以保证 binding 一致。
//
// 对应文档：doc/routes-select-grid-gpu.md §2 / §4 / §5
// 对应 CPU 端：src/algo/GridCommon.h（CPU/GPU 数据结构同义但布局不同）
// 对应 host 端 binding 编号：src/algo/GridGpu.cpp 的 Bind enum
//

#ifndef GRID_COMMON_GLSL
#define GRID_COMMON_GLSL

// -----------------------------------------------------------------------------
// 静态预算（必须与 src/algo/GridGpu.cpp 中的 kMax* 完全一致）
// 2026-05 升级到 ~5K 顶点 demo，扩容 SUBSEGS/AABBS/POLY_VERTS。
// -----------------------------------------------------------------------------
#define MAX_TILES        8192
#define MAX_SUBSEGS      32768
#define MAX_AABBS        16384
#define MAX_POLY_VERTS   16384
#define ROUTE_COUNT      3

// -----------------------------------------------------------------------------
// Params UBO（binding 0）
//
// 与 GridCommon.h::GridParams 对齐的核心字段；多余的 CPU-only 字段（如 panel_rect 已
// 拆为 4 个 float）也放进来，保证一次上传所有 stage 都能直接读。
// std140 对齐：保持 16B 边界。总大小 64 B（与 host 端 params_ubo 大小一致）。
// -----------------------------------------------------------------------------
layout(set = 0, binding = 0, std140) uniform Params {
    float screen_w;
    float screen_h;
    int   n_x;
    int   n_y;             // 16

    float tile_size;
    int   n_grid;
    float separationThreshold;
    float arclength_balance; // 32

    int   nms_grid_distance;
    float anisotropy_min;
    float label_w;
    float label_h;          // 48

    float leader_len;
    int   total_poly_verts; // 三路径合计顶点数
    float panel_min_x;
    float panel_min_y;      // 64

    float panel_max_x;
    float panel_max_y;
    float _pad0;
    float _pad1;            // 80（实际只用 64B；host 端 params_ubo 现为 64B 已不够，需扩到 80B）
} P;

// -----------------------------------------------------------------------------
// Polylines buf（binding 1）：vec2 数组，三路径首尾相接
// segment_offsets buf（binding 2）：[off_a, off_b, off_c, len_a, len_b, len_c, _, _]
//   route i 的顶点范围 = [off_i, off_i + len_i)
// -----------------------------------------------------------------------------
struct Vec2Pad {
    float x;
    float y;
};

layout(set = 0, binding = 1, std430) readonly buffer Polylines {
    Vec2Pad data[];
} polylines;

layout(set = 0, binding = 2, std430) readonly buffer SegmentOffsets {
    uint data[];
} seg_offsets;

// -----------------------------------------------------------------------------
// SubSeg pool（binding 3 count / 4 offset / 5 pool）
// SubSeg = vec2 start + vec2 end + float arclength + uint route_id + uint tile_id + 4B pad → 32B
//
// 设计取舍：本工程为简化 4-stage pipeline，不做 prefix-scan-scatter 两 pass，
// 改为"全局 pool atomicAdd append + 自带 tile_id 字段"。Stage B/C 的 per-tile workgroup
// 协同扫描全局 pool 并 filter，N_subseg ≤ 8192 时延仍在 1 ms 以内。
// -----------------------------------------------------------------------------
struct SubSeg {
    vec2  start;
    vec2  end;
    float arclength;
    uint  route_id;
    uint  tile_id;     // 全局扫描时用以 filter
    uint  _pad0;
};

layout(set = 0, binding = 3, std430) buffer SubsegCount  { uint  data[]; } subseg_count;   // [t*3+r] 每 tile 每路径计数
layout(set = 0, binding = 4, std430) buffer SubsegOffset { uint  data[]; } subseg_offset;  // 保留供未来 prefix-scan 使用
layout(set = 0, binding = 5, std430) buffer SubsegPool   { SubSeg data[]; } subseg_pool;

// -----------------------------------------------------------------------------
// AABB pool（binding 6 count / 7 offset / 8 pool）
// SegAABB = vec2 mn + vec2 mx + uint route_id + uint global_seg_id + 8B pad → 32B
// -----------------------------------------------------------------------------
struct SegAABB {
    vec2 mn;
    vec2 mx;
    uint route_id;
    uint global_seg_id;
    uint tile_id;        // 全局扫描时用以 filter
    uint _pad0;
};

layout(set = 0, binding = 6, std430) buffer AabbCount  { uint    data[]; } aabb_count;    // size = MAX_TILES
layout(set = 0, binding = 7, std430) buffer AabbOffset { uint    data[]; } aabb_offset;   // size = MAX_TILES
layout(set = 0, binding = 8, std430) buffer AabbPool   { SegAABB data[]; } aabb_pool;

// -----------------------------------------------------------------------------
// Tile PCA / Tile Score（binding 9 / 10）
// -----------------------------------------------------------------------------
struct TilePca {
    vec2  mu;            // 8
    vec2  axis_u;        // 16
    vec2  axis_v;        // 24
    float lambda1;       // 28
    float lambda2;       // 32
    float arclength0;    // 36
    float arclength1;    // 40
    float arclength2;    // 44
    uint  valid;         // 48
    // pad to 64
    uint  _pad0;
    uint  _pad1;
    uint  _pad2;
    uint  _pad3;
};

struct ProjInterval {
    float t_min;
    float t_max;
    uint  valid;
};

struct TileScore {
    float separation;       // 4
    float density;          // 8
    float score;            // 12
    uint  feasible;         // 16
    ProjInterval iv0;       // 12 → 28
    ProjInterval iv1;       // 12 → 40
    ProjInterval iv2;       // 12 → 52
    uint  _pad0;            // 56
    uint  _pad1;            // 60
    uint  _pad2;            // 64
};

layout(set = 0, binding = 9,  std430) buffer TilePcaBuf   { TilePca   data[]; } tile_pca;
layout(set = 0, binding = 10, std430) buffer TileScoreBuf { TileScore data[]; } tile_score;

// -----------------------------------------------------------------------------
// Final labels（binding 11）：3 个 LabelResult，doc §6.2 32B std430
// -----------------------------------------------------------------------------
struct LabelResult {
    vec2 anchor;          // 8
    vec2 label_center;    // 16
    uint route_id;        // 20
    uint slot;            // 24
    uint status;          // 28（0=Ok 1=FallbackUsed 2=Infeasible 3=PoolOverflow）
    uint _pad0;           // 32
};

layout(set = 0, binding = 11, std430) buffer FinalLabelsBuf { LabelResult data[]; } final_labels;

// -----------------------------------------------------------------------------
// Status flags（binding 12）：[0]=pool_overflow [1..3]=保留
// -----------------------------------------------------------------------------
layout(set = 0, binding = 12, std430) buffer StatusFlagsBuf { uint data[]; } status_flags;

// -----------------------------------------------------------------------------
// 工具
// -----------------------------------------------------------------------------
int tile_index(int cx, int cy)  { return cy * P.n_x + cx; }
bool tile_inside(int cx, int cy) { return cx >= 0 && cx < P.n_x && cy >= 0 && cy < P.n_y; }

vec2 read_polyvert(uint idx) {
    return vec2(polylines.data[idx].x, polylines.data[idx].y);
}

#endif  // GRID_COMMON_GLSL
