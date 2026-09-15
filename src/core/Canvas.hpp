#pragma once

namespace cg {

class FrameBuffer;

// 过程地址加载器（由 UI 层传入，如 glfwGetProcAddress）
using LoaderFn = void* (*)(const char* name);

// core 里唯一允许碰 OpenGL 的地方：把 FrameBuffer 的内容显示到窗口上。
// 引擎其余部分与 OpenGL 完全无关——算法只管往 FrameBuffer 写像素。
class Canvas {
public:
    Canvas(int width, int height, LoaderFn loader);
    ~Canvas();

    Canvas(const Canvas&) = delete;
    Canvas& operator=(const Canvas&) = delete;

    bool valid() const { return valid_; }

    // 窗口尺寸变化时重建贴图存储
    void resize(int width, int height);

    // 上传像素并绘制：把 FrameBuffer 内容贴到铺满窗口的矩形上显示。
    // 每帧调用一次，详见 docs/OpenGL与界面交互.md
    void present(const FrameBuffer& fb);

private:
    unsigned program_ = 0; // GPU 着色器程序（一条"怎么画"的流水线）
    unsigned texture_ = 0; // 贴图：GPU 显存里的"画板副本"
    unsigned vao_ = 0;     // 顶点数组对象：记住"铺满窗口的矩形"的顶点配置
    bool valid_ = false;
};

} // namespace cg
