#include "rhi/CommandPool.h"

#include "rhi/Device.h"
#include "utils/Log.h"

namespace routes_label::rhi {

CommandPool::CommandPool(const Device& device, uint32_t queue_family)
    : device_(device) {
    VkCommandPoolCreateInfo ci = {};
    ci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    ci.queueFamilyIndex = queue_family;
    ci.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VK_CHECK(vkCreateCommandPool(device_.handle(), &ci, nullptr, &pool_));
    LOG_INFO("[CommandPool] created (family=" << queue_family << ")");
}

CommandPool::~CommandPool() {
    if (pool_ != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device_.handle(), pool_, nullptr);
    }
}

std::vector<VkCommandBuffer> CommandPool::allocate_primary(uint32_t count) const {
    std::vector<VkCommandBuffer> bufs(count, VK_NULL_HANDLE);
    VkCommandBufferAllocateInfo ai = {};
    ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool        = pool_;
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = count;
    VK_CHECK(vkAllocateCommandBuffers(device_.handle(), &ai, bufs.data()));
    return bufs;
}

}  // namespace routes_label::rhi
