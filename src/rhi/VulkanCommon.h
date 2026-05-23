#pragma once
//
// VulkanCommon.h
// RHI 层公共头：Vulkan 头文件、VK_CHECK 宏、错误码字符串化。
//

#include <vulkan/vulkan.h>

#include <stdexcept>
#include <string>

namespace routes_label::rhi {

// VkResult → 简短字符串（人类可读）
const char* result_to_string(VkResult r);

}  // namespace routes_label::rhi

// 全工程统一的 vk 调用错误检查入口。
#define VK_CHECK(expr)                                                                      \
    do {                                                                                    \
        VkResult _r = (expr);                                                               \
        if (_r != VK_SUCCESS) {                                                             \
            throw std::runtime_error(std::string(#expr " failed: ")                         \
                                     + ::routes_label::rhi::result_to_string(_r));          \
        }                                                                                   \
    } while (0)
