#include "rhi/Swapchain.h"

#include "rhi/Device.h"
#include "utils/Log.h"

#include <algorithm>
#include <cstdint>
#include <stdexcept>

namespace routes_label::rhi {

namespace {

VkSurfaceFormatKHR choose_surface_format(const std::vector<VkSurfaceFormatKHR>& available) {
    for (const auto& f : available) {
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return f;
        }
    }
    return available.empty() ? VkSurfaceFormatKHR{} : available[0];
}

VkPresentModeKHR choose_present_mode(const std::vector<VkPresentModeKHR>& available) {
    // 不限帧策略：
    //   1) IMMEDIATE：把已渲染图像立即送显示器，撕裂但完全不限帧（GPU 跑多快显示多快）；
    //   2) MAILBOX：三缓冲，过期帧自动丢弃，无撕裂但仍受 vblank 节奏（macOS/MoltenVK 不一定支持）；
    //   3) FIFO：标准 vsync，等 vblank，必然限帧到刷新率，作为兜底。
    // RoutesLabel 是性能基准 demo，要"跑出最高性能"，因此 IMMEDIATE 优先。
    for (auto m : available) {
        if (m == VK_PRESENT_MODE_IMMEDIATE_KHR) return m;
    }
    for (auto m : available) {
        if (m == VK_PRESENT_MODE_MAILBOX_KHR) return m;
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D choose_extent(const VkSurfaceCapabilitiesKHR& caps,
                         uint32_t fb_w, uint32_t fb_h) {
    if (caps.currentExtent.width != UINT32_MAX) {
        return caps.currentExtent;
    }
    VkExtent2D actual{ fb_w, fb_h };
    actual.width  = std::clamp(actual.width,  caps.minImageExtent.width,  caps.maxImageExtent.width);
    actual.height = std::clamp(actual.height, caps.minImageExtent.height, caps.maxImageExtent.height);
    return actual;
}

}  // namespace

Swapchain::Swapchain(const PhysicalDevice& physical,
                     const Device& device,
                     VkSurfaceKHR surface,
                     uint32_t fb_width, uint32_t fb_height)
    : physical_(physical), device_(device), surface_(surface) {
    create(fb_width, fb_height);
}

Swapchain::~Swapchain() {
    destroy();
}

void Swapchain::recreate(uint32_t fb_width, uint32_t fb_height) {
    device_.wait_idle();
    destroy();
    create(fb_width, fb_height);
}

void Swapchain::create(uint32_t fb_width, uint32_t fb_height) {
    SwapchainSupport sup = physical_.query_swapchain_support(surface_);

    auto fmt        = choose_surface_format(sup.formats);
    auto pm         = choose_present_mode(sup.present_modes);
    auto ext        = choose_extent(sup.capabilities, fb_width, fb_height);

    uint32_t image_count = sup.capabilities.minImageCount + 1;
    if (sup.capabilities.maxImageCount > 0 &&
        image_count > sup.capabilities.maxImageCount) {
        image_count = sup.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR ci = {};
    ci.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface          = surface_;
    ci.minImageCount    = image_count;
    ci.imageFormat      = fmt.format;
    ci.imageColorSpace  = fmt.colorSpace;
    ci.imageExtent      = ext;
    ci.imageArrayLayers = 1;
    ci.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    uint32_t gfx = device_.graphics_family();
    uint32_t prs = device_.present_family();
    uint32_t fams[2] = { gfx, prs };
    if (gfx != prs) {
        ci.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
        ci.queueFamilyIndexCount = 2;
        ci.pQueueFamilyIndices   = fams;
    } else {
        ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    ci.preTransform   = sup.capabilities.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode    = pm;
    ci.clipped        = VK_TRUE;
    ci.oldSwapchain   = VK_NULL_HANDLE;

    VK_CHECK(vkCreateSwapchainKHR(device_.handle(), &ci, nullptr, &swapchain_));

    uint32_t got = 0;
    vkGetSwapchainImagesKHR(device_.handle(), swapchain_, &got, nullptr);
    images_.resize(got);
    vkGetSwapchainImagesKHR(device_.handle(), swapchain_, &got, images_.data());

    image_format_ = fmt.format;
    extent_       = ext;

    image_views_.resize(images_.size());
    for (size_t i = 0; i < images_.size(); ++i) {
        VkImageViewCreateInfo iv = {};
        iv.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        iv.image    = images_[i];
        iv.viewType = VK_IMAGE_VIEW_TYPE_2D;
        iv.format   = image_format_;
        iv.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        iv.subresourceRange.baseMipLevel   = 0;
        iv.subresourceRange.levelCount     = 1;
        iv.subresourceRange.baseArrayLayer = 0;
        iv.subresourceRange.layerCount     = 1;
        VK_CHECK(vkCreateImageView(device_.handle(), &iv, nullptr, &image_views_[i]));
    }

    LOG_INFO("[Swapchain] created " << ext.width << "x" << ext.height
             << " format=" << image_format_
             << " images=" << images_.size()
             << " present=" << pm);
}

void Swapchain::destroy() {
    for (auto v : image_views_) {
        if (v != VK_NULL_HANDLE) vkDestroyImageView(device_.handle(), v, nullptr);
    }
    image_views_.clear();
    images_.clear();

    if (swapchain_ != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device_.handle(), swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }
}

}  // namespace routes_label::rhi
