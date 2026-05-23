#pragma once
//
// Instance.h
// VkInstance + Debug Messenger 的 RAII 封装。
// macOS 必须启用 portability_enumeration 扩展与 ENUMERATE_PORTABILITY_BIT。
//

#include "rhi/VulkanCommon.h"

#include <vector>

namespace routes_label::rhi {

class Instance {
public:
    // app_name: 显示在 vk loader / 工具中
    // required_window_extensions: 由 platform::Window::required_instance_extensions() 提供
    Instance(const char* app_name,
             const char** required_window_extensions,
             uint32_t     required_window_extension_count,
             bool         enable_validation);

    ~Instance();

    Instance(const Instance&) = delete;
    Instance& operator=(const Instance&) = delete;

    VkInstance handle() const { return instance_; }
    bool       validation_enabled() const { return validation_enabled_; }

private:
    void create_instance(const char* app_name,
                         const char** required_window_extensions,
                         uint32_t required_window_extension_count);
    void setup_debug_messenger();
    void destroy_debug_messenger();

    VkInstance               instance_           = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debug_messenger_    = VK_NULL_HANDLE;
    bool                     validation_enabled_ = false;
    std::vector<const char*> enabled_layers_;
};

}  // namespace routes_label::rhi
