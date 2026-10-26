// Canvas.hpp — 把 FrameBuffer 上传为 OpenGL 纹理贴满窗口。
#pragma once
#include "core/GLLoader.hpp"

namespace cg {

class FrameBuffer;

// core 中唯一接触 OpenGL 的地方；其余部分只往 FrameBuffer 写像素。
class Canvas {
public:
    Canvas(int width, int height, LoaderFn loader);
    ~Canvas();

    Canvas(const Canvas&) = delete;
    Canvas& operator=(const Canvas&) = delete;

    bool valid() const { return valid_; }

    // 窗口尺寸变化时重建贴图存储
    void resize(int width, int height);

    // 上传像素并绘制：把 FrameBuffer 贴到铺满窗口的矩形上。
    void present(const FrameBuffer& fb);

private:
    unsigned program_ = 0; // 着色器程序
    unsigned texture_ = 0; // 贴图
    unsigned vao_ = 0;     // 矩形顶点配置
    int texUniform_ = -1;  // uTex uniform 位置
    bool valid_ = false;
};

} // namespace cg
