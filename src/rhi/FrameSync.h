#pragma once
//
// FrameSync.h
// 双缓冲帧同步对象集合。每帧 1 个 fence + 2 个 semaphore，环形使用。
//

#include "rhi/VulkanCommon.h"

#include <array>

namespace routes_label::rhi {

class Device;

constexpr uint32_t kMaxFramesInFlight = 2;

struct FrameObjects {
    VkSemaphore image_available{ VK_NULL_HANDLE };  // 按 frame-in-flight 索引
    VkFence     in_flight{ VK_NULL_HANDLE };        // 按 frame-in-flight 索引
};

class FrameSync {
public:
    explicit FrameSync(const Device& device);
    ~FrameSync();

    FrameSync(const FrameSync&) = delete;
    FrameSync& operator=(const FrameSync&) = delete;

    const FrameObjects& current() const { return frames_[index_]; }
    void                advance()       { index_ = (index_ + 1) % kMaxFramesInFlight; }
    uint32_t            current_index() const { return index_; }

private:
    const Device&                                device_;
    std::array<FrameObjects, kMaxFramesInFlight> frames_{};
    uint32_t                                     index_ = 0;
};

}  // namespace routes_label::rhi
