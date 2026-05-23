#pragma once
//
// RenderPass.h
// 单 color attachment 的 RenderPass。
//

#include "rhi/VulkanCommon.h"

namespace routes_label::rhi {

class Device;

class RenderPass {
public:
    RenderPass(const Device& device, VkFormat color_format);
    ~RenderPass();

    RenderPass(const RenderPass&) = delete;
    RenderPass& operator=(const RenderPass&) = delete;

    VkRenderPass handle() const { return render_pass_; }

private:
    const Device& device_;
    VkRenderPass  render_pass_ = VK_NULL_HANDLE;
};

}  // namespace routes_label::rhi
