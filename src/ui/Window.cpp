#include "ui/Window.hpp"
#include <GLFW/glfw3.h>
#include <iostream>

namespace cg {
namespace {

Window* s_active = nullptr; // 单窗口：GLFW C 回调路由到当前 Window

bool g_glfwReady = false;

} // namespace

void Window::glfwKey(GLFWwindow* window, int key, int scancode, int action, int mods) {
    (void)scancode;
    (void)mods;
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    }
    if (s_active != nullptr && s_active->onKey_) {
        s_active->onKey_(key, action);
    }
}

void Window::glfwMouseButton(GLFWwindow* window, int button, int action, int mods) {
    (void)window;
    (void)mods;
    if (s_active != nullptr && s_active->onMouseBtn_) {
        s_active->onMouseBtn_(button, action);
    }
}

void Window::glfwCursorPos(GLFWwindow* window, double x, double y) {
    (void)window;
    if (s_active == nullptr || !s_active->onMouseMove_) {
        return;
    }
    // 鼠标坐标从窗口逻辑坐标换算到帧缓冲像素（适配高 DPI 缩放）
    int fw = 0, fh = 0, ww = 0, wh = 0;
    glfwGetFramebufferSize(window, &fw, &fh);
    glfwGetWindowSize(window, &ww, &wh);
    if (ww <= 0 || wh <= 0) {
        return;
    }
    s_active->onMouseMove_(x * fw / ww, y * fh / wh);
}

void Window::glfwFramebufferSize(GLFWwindow* window, int width, int height) {
    (void)window;
    if (s_active != nullptr) {
        if (s_active->onResize_) {
            s_active->onResize_(width, height);
        }
        s_active->width_ = width;
        s_active->height_ = height;
    }
}

Window::Window(int width, int height, const char* title, bool hidden)
    : width_(width), height_(height) {
    if (!g_glfwReady) {
        if (glfwInit() != GLFW_TRUE) {
            std::cerr << "[Window] glfwInit failed" << std::endl;
            return;
        }
        g_glfwReady = true;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_VISIBLE, hidden ? GLFW_FALSE : GLFW_TRUE);

    window_ = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (window_ == nullptr) {
        std::cerr << "[Window] glfwCreateWindow failed（是否不支持 3.3 core？）" << std::endl;
        return;
    }
    glfwMakeContextCurrent(window_);
    glfwSetWindowSizeLimits(window_, 400, 300, GLFW_DONT_CARE, GLFW_DONT_CARE);
    glfwSetKeyCallback(window_, &Window::glfwKey);
    glfwSetMouseButtonCallback(window_, &Window::glfwMouseButton);
    glfwSetCursorPosCallback(window_, &Window::glfwCursorPos);
    glfwSetFramebufferSizeCallback(window_, &Window::glfwFramebufferSize);
    s_active = this;
}

Window::~Window() {
    if (window_ != nullptr) {
        glfwDestroyWindow(window_);
        if (s_active == this) {
            s_active = nullptr;
        }
    }
    // glfwTerminate 交给进程退出：本应用生命周期内只有一个窗口
}

bool Window::shouldClose() const {
    if (window_ == nullptr) {
        return true; // 初始化失败时让主循环立刻退出
    }
    return glfwWindowShouldClose(window_) != 0;
}

void Window::swapBuffers() {
    if (window_ != nullptr) {
        glfwSwapBuffers(window_);
    }
}

void Window::pollEvents() {
    glfwPollEvents();
}

void Window::setTitle(const std::string& title) {
    if (window_ != nullptr) {
        glfwSetWindowTitle(window_, title.c_str());
    }
}

LoaderFn Window::loader() const {
    return reinterpret_cast<LoaderFn>(glfwGetProcAddress);
}

int Window::fbWidth() const {
    int w = 0, h = 0;
    if (window_ != nullptr) {
        glfwGetFramebufferSize(window_, &w, &h);
    }
    return w;
}

int Window::fbHeight() const {
    int w = 0, h = 0;
    if (window_ != nullptr) {
        glfwGetFramebufferSize(window_, &w, &h);
    }
    return h;
}

} // namespace cg
