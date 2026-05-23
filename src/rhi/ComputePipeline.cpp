#include "rhi/ComputePipeline.h"

#include "rhi/Device.h"
#include "rhi/ShaderModule.h"
#include "utils/Log.h"

namespace routes_label::rhi {

ComputePipeline::ComputePipeline(const Device& device,
                                 const ComputePipelineDesc& desc)
    : device_(device) {
    // 1. shader stage
    ShaderModule comp(device_, desc.comp_spv);

    VkSpecializationInfo spec_info = {};
    if (!desc.spec_map.empty() && !desc.spec_data.empty()) {
        spec_info.mapEntryCount = static_cast<uint32_t>(desc.spec_map.size());
        spec_info.pMapEntries   = desc.spec_map.data();
        spec_info.dataSize      = desc.spec_data.size();
        spec_info.pData         = desc.spec_data.data();
    }

    VkPipelineShaderStageCreateInfo stage = {};
    stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = comp.handle();
    stage.pName  = "main";
    stage.pSpecializationInfo =
        (!desc.spec_map.empty() && !desc.spec_data.empty()) ? &spec_info : nullptr;

    // 2. pipeline layout（descriptor sets + 可选 push constant）
    VkPushConstantRange pc_range = {};
    pc_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pc_range.offset     = 0;
    pc_range.size       = desc.push_constant_size;

    VkPipelineLayoutCreateInfo lci = {};
    lci.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    lci.setLayoutCount = static_cast<uint32_t>(desc.descriptor_set_layouts.size());
    lci.pSetLayouts    = desc.descriptor_set_layouts.empty()
                       ? nullptr : desc.descriptor_set_layouts.data();
    if (desc.push_constant_size > 0) {
        lci.pushConstantRangeCount = 1;
        lci.pPushConstantRanges    = &pc_range;
    }
    VK_CHECK(vkCreatePipelineLayout(device_.handle(), &lci, nullptr, &layout_));

    // 3. compute pipeline
    VkComputePipelineCreateInfo pci = {};
    pci.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pci.stage  = stage;
    pci.layout = layout_;

    VK_CHECK(vkCreateComputePipelines(device_.handle(), VK_NULL_HANDLE,
                                      1, &pci, nullptr, &pipeline_));
    LOG_INFO("[ComputePipeline] created (spv='"
             << desc.comp_spv.filename().string()
             << "', set_layouts=" << desc.descriptor_set_layouts.size()
             << ", push_constant=" << desc.push_constant_size << " B)");
}

ComputePipeline::~ComputePipeline() {
    if (pipeline_ != VK_NULL_HANDLE) vkDestroyPipeline(device_.handle(), pipeline_, nullptr);
    if (layout_   != VK_NULL_HANDLE) vkDestroyPipelineLayout(device_.handle(), layout_, nullptr);
}

}  // namespace routes_label::rhi
