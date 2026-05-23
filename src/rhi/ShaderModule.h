#pragma once
//
// ShaderModule.h
// 从 .spv 文件加载并创建 VkShaderModule。
//

#include "rhi/VulkanCommon.h"

#include <filesystem>

namespace routes_label::rhi {

class Device;

class ShaderModule {
public:
    ShaderModule(const Device& device, const std::filesystem::path& spv_path);
    ~ShaderModule();

    ShaderModule(const ShaderModule&) = delete;
    ShaderModule& operator=(const ShaderModule&) = delete;

    VkShaderModule handle() const { return module_; }

private:
    const Device&  device_;
    VkShaderModule module_ = VK_NULL_HANDLE;
};

}  // namespace routes_label::rhi
