#include "rhi/FrameSync.h"

#include "rhi/Device.h"
#include "utils/Log.h"

namespace routes_label::rhi {

FrameSync::FrameSync(const Device& device)
    : device_(device) {
    VkSemaphoreCreateInfo sci = {};
    sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fci = {};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;     // 第一帧不等

    for (auto& f : frames_) {
        VK_CHECK(vkCreateSemaphore(device_.handle(), &sci, nullptr, &f.image_available));
        VK_CHECK(vkCreateFence    (device_.handle(), &fci, nullptr, &f.in_flight));
    }
    LOG_INFO("[FrameSync] " << kMaxFramesInFlight << " frames in flight");
}

FrameSync::~FrameSync() {
    for (auto& f : frames_) {
        if (f.image_available) vkDestroySemaphore(device_.handle(), f.image_available, nullptr);
        if (f.in_flight)       vkDestroyFence    (device_.handle(), f.in_flight,       nullptr);
    }
}

}  // namespace routes_label::rhi
