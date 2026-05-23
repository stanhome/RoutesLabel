#pragma once
//
// GraphicsPipeline.h
// 持有 VkPipeline + VkPipelineLayout。
// 通过 GraphicsPipelineDesc 配置：vertex input / topology / primitiveRestart / descriptor set layouts。
// 保留 dynamic viewport+scissor、单 color attachment、不开 depth、不开 blend。
//

#include "rhi/VulkanCommon.h"

#include <filesystem>
#include <vector>

namespace routes_label::rhi {

class Device;
class RenderPass;

struct GraphicsPipelineDesc {
    std::filesystem::path vert_spv;
    std::filesystem::path frag_spv;

    std::vector<VkVertexInputBindingDescription>   vertex_bindings;
    std::vector<VkVertexInputAttributeDescription> vertex_attributes;

    VkPrimitiveTopology topology          = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkBool32            primitive_restart = VK_FALSE;

    std::vector<VkDescriptorSetLayout> descriptor_set_layouts;
};

class GraphicsPipeline {
public:
    GraphicsPipeline(const Device& device,
                     const RenderPass& render_pass,
                     const GraphicsPipelineDesc& desc);
    ~GraphicsPipeline();

    GraphicsPipeline(const GraphicsPipeline&)            = delete;
    GraphicsPipeline& operator=(const GraphicsPipeline&) = delete;

    VkPipeline       handle() const { return pipeline_; }
    VkPipelineLayout layout() const { return layout_; }

private:
    const Device&    device_;
    VkPipelineLayout layout_   = VK_NULL_HANDLE;
    VkPipeline       pipeline_ = VK_NULL_HANDLE;
};

}  // namespace routes_label::rhi
