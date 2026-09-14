#pragma once
#include "core/Canvas.hpp"

struct GLFWwindow;

namespace cg {

// GLFW 窗口封装：建窗口、建 GL 上下文、转发轮询。算法层对它无感知。
class Window {
public:
    // hidden = true 时窗口不可见，用于 --smoke 自检
    Window(int width, int height, const char* title, bool hidden = false);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool valid() const { return window_ != nullptr; }
    bool shouldClose() const;
    void swapBuffers();
    void pollEvents();

    // GL 过程地址加载器，交给 Canvas 初始化 glad 用
    LoaderFn loader() const;

    int width() const  { return width_; }
    int height() const { return height_; }

private:
    GLFWwindow* window_ = nullptr;
    int width_ = 0;
    int height_ = 0;
};

} // namespace cg
