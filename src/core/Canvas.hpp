// Canvas.hpp
// 实现上屏画布：把 FrameBuffer 的像素上传成 OpenGL 纹理并贴满窗口——全项目唯一接触 OpenGL 绘制的文件。
#pragma once
#include "core/GLLoader.hpp"

namespace cg {

class FrameBuffer;

// core 里唯一允许碰 OpenGL 的地方：把 FrameBuffer 的内容显示到窗口上。
// 引擎其余部分与 OpenGL 完全无关——算法只管往 FrameBuffer 写像素。
// GL 函数的加载机制见 GLLoader.hpp，原理说明见 docs/OpenGL与界面交互.md。
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
    unsigned program_ = 0;      // GPU 着色器程序（一条"怎么画"的流水线）
    unsigned texture_ = 0;      // 贴图：GPU 显存里的"画板副本"
    unsigned vao_ = 0;          // 顶点数组对象：记住"铺满窗口的矩形"的顶点配置
    int texUniform_ = -1;       // uTex 的 uniform 位置，链接后查一次即可
    bool valid_ = false;
};

} // namespace cg
