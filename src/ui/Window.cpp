#include "ui/Window.hpp"
#include <GLFW/glfw3.h>
#include <iostream>

namespace cg {
namespace {

bool g_glfwReady = false;

void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    (void)scancode;
    (void)mods;
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    }
}

} // namespace

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
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    glfwWindowHint(GLFW_VISIBLE, hidden ? GLFW_FALSE : GLFW_TRUE);

    window_ = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (window_ == nullptr) {
        std::cerr << "[Window] glfwCreateWindow failed（是否不支持 3.3 core？）" << std::endl;
        return;
    }
    glfwMakeContextCurrent(window_);
    glfwSetKeyCallback(window_, keyCallback);
}

Window::~Window() {
    if (window_ != nullptr) {
        glfwDestroyWindow(window_);
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

LoaderFn Window::loader() const {
    return reinterpret_cast<LoaderFn>(glfwGetProcAddress);
}

} // namespace cg
