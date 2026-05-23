#include "rhi/Descriptor.h"

#include "rhi/Device.h"
#include "utils/Log.h"

namespace routes_label::rhi {

// -----------------------------------------------------------------------------
// DescriptorSetLayout
// -----------------------------------------------------------------------------
DescriptorSetLayout::DescriptorSetLayout(
    const Device& device,
    const std::vector<VkDescriptorSetLayoutBinding>& bindings)
    : device_(device) {
    VkDescriptorSetLayoutCreateInfo ci = {};
    ci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ci.bindingCount = static_cast<uint32_t>(bindings.size());
    ci.pBindings    = bindings.data();
    VK_CHECK(vkCreateDescriptorSetLayout(device_.handle(), &ci, nullptr, &layout_));
    LOG_INFO("[DescriptorSetLayout] created (" << bindings.size() << " bindings)");
}

DescriptorSetLayout::~DescriptorSetLayout() {
    if (layout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device_.handle(), layout_, nullptr);
    }
}

// -----------------------------------------------------------------------------
// DescriptorPool
// -----------------------------------------------------------------------------
DescriptorPool::DescriptorPool(const Device&                            device,
                               uint32_t                                 max_sets,
                               const std::vector<VkDescriptorPoolSize>& pool_sizes)
    : device_(device) {
    VkDescriptorPoolCreateInfo ci = {};
    ci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    ci.maxSets       = max_sets;
    ci.poolSizeCount = static_cast<uint32_t>(pool_sizes.size());
    ci.pPoolSizes    = pool_sizes.data();
    VK_CHECK(vkCreateDescriptorPool(device_.handle(), &ci, nullptr, &pool_));
    LOG_INFO("[DescriptorPool] created (max_sets=" << max_sets << ")");
}

DescriptorPool::~DescriptorPool() {
    if (pool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_.handle(), pool_, nullptr);
    }
}

// -----------------------------------------------------------------------------
// DescriptorSets
// -----------------------------------------------------------------------------
DescriptorSets::DescriptorSets(const Device&              device,
                               const DescriptorPool&      pool,
                               const DescriptorSetLayout& layout,
                               uint32_t                   count)
    : device_(device) {
    std::vector<VkDescriptorSetLayout> layouts(count, layout.handle());

    VkDescriptorSetAllocateInfo ai = {};
    ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool     = pool.handle();
    ai.descriptorSetCount = count;
    ai.pSetLayouts        = layouts.data();

    sets_.resize(count, VK_NULL_HANDLE);
    VK_CHECK(vkAllocateDescriptorSets(device_.handle(), &ai, sets_.data()));
    LOG_INFO("[DescriptorSets] allocated " << count << " sets");
}

void DescriptorSets::update_uniform_buffer(uint32_t set_index,
                                           uint32_t binding,
                                           VkBuffer buffer,
                                           VkDeviceSize offset,
                                           VkDeviceSize range) const {
    VkDescriptorBufferInfo bi = {};
    bi.buffer = buffer;
    bi.offset = offset;
    bi.range  = range;

    VkWriteDescriptorSet w = {};
    w.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.dstSet          = sets_[set_index];
    w.dstBinding      = binding;
    w.dstArrayElement = 0;
    w.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    w.descriptorCount = 1;
    w.pBufferInfo     = &bi;

    vkUpdateDescriptorSets(device_.handle(), 1, &w, 0, nullptr);
}

void DescriptorSets::update_storage_buffer(uint32_t set_index,
                                           uint32_t binding,
                                           VkBuffer buffer,
                                           VkDeviceSize offset,
                                           VkDeviceSize range) const {
    VkDescriptorBufferInfo bi = {};
    bi.buffer = buffer;
    bi.offset = offset;
    bi.range  = range;

    VkWriteDescriptorSet w = {};
    w.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.dstSet          = sets_[set_index];
    w.dstBinding      = binding;
    w.dstArrayElement = 0;
    w.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    w.descriptorCount = 1;
    w.pBufferInfo     = &bi;

    vkUpdateDescriptorSets(device_.handle(), 1, &w, 0, nullptr);
}

}  // namespace routes_label::rhi
