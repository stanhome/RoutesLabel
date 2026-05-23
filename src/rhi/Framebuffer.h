#pragma once
//
// Framebuffer.h
// 一组与 swapchain image views 一一对应的 VkFramebuffer。
//

#include "rhi/VulkanCommon.h"

#include <vector>

namespace routes_label::rhi {

class Device;
class RenderPass;

class Framebuffers {
public:
    Framebuffers(const Device& device,
                 const RenderPass& render_pass,
                 const std::vector<VkImageView>& views,
                 VkExtent2D extent);
    ~Framebuffers();

    Framebuffers(const Framebuffers&) = delete;
    Framebuffers& operator=(const Framebuffers&) = delete;

    const std::vector<VkFramebuffer>& handles() const { return framebuffers_; }

private:
    const Device&              device_;
    std::vector<VkFramebuffer> framebuffers_;
};

}  // namespace routes_label::rhi
