#pragma once

namespace cg {

class FrameBuffer;

// 过程地址加载器（由 UI 层传入，如 glfwGetProcAddress）
using LoaderFn = void* (*)(const char* name);

// core 里唯一允许碰 OpenGL 的地方：把 FrameBuffer 内容贴到窗口。
// 引擎其余部分与 GL 完全无关。
class Canvas {
public:
    Canvas(int width, int height, LoaderFn loader);
    ~Canvas();

    Canvas(const Canvas&) = delete;
    Canvas& operator=(const Canvas&) = delete;

    bool valid() const { return valid_; }

    // 上传像素并绘制全屏四边形，每帧调用一次
    void present(const FrameBuffer& fb);

private:
    unsigned program_ = 0;
    unsigned texture_ = 0;
    unsigned vao_ = 0;
    bool valid_ = false;
};

} // namespace cg
