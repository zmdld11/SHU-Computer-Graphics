#pragma once
#include "core/Color.hpp"
#include "core/FrameBuffer.hpp"
#include "core/Point.hpp"

namespace cg::raster {

// 线型线宽属性：mask 为 16 位像素掩码（bit=1 才落点），width 为方形刷子边长（奇数）
struct LineStyle {
    unsigned mask = 0xFFFFu;
    int width = 1;
};

// 直线：Bresenham 算法，任意斜率（|m|>1 时切换驱动轴，方向由端点差决定，全整数运算）
void drawLine(FrameBuffer& fb, IPoint a, IPoint b, const Color& color,
              const LineStyle& style = {});

// 圆：中点算法（判别式 d=1-R 起步），八分对称
void drawCircle(FrameBuffer& fb, IPoint center, int radius, const Color& color,
                const LineStyle& style = {});

// 圆弧：中点算法 + 逐点角度过滤。角度为屏幕坐标（0°=正右，y 向下故顺时针为正），
// 画从 startDeg 顺时针到 endDeg 的弧；endDeg<=startDeg 视为补一圈。
// 对应实验一挑战问题：指定起止角的圆弧段
void drawArc(FrameBuffer& fb, IPoint center, int radius, float startDeg, float endDeg,
             const Color& color, const LineStyle& style = {});

} // namespace cg::raster
