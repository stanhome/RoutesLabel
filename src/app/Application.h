#pragma once
//
// Application.h
// 顶层应用对象：组合 Window / Instance / Device / Renderer，提供 run() 主循环。
//

#include <memory>

namespace routes_label::platform { class Window; }
namespace routes_label::rhi {
class Instance;
class PhysicalDevice;
class Device;
}
namespace routes_label::renderer { class RoutesRenderer; }

struct VkSurfaceKHR_T;
typedef VkSurfaceKHR_T* VkSurfaceKHR;

namespace routes_label::app {

class Application {
public:
    Application();
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    void run();

private:
    void init();
    void shutdown();

    std::unique_ptr<platform::Window>          window_;
    std::unique_ptr<rhi::Instance>             instance_;
    VkSurfaceKHR                               surface_  = nullptr;
    std::unique_ptr<rhi::PhysicalDevice>       physical_;
    std::unique_ptr<rhi::Device>               device_;
    std::unique_ptr<renderer::RoutesRenderer>  renderer_;
};

}  // namespace routes_label::app
