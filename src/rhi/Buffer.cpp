#include "rhi/Buffer.h"

#include "rhi/Device.h"
#include "rhi/PhysicalDevice.h"
#include "utils/Log.h"

#include <cstring>
#include <stdexcept>

namespace routes_label::rhi {

namespace {

uint32_t find_memory_type(VkPhysicalDevice phys,
                          uint32_t type_filter,
                          VkMemoryPropertyFlags required) {
    VkPhysicalDeviceMemoryProperties mp = {};
    vkGetPhysicalDeviceMemoryProperties(phys, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) {
        const bool type_ok    = (type_filter & (1u << i)) != 0;
        const bool prop_ok    = (mp.memoryTypes[i].propertyFlags & required) == required;
        if (type_ok && prop_ok) return i;
    }
    throw std::runtime_error("[Buffer] find_memory_type: no suitable memory type");
}

void create_raw_buffer(VkDevice              dev,
                       VkPhysicalDevice      phys,
                       VkDeviceSize          size,
                       VkBufferUsageFlags    usage,
                       VkMemoryPropertyFlags props,
                       VkBuffer&             out_buffer,
                       VkDeviceMemory&       out_memory) {
    VkBufferCreateInfo bi = {};
    bi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size        = size;
    bi.usage       = usage;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VK_CHECK(vkCreateBuffer(dev, &bi, nullptr, &out_buffer));

    VkMemoryRequirements mr = {};
    vkGetBufferMemoryRequirements(dev, out_buffer, &mr);

    VkMemoryAllocateInfo ai = {};
    ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize  = mr.size;
    ai.memoryTypeIndex = find_memory_type(phys, mr.memoryTypeBits, props);
    VK_CHECK(vkAllocateMemory(dev, &ai, nullptr, &out_memory));

    VK_CHECK(vkBindBufferMemory(dev, out_buffer, out_memory, 0));
}

// 在临时 transient command pool 上一次性 submit 一个 cmd 并等待完成。
// 用于资源 staging 上传，不与 RoutesRenderer 的 per-frame command pool 冲突。
void immediate_submit_copy(const Device& device,
                           VkBuffer      src,
                           VkBuffer      dst,
                           VkDeviceSize  size) {
    VkCommandPoolCreateInfo pci = {};
    pci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pci.queueFamilyIndex = device.graphics_family();
    pci.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    VkCommandPool pool = VK_NULL_HANDLE;
    VK_CHECK(vkCreateCommandPool(device.handle(), &pci, nullptr, &pool));

    VkCommandBufferAllocateInfo ai = {};
    ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool        = pool;
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VK_CHECK(vkAllocateCommandBuffers(device.handle(), &ai, &cmd));

    VkCommandBufferBeginInfo bi = {};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(cmd, &bi));

    VkBufferCopy region = {};
    region.size = size;
    vkCmdCopyBuffer(cmd, src, dst, 1, &region);

    VK_CHECK(vkEndCommandBuffer(cmd));

    VkSubmitInfo si = {};
    si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers    = &cmd;
    VK_CHECK(vkQueueSubmit(device.graphics_queue(), 1, &si, VK_NULL_HANDLE));
    VK_CHECK(vkQueueWaitIdle(device.graphics_queue()));

    vkFreeCommandBuffers(device.handle(), pool, 1, &cmd);
    vkDestroyCommandPool(device.handle(), pool, nullptr);
}

}  // namespace

// -----------------------------------------------------------------------------
// Buffer
// -----------------------------------------------------------------------------

Buffer::Buffer(const Device& device, VkBuffer buf, VkDeviceMemory mem,
               VkDeviceSize size, void* mapped)
    : device_(&device), buffer_(buf), memory_(mem), size_(size), mapped_(mapped) {}

Buffer::Buffer(Buffer&& other) noexcept
    : device_(other.device_),
      buffer_(other.buffer_),
      memory_(other.memory_),
      size_(other.size_),
      mapped_(other.mapped_) {
    other.device_ = nullptr;
    other.buffer_ = VK_NULL_HANDLE;
    other.memory_ = VK_NULL_HANDLE;
    other.size_   = 0;
    other.mapped_ = nullptr;
}

Buffer& Buffer::operator=(Buffer&& other) noexcept {
    if (this != &other) {
        destroy();
        device_  = other.device_;
        buffer_  = other.buffer_;
        memory_  = other.memory_;
        size_    = other.size_;
        mapped_  = other.mapped_;
        other.device_ = nullptr;
        other.buffer_ = VK_NULL_HANDLE;
        other.memory_ = VK_NULL_HANDLE;
        other.size_   = 0;
        other.mapped_ = nullptr;
    }
    return *this;
}

Buffer::~Buffer() {
    destroy();
}

void Buffer::destroy() noexcept {
    if (device_ == nullptr) return;
    VkDevice dev = device_->handle();
    if (mapped_ != nullptr && memory_ != VK_NULL_HANDLE) {
        vkUnmapMemory(dev, memory_);
        mapped_ = nullptr;
    }
    if (buffer_ != VK_NULL_HANDLE) vkDestroyBuffer(dev, buffer_, nullptr);
    if (memory_ != VK_NULL_HANDLE) vkFreeMemory(dev, memory_, nullptr);
    buffer_ = VK_NULL_HANDLE;
    memory_ = VK_NULL_HANDLE;
    size_   = 0;
    device_ = nullptr;
}

Buffer Buffer::CreateDeviceLocal(const Device&         device,
                                 const PhysicalDevice& physical,
                                 VkDeviceSize          size,
                                 VkBufferUsageFlags    usage,
                                 const void*           src_data) {
    if (size == 0) {
        throw std::runtime_error("[Buffer] CreateDeviceLocal: size must be > 0");
    }

    VkDevice         dev  = device.handle();
    VkPhysicalDevice phys = physical.handle();

    // 1. staging buffer: HOST_VISIBLE | HOST_COHERENT
    VkBuffer       staging_buf = VK_NULL_HANDLE;
    VkDeviceMemory staging_mem = VK_NULL_HANDLE;
    create_raw_buffer(dev, phys, size,
                      VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                          | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      staging_buf, staging_mem);

    if (src_data != nullptr) {
        void* mapped = nullptr;
        VK_CHECK(vkMapMemory(dev, staging_mem, 0, size, 0, &mapped));
        std::memcpy(mapped, src_data, static_cast<size_t>(size));
        vkUnmapMemory(dev, staging_mem);
    }

    // 2. device-local buffer
    VkBuffer       device_buf = VK_NULL_HANDLE;
    VkDeviceMemory device_mem = VK_NULL_HANDLE;
    create_raw_buffer(dev, phys, size,
                      VK_BUFFER_USAGE_TRANSFER_DST_BIT | usage,
                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                      device_buf, device_mem);

    // 3. immediate submit staging → device copy
    immediate_submit_copy(device, staging_buf, device_buf, size);

    // 4. cleanup staging
    vkDestroyBuffer(dev, staging_buf, nullptr);
    vkFreeMemory(dev, staging_mem, nullptr);

    LOG_INFO("[Buffer] device-local " << size << " bytes (usage=0x"
             << std::hex << usage << std::dec << ")");
    return Buffer(device, device_buf, device_mem, size, /*mapped=*/nullptr);
}

Buffer Buffer::CreateHostVisible(const Device&         device,
                                 const PhysicalDevice& physical,
                                 VkDeviceSize          size,
                                 VkBufferUsageFlags    usage) {
    if (size == 0) {
        throw std::runtime_error("[Buffer] CreateHostVisible: size must be > 0");
    }

    VkBuffer       buf = VK_NULL_HANDLE;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    create_raw_buffer(device.handle(), physical.handle(), size, usage,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                          | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      buf, mem);

    void* mapped = nullptr;
    VK_CHECK(vkMapMemory(device.handle(), mem, 0, size, 0, &mapped));

    LOG_INFO("[Buffer] host-visible " << size << " bytes (persistently mapped, usage=0x"
             << std::hex << usage << std::dec << ")");
    return Buffer(device, buf, mem, size, mapped);
}

}  // namespace routes_label::rhi
