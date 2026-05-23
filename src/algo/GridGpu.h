#pragma once
//
// algo/GridGpu.h
// GPU 实现入口（Vulkan compute shader 4-stage pipeline）。
// 与 GridCpu 对外接口完全一致：compute(polylines, params, collect_debug) → GridResult。
//
// 对应文档：doc/routes-select-grid-gpu.md §4 全 4-stage compute pipeline
//          + §5 Buffer Layout / §6 CPU↔GPU 协作合约 / §7 降级
//
// 设计要点：
//   - PIMPL 隐藏所有 Vulkan 资源（Buffer / DescriptorSets / 5 ComputePipelines / fence / cmd pool）
//     避免污染头文件依赖；
//   - 构造期一次性按 doc §2.2 容量预算分配 SSBO，shader 加载失败/compute_family 缺失时
//     设置 is_available()=false，外部应回退 CPU；
//   - compute() 内部捕获异常 → 设 status=PoolOverflow 或 last_error_，让 RoutesRenderer
//     自动 fallback。
//

#include "algo/GridCommon.h"

#include <memory>
#include <string>

namespace routes_label::rhi {
class Device;
class PhysicalDevice;
}  // namespace routes_label::rhi

namespace routes_label::algo {

class GridGpu {
public:
    // 构造期完成所有 Vulkan 资源准备。失败时不会抛异常（除非 device 本身已坏），
    // 而是把 is_available() 置 false + last_error() 填充诊断信息。
    GridGpu(rhi::Device& device, rhi::PhysicalDevice& physical);
    ~GridGpu();

    GridGpu(const GridGpu&)            = delete;
    GridGpu& operator=(const GridGpu&) = delete;

    // 与 GridCpu::compute 接口完全一致（GridCommon.h）。
    // 内部失败时返回的 GridResult 中 labels[*].status 为 Infeasible 或 PoolOverflow，
    // 调用方应据此 fallback 到 CPU。
    GridResult compute(const Polylines& polylines,
                       const GridParams& params,
                       bool collect_debug = true);

    // GPU 路径是否可用。false 时 compute() 行为未定义（不应被调用）。
    [[nodiscard]] bool is_available() const noexcept;

    // 最近一次失败的人类可读原因（用于调试面板上的 "GPU 状态" 行）。
    [[nodiscard]] const std::string& last_error() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace routes_label::algo
