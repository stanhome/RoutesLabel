#include "app/Application.h"

#include "platform/Window.h"
#include "renderer/TriangleRenderer.h"
#include "rhi/Device.h"
#include "rhi/Instance.h"
#include "rhi/PhysicalDevice.h"
#include "utils/Log.h"

#include <vulkan/vulkan.h>

namespace routes_label::app {

namespace {
constexpr uint32_t kInitWidth  = 1280;
constexpr uint32_t kInitHeight = 720;
constexpr const char* kTitle   = "RoutesLabel - Triangle Demo";

#if defined(ROUTES_ENABLE_VALIDATION) && ROUTES_ENABLE_VALIDATION
constexpr bool kEnableValidation = true;
#else
constexpr bool kEnableValidation = false;
#endif
}

Application::Application() {
    init();
}

Application::~Application() {
    shutdown();
}

void Application::init() {
    window_ = std::make_unique<platform::Window>(kInitWidth, kInitHeight, kTitle);

    uint32_t ext_count = 0;
    const char** exts  = platform::Window::required_instance_extensions(&ext_count);
    instance_ = std::make_unique<rhi::Instance>("RoutesLabel", exts, ext_count, kEnableValidation);

    surface_ = window_->create_surface(instance_->handle());

    physical_ = std::make_unique<rhi::PhysicalDevice>(instance_->handle(), surface_);
    device_   = std::make_unique<rhi::Device>(*physical_, instance_->validation_enabled());

    renderer_ = std::make_unique<renderer::TriangleRenderer>(
        *instance_, *physical_, *device_, *window_, surface_);

    LOG_INFO("[Application] initialized");
}

void Application::shutdown() {
    if (renderer_) renderer_->wait_idle();
    renderer_.reset();
    device_.reset();
    physical_.reset();
    if (instance_ && surface_ != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(instance_->handle(), surface_, nullptr);
        surface_ = VK_NULL_HANDLE;
    }
    instance_.reset();
    window_.reset();
}

void Application::run() {
    while (!window_->should_close()) {
        window_->poll_events();
        renderer_->draw_frame();
    }
    if (renderer_) renderer_->wait_idle();
}

}  // namespace routes_label::app
