#include "rhi/Framebuffer.h"

#include "rhi/Device.h"
#include "rhi/RenderPass.h"
#include "utils/Log.h"

namespace routes_label::rhi {

Framebuffers::Framebuffers(const Device& device,
                           const RenderPass& render_pass,
                           const std::vector<VkImageView>& views,
                           VkExtent2D extent)
    : device_(device) {
    framebuffers_.resize(views.size(), VK_NULL_HANDLE);
    for (size_t i = 0; i < views.size(); ++i) {
        VkImageView attachments[1] = { views[i] };
        VkFramebufferCreateInfo ci = {};
        ci.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        ci.renderPass      = render_pass.handle();
        ci.attachmentCount = 1;
        ci.pAttachments    = attachments;
        ci.width           = extent.width;
        ci.height          = extent.height;
        ci.layers          = 1;
        VK_CHECK(vkCreateFramebuffer(device_.handle(), &ci, nullptr, &framebuffers_[i]));
    }
    LOG_INFO("[Framebuffers] created " << framebuffers_.size()
             << " (" << extent.width << "x" << extent.height << ")");
}

Framebuffers::~Framebuffers() {
    for (auto fb : framebuffers_) {
        if (fb != VK_NULL_HANDLE) vkDestroyFramebuffer(device_.handle(), fb, nullptr);
    }
}

}  // namespace routes_label::rhi
