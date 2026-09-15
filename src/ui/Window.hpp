#pragma once
#include "core/Canvas.hpp"
#include <functional>
#include <string>

struct GLFWwindow;

namespace cg {

// GLFW 窗口封装：建窗口、建 GL 上下文、事件回调转发。算法层对它无感知。
// 单窗口应用：回调经静态实例路由。
class Window {
public:
    using KeyFn = std::function<void(int key, int action)>;
    using MouseBtnFn = std::function<void(int button, int action)>;
    using MouseMoveFn = std::function<void(double x, double y)>;
    using ResizeFn = std::function<void(int fbWidth, int fbHeight)>;

    // hidden = true 时窗口不可见，用于 --smoke 自检
    Window(int width, int height, const char* title, bool hidden = false);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool valid() const { return window_ != nullptr; }
    bool shouldClose() const;
    void swapBuffers();
    void pollEvents();
    void setTitle(const std::string& title);

    // GL 过程地址加载器，交给 Canvas 初始化 glad 用
    LoaderFn loader() const;

    // 帧缓冲像素尺寸（高 DPI 缩放下与窗口逻辑尺寸不同）
    int fbWidth() const;
    int fbHeight() const;

    int width() const  { return width_; }
    int height() const { return height_; }

    void setOnKey(KeyFn cb)              { onKey_ = std::move(cb); }
    void setOnMouseButton(MouseBtnFn cb) { onMouseBtn_ = std::move(cb); }
    void setOnMouseMove(MouseMoveFn cb)  { onMouseMove_ = std::move(cb); }
    void setOnResize(ResizeFn cb)        { onResize_ = std::move(cb); }

private:
    GLFWwindow* window_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    KeyFn onKey_;
    MouseBtnFn onMouseBtn_;
    MouseMoveFn onMouseMove_;
    ResizeFn onResize_;

    // GLFW C 回调的静态入口（可访问私有成员），转发到对应 std::function
    static void glfwKey(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void glfwMouseButton(GLFWwindow* window, int button, int action, int mods);
    static void glfwCursorPos(GLFWwindow* window, double x, double y);
    static void glfwFramebufferSize(GLFWwindow* window, int width, int height);
};

} // namespace cg
