#pragma once
//
// Swapchain.h
// VkSwapchainKHR + image views 集合的 RAII 封装。
// 提供 recreate() 用于窗口尺寸变化。
//

#include "rhi/PhysicalDevice.h"
#include "rhi/VulkanCommon.h"

#include <vector>

namespace routes_label::rhi {

class Device;

class Swapchain {
public:
    Swapchain(const PhysicalDevice& physical,
              const Device& device,
              VkSurfaceKHR surface,
              uint32_t fb_width, uint32_t fb_height);
    ~Swapchain();

    Swapchain(const Swapchain&) = delete;
    Swapchain& operator=(const Swapchain&) = delete;

    void recreate(uint32_t fb_width, uint32_t fb_height);

    VkSwapchainKHR    handle() const          { return swapchain_; }
    VkFormat          image_format() const    { return image_format_; }
    VkExtent2D        extent() const          { return extent_; }
    uint32_t          image_count() const     { return static_cast<uint32_t>(images_.size()); }
    const std::vector<VkImageView>& image_views() const { return image_views_; }

private:
    void create(uint32_t fb_width, uint32_t fb_height);
    void destroy();

    const PhysicalDevice& physical_;
    const Device&         device_;
    VkSurfaceKHR          surface_      = VK_NULL_HANDLE;

    VkSwapchainKHR           swapchain_    = VK_NULL_HANDLE;
    VkFormat                 image_format_ = VK_FORMAT_UNDEFINED;
    VkExtent2D               extent_       = {0, 0};
    std::vector<VkImage>     images_;
    std::vector<VkImageView> image_views_;
};

}  // namespace routes_label::rhi
