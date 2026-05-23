#include "rhi/Device.h"

#include "utils/Log.h"

#include <set>
#include <vector>

namespace routes_label::rhi {

Device::Device(const PhysicalDevice& physical, bool enable_validation) {
    (void)enable_validation;  // 现代 Vulkan 已忽略 device-level layer，此参数仅为兼容历史接口
    const auto& idx = physical.queue_indices();
    graphics_family_ = *idx.graphics;
    present_family_  = *idx.present;
    compute_family_  = idx.compute;

    std::set<uint32_t> unique_families = { graphics_family_, present_family_ };
    if (compute_family_) unique_families.insert(*compute_family_);

    float priority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> queue_cis;
    queue_cis.reserve(unique_families.size());
    for (uint32_t fam : unique_families) {
        VkDeviceQueueCreateInfo qci = {};
        qci.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qci.queueFamilyIndex = fam;
        qci.queueCount       = 1;
        qci.pQueuePriorities = &priority;
        queue_cis.push_back(qci);
    }

    std::vector<const char*> device_exts = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    };
    if (physical.requires_portability_subset()) {
        device_exts.push_back("VK_KHR_portability_subset");
    }

    VkPhysicalDeviceFeatures features = {};

    VkDeviceCreateInfo ci = {};
    ci.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    ci.queueCreateInfoCount    = static_cast<uint32_t>(queue_cis.size());
    ci.pQueueCreateInfos       = queue_cis.data();
    ci.enabledExtensionCount   = static_cast<uint32_t>(device_exts.size());
    ci.ppEnabledExtensionNames = device_exts.data();
    ci.pEnabledFeatures        = &features;

    // 注意：vkCreateDevice 的 enabledLayerCount 在现代 Vulkan 必须为 0。
    // device-level layer 自 Vulkan 1.0 起就被废弃，仅 instance layer 生效。

    VK_CHECK(vkCreateDevice(physical.handle(), &ci, nullptr, &device_));

    vkGetDeviceQueue(device_, graphics_family_, 0, &graphics_queue_);
    vkGetDeviceQueue(device_, present_family_,  0, &present_queue_);
    if (compute_family_) {
        vkGetDeviceQueue(device_, *compute_family_, 0, &compute_queue_);
    }

    LOG_INFO("[Device] VkDevice created (queues: graphics+present"
             << (compute_family_ ? " +compute" : "") << ")");
}

Device::~Device() {
    if (device_ != VK_NULL_HANDLE) {
        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
    }
}

}  // namespace routes_label::rhi
