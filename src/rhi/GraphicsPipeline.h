#pragma once
//
// GraphicsPipeline.h
// 持有 VkPipeline + VkPipelineLayout（首期 layout 为空）。
//

#include "rhi/VulkanCommon.h"

#include <filesystem>

namespace routes_label::rhi {

class Device;
class RenderPass;

class GraphicsPipeline {
public:
    GraphicsPipeline(const Device& device,
                     const RenderPass& render_pass,
                     const std::filesystem::path& vert_spv,
                     const std::filesystem::path& frag_spv);
    ~GraphicsPipeline();

    GraphicsPipeline(const GraphicsPipeline&) = delete;
    GraphicsPipeline& operator=(const GraphicsPipeline&) = delete;

    VkPipeline       handle() const { return pipeline_; }
    VkPipelineLayout layout() const { return layout_; }

private:
    const Device&    device_;
    VkPipelineLayout layout_   = VK_NULL_HANDLE;
    VkPipeline       pipeline_ = VK_NULL_HANDLE;
};

}  // namespace routes_label::rhi
