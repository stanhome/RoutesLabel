#pragma once
//
// ComputePipeline.h
// 持有 VkPipeline + VkPipelineLayout（compute 用）。
// 与 GraphicsPipeline 风格完全对齐，但只接收 1 个 .comp.spv，并支持 push_constant。
//
// 用途：doc/routes-select-grid-gpu.md §4 的 4-stage compute pipeline，每个 stage
// 一个独立 ComputePipeline 实例（共 5 个：clip / scan / pca / score / label）。
//

#include "rhi/VulkanCommon.h"

#include <filesystem>
#include <vector>

namespace routes_label::rhi {

class Device;

struct ComputePipelineDesc {
    std::filesystem::path comp_spv;

    // 多个 set layout（典型 1 个：所有 SSBO/UBO 都挂在同一个 set 上）
    std::vector<VkDescriptorSetLayout> descriptor_set_layouts;

    // 单一 push_constant range，VK_SHADER_STAGE_COMPUTE_BIT，offset=0。
    // size=0 表示不使用 push constant。
    uint32_t push_constant_size = 0;

    // 可选：local_size 通过 specialization constants 传给 shader（id=0/1/2 对应 x/y/z）。
    // 这里保留扩展点；本工程暂时通过 GLSL 中 layout(local_size_x=...) 静态指定，留空即可。
    std::vector<VkSpecializationMapEntry> spec_map;
    std::vector<uint8_t>                  spec_data;
};

class ComputePipeline {
public:
    ComputePipeline(const Device& device, const ComputePipelineDesc& desc);
    ~ComputePipeline();

    ComputePipeline(const ComputePipeline&)            = delete;
    ComputePipeline& operator=(const ComputePipeline&) = delete;

    [[nodiscard]] VkPipeline       handle() const noexcept { return pipeline_; }
    [[nodiscard]] VkPipelineLayout layout() const noexcept { return layout_; }

private:
    const Device&    device_;
    VkPipelineLayout layout_   = VK_NULL_HANDLE;
    VkPipeline       pipeline_ = VK_NULL_HANDLE;
};

}  // namespace routes_label::rhi
