#pragma once
#include "core/Color.hpp"
#include <cstddef>
#include <vector>

namespace cg {

// 软件光栅化的底座：CPU 端像素数组。所有手写算法最终都落到 putPixel。
class FrameBuffer {
public:
    FrameBuffer(int width, int height);

    // 窗口尺寸变化时重设画布（内容清空）
    void resize(int width, int height);

    int width() const  { return width_; }
    int height() const { return height_; }

    void clear(const Color& color = Color::Black());

    // 唯一画点入口。行 0 在顶部；越界坐标直接忽略（光栅化算法允许过冲）
    void putPixel(int x, int y, const Color& color);

    Color pixel(int x, int y) const;

    // RGBA 行主序数据，供 Canvas 上屏
    const std::uint8_t* data() const { return pixels_.data(); }

private:
    int width_ = 0;
    int height_ = 0;
    std::vector<std::uint8_t> pixels_; // 每像素 4 字节
};

} // namespace cg
