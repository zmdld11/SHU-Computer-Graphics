#pragma once
#include "core/Color.hpp"
#include "core/FrameBuffer.hpp"
#include "core/Point.hpp"

namespace cg::raster {

// 实验一（#2）实现：所有算法手写，最终只调 FrameBuffer::putPixel。
// 接口为初稿，实验实现时可调整，调整理由写进 issue。

// 直线：Bresenham 算法，支持任意斜率
void drawLine(FrameBuffer& fb, IPoint a, IPoint b, const Color& color);

// 圆/圆弧：中点算法（八分对称）
void drawCircle(FrameBuffer& fb, IPoint center, int radius, const Color& color);

// 线型控制：像素掩码（如 0xFFFF 实线、0xFF00 虚线），对后续画线生效
void setLineMask(unsigned mask);

// 线宽控制：像素重复法或刷子法
void setLineWidth(int width);

} // namespace cg::raster
