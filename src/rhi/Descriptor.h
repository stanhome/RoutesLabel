#pragma once
//
// rhi/Descriptor.h
// DescriptorSetLayout / DescriptorPool / DescriptorSets RAII 三件套。
//
// 设计：
//   - DescriptorSetLayout: 由 binding 描述列表构造，本工程首期只用 UBO。
//   - DescriptorPool:      固定容量，按 pool sizes 预分配。
//   - DescriptorSets:      从 pool + layout 一次性分配 N 个集合，提供 update_uniform_buffer 便捷接口。
//

#include "rhi/VulkanCommon.h"

#include <vector>

namespace routes_label::rhi {

class Device;

class DescriptorSetLayout {
public:
    DescriptorSetLayout(const Device& device,
                        const std::vector<VkDescriptorSetLayoutBinding>& bindings);
    ~DescriptorSetLayout();

    DescriptorSetLayout(const DescriptorSetLayout&)            = delete;
    DescriptorSetLayout& operator=(const DescriptorSetLayout&) = delete;

    VkDescriptorSetLayout handle() const { return layout_; }

private:
    const Device&         device_;
    VkDescriptorSetLayout layout_ = VK_NULL_HANDLE;
};

class DescriptorPool {
public:
    DescriptorPool(const Device&                              device,
                   uint32_t                                   max_sets,
                   const std::vector<VkDescriptorPoolSize>&   pool_sizes);
    ~DescriptorPool();

    DescriptorPool(const DescriptorPool&)            = delete;
    DescriptorPool& operator=(const DescriptorPool&) = delete;

    VkDescriptorPool handle() const { return pool_; }

private:
    const Device&    device_;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
};

class DescriptorSets {
public:
    DescriptorSets(const Device&              device,
                   const DescriptorPool&      pool,
                   const DescriptorSetLayout& layout,
                   uint32_t                   count);
    ~DescriptorSets() = default;  // descriptor sets 由 pool 统一回收，无需单独 free

    DescriptorSets(const DescriptorSets&)            = delete;
    DescriptorSets& operator=(const DescriptorSets&) = delete;

    VkDescriptorSet at(uint32_t i) const { return sets_[i]; }
    const std::vector<VkDescriptorSet>& all() const { return sets_; }

    // 把第 set_index 个 set 的 binding 绑定到一段 VkBuffer 区间（UBO 用）。
    void update_uniform_buffer(uint32_t set_index,
                               uint32_t binding,
                               VkBuffer buffer,
                               VkDeviceSize offset,
                               VkDeviceSize range) const;

private:
    const Device&                device_;
    std::vector<VkDescriptorSet> sets_;
};

}  // namespace routes_label::rhi
