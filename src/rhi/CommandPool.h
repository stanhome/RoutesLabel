#pragma once
//
// CommandPool.h
// CommandPool + 批量分配 CommandBuffer。
//

#include "rhi/VulkanCommon.h"

#include <vector>

namespace routes_label::rhi {

class Device;

class CommandPool {
public:
    CommandPool(const Device& device, uint32_t queue_family);
    ~CommandPool();

    CommandPool(const CommandPool&) = delete;
    CommandPool& operator=(const CommandPool&) = delete;

    VkCommandPool handle() const { return pool_; }

    std::vector<VkCommandBuffer> allocate_primary(uint32_t count) const;

private:
    const Device& device_;
    VkCommandPool pool_ = VK_NULL_HANDLE;
};

}  // namespace routes_label::rhi
