#pragma once
//
// PhysicalDevice.h
// 选择合适的物理设备 + 缓存 queue family / swapchain support 信息。
//

#include "rhi/VulkanCommon.h"

#include <optional>
#include <vector>

namespace routes_label::rhi {

struct QueueFamilyIndices {
    std::optional<uint32_t> graphics;
    std::optional<uint32_t> present;
    std::optional<uint32_t> compute;   // 首期保留但不强制要求独立队列族

    bool complete() const { return graphics.has_value() && present.has_value(); }
};

struct SwapchainSupport {
    VkSurfaceCapabilitiesKHR        capabilities{};
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR>   present_modes;
};

class PhysicalDevice {
public:
    PhysicalDevice(VkInstance instance, VkSurfaceKHR surface);

    VkPhysicalDevice            handle() const          { return physical_; }
    const QueueFamilyIndices&   queue_indices() const   { return indices_; }
    SwapchainSupport            query_swapchain_support(VkSurfaceKHR surface) const;

    // 是否需要启用 VK_KHR_portability_subset（macOS / MoltenVK）
    bool                        requires_portability_subset() const { return needs_portability_; }

    const VkPhysicalDeviceProperties& properties() const { return properties_; }

private:
    VkPhysicalDevice           physical_           = VK_NULL_HANDLE;
    QueueFamilyIndices         indices_;
    VkPhysicalDeviceProperties properties_         = {};
    bool                       needs_portability_  = false;
};

}  // namespace routes_label::rhi
