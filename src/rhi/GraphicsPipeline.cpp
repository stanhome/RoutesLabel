#include "rhi/GraphicsPipeline.h"

#include "rhi/Device.h"
#include "rhi/RenderPass.h"
#include "rhi/ShaderModule.h"
#include "utils/Log.h"

namespace routes_label::rhi {

GraphicsPipeline::GraphicsPipeline(const Device& device,
                                   const RenderPass& render_pass,
                                   const GraphicsPipelineDesc& desc)
    : device_(device) {
    ShaderModule vert(device_, desc.vert_spv);
    ShaderModule frag(device_, desc.frag_spv);

    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert.handle();
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag.handle();
    stages[1].pName  = "main";

    VkPipelineVertexInputStateCreateInfo vi = {};
    vi.sType                         = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = static_cast<uint32_t>(desc.vertex_bindings.size());
    vi.pVertexBindingDescriptions    = desc.vertex_bindings.empty()
                                       ? nullptr : desc.vertex_bindings.data();
    vi.vertexAttributeDescriptionCount = static_cast<uint32_t>(desc.vertex_attributes.size());
    vi.pVertexAttributeDescriptions    = desc.vertex_attributes.empty()
                                       ? nullptr : desc.vertex_attributes.data();

    VkPipelineInputAssemblyStateCreateInfo ia = {};
    ia.sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology               = desc.topology;
    ia.primitiveRestartEnable = desc.primitive_restart;

    // viewport / scissor 改为 dynamic state，避免 swapchain 重建时重建 pipeline
    VkPipelineViewportStateCreateInfo vp = {};
    vp.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.scissorCount  = 1;

    VkPipelineRasterizationStateCreateInfo rs = {};
    rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode    = VK_CULL_MODE_NONE;
    rs.frontFace   = VK_FRONT_FACE_CLOCKWISE;
    rs.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms = {};
    ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blend = {};
    blend.colorWriteMask =
          VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
        | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blend.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo cb = {};
    cb.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments    = &blend;

    VkDynamicState dyn_states[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dyn = {};
    dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates    = dyn_states;

    VkPipelineLayoutCreateInfo lci = {};
    lci.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    lci.setLayoutCount = static_cast<uint32_t>(desc.descriptor_set_layouts.size());
    lci.pSetLayouts    = desc.descriptor_set_layouts.empty()
                       ? nullptr : desc.descriptor_set_layouts.data();
    VK_CHECK(vkCreatePipelineLayout(device_.handle(), &lci, nullptr, &layout_));

    VkGraphicsPipelineCreateInfo pci = {};
    pci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pci.stageCount          = 2;
    pci.pStages             = stages;
    pci.pVertexInputState   = &vi;
    pci.pInputAssemblyState = &ia;
    pci.pViewportState      = &vp;
    pci.pRasterizationState = &rs;
    pci.pMultisampleState   = &ms;
    pci.pColorBlendState    = &cb;
    pci.pDynamicState       = &dyn;
    pci.layout              = layout_;
    pci.renderPass          = render_pass.handle();
    pci.subpass             = 0;

    VK_CHECK(vkCreateGraphicsPipelines(device_.handle(), VK_NULL_HANDLE,
                                       1, &pci, nullptr, &pipeline_));
    LOG_INFO("[GraphicsPipeline] created (topology=" << desc.topology
             << ", restart=" << desc.primitive_restart
             << ", set_layouts=" << desc.descriptor_set_layouts.size() << ")");
}

GraphicsPipeline::~GraphicsPipeline() {
    if (pipeline_ != VK_NULL_HANDLE) vkDestroyPipeline(device_.handle(), pipeline_, nullptr);
    if (layout_   != VK_NULL_HANDLE) vkDestroyPipelineLayout(device_.handle(), layout_, nullptr);
}

}  // namespace routes_label::rhi
