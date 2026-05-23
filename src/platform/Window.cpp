#include "platform/Window.h"

#include "utils/Log.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <stdexcept>

namespace routes_label::platform {

namespace {
int g_glfw_refcount = 0;

void glfw_error_callback(int code, const char* desc) {
    LOG_ERROR("[GLFW] error " << code << ": " << (desc ? desc : "(null)"));
}
}  // namespace

Window::Window(uint32_t width, uint32_t height, std::string title) {
    if (g_glfw_refcount == 0) {
        glfwSetErrorCallback(glfw_error_callback);
        if (!glfwInit()) {
            throw std::runtime_error("glfwInit failed");
        }
    }
    ++g_glfw_refcount;

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);   // 不要 GL context
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    window_ = glfwCreateWindow(static_cast<int>(width),
                               static_cast<int>(height),
                               title.c_str(),
                               nullptr, nullptr);
    if (!window_) {
        --g_glfw_refcount;
        if (g_glfw_refcount == 0) glfwTerminate();
        throw std::runtime_error("glfwCreateWindow failed");
    }

    glfwSetWindowUserPointer(window_, this);
    glfwSetFramebufferSizeCallback(window_, &Window::framebuffer_resize_callback);

    LOG_INFO("[Window] created " << width << "x" << height << " '" << title << "'");
}

Window::~Window() {
    if (window_) {
        glfwDestroyWindow(window_);
        window_ = nullptr;
    }
    if (--g_glfw_refcount == 0) {
        glfwTerminate();
    }
}

bool Window::should_close() const {
    return glfwWindowShouldClose(window_) != 0;
}

void Window::poll_events() const {
    glfwPollEvents();
}

void Window::framebuffer_size(uint32_t& width, uint32_t& height) const {
    int w = 0, h = 0;
    glfwGetFramebufferSize(window_, &w, &h);
    width  = static_cast<uint32_t>(w);
    height = static_cast<uint32_t>(h);
}

void Window::wait_if_minimized() const {
    int w = 0, h = 0;
    glfwGetFramebufferSize(window_, &w, &h);
    while ((w == 0 || h == 0) && !glfwWindowShouldClose(window_)) {
        glfwWaitEvents();
        glfwGetFramebufferSize(window_, &w, &h);
    }
}

VkSurfaceKHR Window::create_surface(VkInstance instance,
                                    const VkAllocationCallbacks* allocator) const {
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkResult r = glfwCreateWindowSurface(instance, window_, allocator, &surface);
    if (r != 0 /* VK_SUCCESS */) {
        throw std::runtime_error("glfwCreateWindowSurface failed: code " + std::to_string(r));
    }
    return surface;
}

const char** Window::required_instance_extensions(uint32_t* count) {
    return glfwGetRequiredInstanceExtensions(count);
}

void Window::framebuffer_resize_callback(GLFWwindow* w, int /*width*/, int /*height*/) {
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(w));
    if (self) self->resized_ = true;
}

}  // namespace routes_label::platform
