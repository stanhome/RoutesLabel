#pragma once
//
// rhi/Buffer.h
// 通用 VkBuffer + VkDeviceMemory RAII 封装。提供两类工厂：
//   - CreateDeviceLocal：内部走 staging buffer 一次性上传到 DEVICE_LOCAL（常驻 VRAM）。
//   - CreateHostVisible：HOST_VISIBLE | HOST_COHERENT，构造期持久 vkMapMemory，draw_frame 直接 memcpy。
//
// 设计要点：
//   - Buffer 自管 VkDeviceMemory（不引入更复杂的 allocator，本工程数据量极小）；
//   - DeviceLocal 的 staging upload 在工厂内自建临时 transient command pool + immediate submit & wait，
//     避免占用 RoutesRenderer 的 graphics command pool 与帧 command buffer。
//

#include "rhi/VulkanCommon.h"

namespace routes_label::rhi {

class Device;
class PhysicalDevice;

class Buffer {
public:
    // 用 device-local 内存 + staging copy 上传初始数据。
    // src_data 可以为 nullptr（仅分配 GPU 内存，自行 vkCmdCopyBuffer）。
    static Buffer CreateDeviceLocal(const Device&         device,
                                    const PhysicalDevice& physical,
                                    VkDeviceSize          size,
                                    VkBufferUsageFlags    usage,
                                    const void*           src_data);

    // 用 host-visible | host-coherent 内存（持久 map）。
    // 构造完成后即可通过 mapped() 直接 memcpy。
    static Buffer CreateHostVisible(const Device&         device,
                                    const PhysicalDevice& physical,
                                    VkDeviceSize          size,
                                    VkBufferUsageFlags    usage);

    Buffer() = default;
    ~Buffer();

    Buffer(Buffer&& other) noexcept;
    Buffer& operator=(Buffer&& other) noexcept;

    Buffer(const Buffer&)            = delete;
    Buffer& operator=(const Buffer&) = delete;

    VkBuffer        handle() const { return buffer_; }
    VkDeviceMemory  memory() const { return memory_; }
    VkDeviceSize    size()   const { return size_;   }
    void*           mapped() const { return mapped_; }

private:
    Buffer(const Device& device, VkBuffer buf, VkDeviceMemory mem,
           VkDeviceSize size, void* mapped);

    void destroy() noexcept;

    const Device*  device_ = nullptr;
    VkBuffer       buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    VkDeviceSize   size_   = 0;
    void*          mapped_ = nullptr;  // 仅 HostVisible 工厂会设置
};

}  // namespace routes_label::rhi
