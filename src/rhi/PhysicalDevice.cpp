#include "rhi/PhysicalDevice.h"

#include "utils/Log.h"

#include <cstring>
#include <stdexcept>

namespace routes_label::rhi {

namespace {

QueueFamilyIndices find_queue_families(VkPhysicalDevice dev, VkSurfaceKHR surface) {
    QueueFamilyIndices idx;
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &count, families.data());

    for (uint32_t i = 0; i < count; ++i) {
        if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            idx.graphics = i;
        }
        if (families[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
            idx.compute = i;
        }
        VkBool32 present_support = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, surface, &present_support);
        if (present_support) {
            idx.present = i;
        }
        if (idx.complete()) break;
    }
    return idx;
}

bool has_device_extension(VkPhysicalDevice dev, const char* name) {
    uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(dev, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> exts(count);
    vkEnumerateDeviceExtensionProperties(dev, nullptr, &count, exts.data());
    for (const auto& e : exts) {
        if (std::strcmp(e.extensionName, name) == 0) return true;
    }
    return false;
}

bool supports_swapchain(VkPhysicalDevice dev, VkSurfaceKHR surface) {
    if (!has_device_extension(dev, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) return false;
    uint32_t fmt_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(dev, surface, &fmt_count, nullptr);
    uint32_t pm_count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(dev, surface, &pm_count, nullptr);
    return fmt_count > 0 && pm_count > 0;
}

int score_device(VkPhysicalDevice dev,
                 VkSurfaceKHR     surface,
                 QueueFamilyIndices& out_idx,
                 bool& out_needs_portability,
                 VkPhysicalDeviceProperties& out_props) {
    vkGetPhysicalDeviceProperties(dev, &out_props);
    out_idx = find_queue_families(dev, surface);
    if (!out_idx.complete()) return -1;
    if (!supports_swapchain(dev, surface)) return -1;

    out_needs_portability = has_device_extension(dev, "VK_KHR_portability_subset");

    int score = 0;
    if (out_props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) score += 1000;
    else if (out_props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) score += 500;
    score += static_cast<int>(out_props.limits.maxImageDimension2D / 1024);
    return score;
}

}  // namespace

PhysicalDevice::PhysicalDevice(VkInstance instance, VkSurfaceKHR surface) {
    uint32_t count = 0;
    VK_CHECK(vkEnumeratePhysicalDevices(instance, &count, nullptr));
    if (count == 0) {
        throw std::runtime_error("No Vulkan-capable physical device found");
    }
    std::vector<VkPhysicalDevice> devices(count);
    VK_CHECK(vkEnumeratePhysicalDevices(instance, &count, devices.data()));

    int best_score = -1;
    for (auto d : devices) {
        QueueFamilyIndices idx;
        bool               needs_port = false;
        VkPhysicalDeviceProperties p{};
        int s = score_device(d, surface, idx, needs_port, p);
        LOG_INFO("[PhysicalDevice] candidate '" << p.deviceName
                 << "' type=" << p.deviceType << " score=" << s);
        if (s > best_score) {
            best_score          = s;
            physical_           = d;
            indices_            = idx;
            properties_         = p;
            needs_portability_  = needs_port;
        }
    }

    if (physical_ == VK_NULL_HANDLE || best_score < 0) {
        throw std::runtime_error("No suitable physical device with required queues + swapchain");
    }
    LOG_INFO("[PhysicalDevice] selected '" << properties_.deviceName
             << "' (graphics=" << *indices_.graphics
             << " present=" << *indices_.present
             << " compute=" << (indices_.compute ? std::to_string(*indices_.compute) : std::string("-"))
             << " portability_subset=" << (needs_portability_ ? "yes" : "no") << ")");
}

SwapchainSupport PhysicalDevice::query_swapchain_support(VkSurfaceKHR surface) const {
    SwapchainSupport sup;
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_, surface, &sup.capabilities));

    uint32_t fmt_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface, &fmt_count, nullptr);
    sup.formats.resize(fmt_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface, &fmt_count, sup.formats.data());

    uint32_t pm_count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_, surface, &pm_count, nullptr);
    sup.present_modes.resize(pm_count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_, surface, &pm_count, sup.present_modes.data());

    return sup;
}

}  // namespace routes_label::rhi
