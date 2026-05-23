#include "rhi/Instance.h"

#include "utils/Log.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace routes_label::rhi {

namespace {

constexpr const char* kValidationLayer = "VK_LAYER_KHRONOS_validation";

bool has_layer(const char* name) {
    uint32_t count = 0;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> layers(count);
    vkEnumerateInstanceLayerProperties(&count, layers.data());
    for (const auto& l : layers) {
        if (std::strcmp(l.layerName, name) == 0) return true;
    }
    return false;
}

bool has_instance_extension(const char* name) {
    uint32_t count = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> exts(count);
    vkEnumerateInstanceExtensionProperties(nullptr, &count, exts.data());
    for (const auto& e : exts) {
        if (std::strcmp(e.extensionName, name) == 0) return true;
    }
    return false;
}

VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
        VkDebugUtilsMessageSeverityFlagBitsEXT      severity,
        VkDebugUtilsMessageTypeFlagsEXT             /*type*/,
        const VkDebugUtilsMessengerCallbackDataEXT* data,
        void*                                       /*user*/) {
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        LOG_ERROR("[Vulkan] " << data->pMessage);
    } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        LOG_WARN("[Vulkan] " << data->pMessage);
    } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
        // INFO 太啰嗦，改 VERBOSE 处理
        LOG_INFO("[Vulkan] " << data->pMessage);
    }
    return VK_FALSE;
}

void fill_messenger_ci(VkDebugUtilsMessengerCreateInfoEXT& ci) {
    ci = {};
    ci.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    ci.messageSeverity =
          VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
        | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    ci.messageType =
          VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
        | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
        | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    ci.pfnUserCallback = debug_callback;
}

}  // namespace

Instance::Instance(const char* app_name,
                   const char** required_window_extensions,
                   uint32_t     required_window_extension_count,
                   bool         enable_validation) {
    if (enable_validation) {
        if (has_layer(kValidationLayer)) {
            enabled_layers_.push_back(kValidationLayer);
            validation_enabled_ = true;
            LOG_INFO("[Instance] validation layer enabled: " << kValidationLayer);
        } else {
            LOG_WARN("[Instance] validation requested but " << kValidationLayer
                     << " not available; continuing without validation");
        }
    }

    create_instance(app_name,
                    required_window_extensions,
                    required_window_extension_count);

    if (validation_enabled_) {
        setup_debug_messenger();
    }
}

Instance::~Instance() {
    if (debug_messenger_ != VK_NULL_HANDLE) {
        destroy_debug_messenger();
    }
    if (instance_ != VK_NULL_HANDLE) {
        vkDestroyInstance(instance_, nullptr);
        instance_ = VK_NULL_HANDLE;
    }
}

void Instance::create_instance(const char* app_name,
                               const char** required_window_extensions,
                               uint32_t required_window_extension_count) {
    // 收集需要的扩展
    std::vector<const char*> extensions(
        required_window_extensions,
        required_window_extensions + required_window_extension_count);

    if (validation_enabled_) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    // macOS（MoltenVK）必须开启 portability_enumeration 扩展
    bool portability_enabled = false;
#if defined(__APPLE__)
    if (has_instance_extension(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
        extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        // 该扩展依赖于 KHR_get_physical_device_properties2（Vulkan 1.1+ 已合入核心，
        // 但保险起见在某些老 SDK 下仍需显式启用）
        if (has_instance_extension("VK_KHR_get_physical_device_properties2")) {
            extensions.push_back("VK_KHR_get_physical_device_properties2");
        }
        portability_enabled = true;
    }
#endif

    // 去重（required_window_extensions 可能与上面手动加的有重叠）
    std::sort(extensions.begin(), extensions.end(),
              [](const char* a, const char* b) { return std::strcmp(a, b) < 0; });
    extensions.erase(std::unique(extensions.begin(), extensions.end(),
                                 [](const char* a, const char* b) {
                                     return std::strcmp(a, b) == 0;
                                 }),
                     extensions.end());

    LOG_INFO("[Instance] enabled extensions:");
    for (auto* e : extensions) LOG_INFO("  - " << e);

    VkApplicationInfo ai = {};
    ai.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    ai.pApplicationName   = app_name;
    ai.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    ai.pEngineName        = "RoutesLabel";
    ai.engineVersion      = VK_MAKE_VERSION(0, 1, 0);
    ai.apiVersion         = VK_API_VERSION_1_2;

    VkInstanceCreateInfo ci = {};
    ci.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo        = &ai;
    ci.enabledExtensionCount   = static_cast<uint32_t>(extensions.size());
    ci.ppEnabledExtensionNames = extensions.empty() ? nullptr : extensions.data();
    ci.enabledLayerCount       = static_cast<uint32_t>(enabled_layers_.size());
    ci.ppEnabledLayerNames     = enabled_layers_.empty() ? nullptr : enabled_layers_.data();

    if (portability_enabled) {
        ci.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }

    // 把 messenger CI 通过 pNext 链入，使得 vkCreateInstance/vkDestroyInstance 自身也能被 validation 捕获
    VkDebugUtilsMessengerCreateInfoEXT mci{};
    if (validation_enabled_) {
        fill_messenger_ci(mci);
        ci.pNext = &mci;
    }

    VK_CHECK(vkCreateInstance(&ci, nullptr, &instance_));
    LOG_INFO("[Instance] VkInstance created (api 1.2)");
}

void Instance::setup_debug_messenger() {
    auto fn = (PFN_vkCreateDebugUtilsMessengerEXT)
        vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT");
    if (!fn) {
        LOG_WARN("[Instance] vkCreateDebugUtilsMessengerEXT unavailable");
        return;
    }
    VkDebugUtilsMessengerCreateInfoEXT ci{};
    fill_messenger_ci(ci);
    VK_CHECK(fn(instance_, &ci, nullptr, &debug_messenger_));
    LOG_INFO("[Instance] debug messenger installed");
}

void Instance::destroy_debug_messenger() {
    auto fn = (PFN_vkDestroyDebugUtilsMessengerEXT)
        vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT");
    if (fn) {
        fn(instance_, debug_messenger_, nullptr);
    }
    debug_messenger_ = VK_NULL_HANDLE;
}

}  // namespace routes_label::rhi
