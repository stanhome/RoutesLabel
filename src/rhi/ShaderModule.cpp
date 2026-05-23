#include "rhi/ShaderModule.h"

#include "rhi/Device.h"
#include "utils/FileSystem.h"
#include "utils/Log.h"

#include <stdexcept>

namespace routes_label::rhi {

ShaderModule::ShaderModule(const Device& device, const std::filesystem::path& spv_path)
    : device_(device) {
    auto bytes = utils::read_binary_file(spv_path);
    if (bytes.size() % 4 != 0) {
        throw std::runtime_error("SPIR-V size not multiple of 4: " + spv_path.string());
    }
    VkShaderModuleCreateInfo ci = {};
    ci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = bytes.size();
    ci.pCode    = reinterpret_cast<const uint32_t*>(bytes.data());

    VK_CHECK(vkCreateShaderModule(device_.handle(), &ci, nullptr, &module_));
    LOG_INFO("[ShaderModule] loaded '" << spv_path.filename().string()
             << "' (" << bytes.size() << " bytes)");
}

ShaderModule::~ShaderModule() {
    if (module_ != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device_.handle(), module_, nullptr);
    }
}

}  // namespace routes_label::rhi
