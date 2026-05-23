#pragma once
//
// algo/GridCpu.h
// CPU 单线程参考实现（ground truth + 默认运行路径）。
//
// 对应文档：doc/routes-select-grid-gpu.md §7.2 + §3 + §2
//          doc/routes-select-grid-variance.md §2 (数学定义) + §6 (CPU 实现建议)
//
// 流程：
//   Stage A: 双 payload 同次 voxel traversal（线段 → tile-clipped sub_seg + 完整线段 AABB 入箱）
//   Stage B: per-tile 加权 PCA（2x2 闭式特征分解）
//   Stage C: separation / density / score
//   Stage D: Top-3 + NMS + label 摆放（grid-AABB 加速 8 slot 校验）
//
// 复杂度：O(N · √n_grid)，N=100 / 1080p 实测 < 1 ms（doc §9）。
//

#include "algo/GridCommon.h"

namespace routes_label::algo {

class GridCpu {
public:
    GridCpu() = default;

    // 输入三路径 polyline 与算法参数，返回完整结果（含 debug snapshot）。
    // collect_debug=true 时填充 result.debug；false 时跳过 debug 数据写入以省时间。
    GridResult compute(const Polylines& polylines,
                       const GridParams& params,
                       bool collect_debug = true) const;
};

}  // namespace routes_label::algo
