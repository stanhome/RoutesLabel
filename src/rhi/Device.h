#pragma once
//
// Device.h
// 逻辑设备 + 队列句柄持有。
//

#include "rhi/PhysicalDevice.h"
#include "rhi/VulkanCommon.h"

#include <optional>

namespace routes_label::rhi {

class Device {
public:
    Device(const PhysicalDevice& physical, bool enable_validation);
    ~Device();

    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;

    VkDevice  handle() const         { return device_; }
    VkQueue   graphics_queue() const { return graphics_queue_; }
    VkQueue   present_queue() const  { return present_queue_; }
    VkQueue   compute_queue() const  { return compute_queue_; }

    uint32_t  graphics_family() const { return graphics_family_; }
    uint32_t  present_family()  const { return present_family_; }
    std::optional<uint32_t> compute_family() const { return compute_family_; }

    void wait_idle() const { vkDeviceWaitIdle(device_); }

private:
    VkDevice                 device_           = VK_NULL_HANDLE;
    VkQueue                  graphics_queue_   = VK_NULL_HANDLE;
    VkQueue                  present_queue_    = VK_NULL_HANDLE;
    VkQueue                  compute_queue_    = VK_NULL_HANDLE;

    uint32_t                 graphics_family_  = 0;
    uint32_t                 present_family_   = 0;
    std::optional<uint32_t>  compute_family_;
};

}  // namespace routes_label::rhi
