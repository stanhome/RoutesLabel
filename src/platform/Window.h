#pragma once
//
// Window.h
// GLFW 窗口封装。提供 Vulkan Surface 创建能力，屏蔽窗口系统差异。
//

#include <cstdint>
#include <string>

// forward decl 避免在头文件污染 vulkan/glfw 全量符号
struct GLFWwindow;
struct VkInstance_T;
typedef VkInstance_T* VkInstance;
struct VkSurfaceKHR_T;
typedef VkSurfaceKHR_T* VkSurfaceKHR;
struct VkAllocationCallbacks;

namespace routes_label::platform {

class Window {
public:
    Window(uint32_t width, uint32_t height, std::string title);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool        should_close() const;
    void        poll_events() const;

    // 当前 framebuffer 像素尺寸（在 Retina 屏可能 != 窗口尺寸）
    void        framebuffer_size(uint32_t& width, uint32_t& height) const;

    // 是否被通知尺寸变更过；调用方读取后应 reset
    bool        framebuffer_resized() const { return resized_; }
    void        reset_framebuffer_resized() { resized_ = false; }

    // 当窗口最小化（framebuffer 任一维度为 0）时阻塞，直到恢复或关闭
    void        wait_if_minimized() const;

    // 创建 Vulkan Surface（与平台无关，由 GLFW 内部分发）
    VkSurfaceKHR create_surface(VkInstance instance,
                                const VkAllocationCallbacks* allocator = nullptr) const;

    // GLFW 要求查询 instance 扩展，转发给 RHI 层
    static const char** required_instance_extensions(uint32_t* count);

    GLFWwindow* handle() const { return window_; }

private:
    static void framebuffer_resize_callback(GLFWwindow* w, int width, int height);

    GLFWwindow* window_   = nullptr;
    bool        resized_  = false;
};

}  // namespace routes_label::platform
