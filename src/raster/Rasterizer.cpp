#include "raster/Rasterizer.hpp"
#include <cmath>

namespace cg::raster {
namespace {

constexpr float kPi = 3.14159265358979f;

// 掩码过滤 + 方形刷子落点：实验一"线型、线宽属性"的统一出口
void plotPen(FrameBuffer& fb, int x, int y, const Color& c, const LineStyle& s, unsigned step) {
    if (((s.mask >> (step & 15u)) & 1u) == 0u) {
        return;
    }
    if (s.width <= 1) {
        fb.putPixel(x, y, c);
        return;
    }
    const int half = s.width / 2;
    for (int dy = -half; dy <= half; ++dy) {
        for (int dx = -half; dx <= half; ++dx) {
            fb.putPixel(x + dx, y + dy, c);
        }
    }
}

// 像素相对圆心的方位角（度，[0,360)），y 向下 → 顺时针为正
float angleOf(int px, int py, int cx, int cy) {
    float a = std::atan2(static_cast<float>(py - cy), static_cast<float>(px - cx)) *
              (180.0f / kPi);
    return a < 0.0f ? a + 360.0f : a;
}

// start < end（end 可大于 360）
bool inArcRange(float ang, float start, float end) {
    return (ang >= start && ang <= end) || (ang + 360.0f >= start && ang + 360.0f <= end);
}

} // namespace

void drawLine(FrameBuffer& fb, IPoint a, IPoint b, const Color& color, const LineStyle& style) {
    const int dx = b.x >= a.x ? b.x - a.x : a.x - b.x;
    const int dy = b.y >= a.y ? b.y - a.y : a.y - b.y;
    const int sx = b.x >= a.x ? 1 : -1;
    const int sy = b.y >= a.y ? 1 : -1;

    int x = a.x;
    int y = a.y;
    unsigned step = 0;

    if (dx >= dy) {
        // |m| <= 1：x 为驱动轴，误差项控制 y 何时 +1
        int e = 2 * dy - dx;
        for (int i = 0; i <= dx; ++i) {
            plotPen(fb, x, y, color, style, step++);
            if (e >= 0) {
                y += sy;
                e += 2 * (dy - dx);
            } else {
                e += 2 * dy;
            }
            x += sx;
        }
    } else {
        // |m| > 1：y 为驱动轴，x 与 y 角色互换
        int e = 2 * dx - dy;
        for (int i = 0; i <= dy; ++i) {
            plotPen(fb, x, y, color, style, step++);
            if (e >= 0) {
                x += sx;
                e += 2 * (dx - dy);
            } else {
                e += 2 * dx;
            }
            y += sy;
        }
    }
}

void drawCircle(FrameBuffer& fb, IPoint center, int radius, const Color& color,
                const LineStyle& style) {
    if (radius < 0) {
        return;
    }
    if (radius == 0) {
        plotPen(fb, center.x, center.y, color, style, 0);
        return;
    }

    int x = 0;
    int y = radius;
    int d = 1 - radius; // 中点判别式
    unsigned step = 0;

    while (x <= y) {
        // 八分对称：由 (x,y) 生成圆上 8 点
        plotPen(fb, center.x + x, center.y + y, color, style, step++);
        plotPen(fb, center.x + y, center.y + x, color, style, step++);
        plotPen(fb, center.x + y, center.y - x, color, style, step++);
        plotPen(fb, center.x + x, center.y - y, color, style, step++);
        plotPen(fb, center.x - x, center.y - y, color, style, step++);
        plotPen(fb, center.x - y, center.y - x, color, style, step++);
        plotPen(fb, center.x - y, center.y + x, color, style, step++);
        plotPen(fb, center.x - x, center.y + y, color, style, step++);

        if (d < 0) {
            d += 2 * x + 3;       // 取正右像素
        } else {
            d += 2 * (x - y) + 5; // 取右下像素
            --y;
        }
        ++x;
    }
}

void drawArc(FrameBuffer& fb, IPoint center, int radius, float startDeg, float endDeg,
             const Color& color, const LineStyle& style) {
    if (radius <= 0) {
        return;
    }

    // 角度归一化到 [0,360)，保证 start < end
    float s = std::fmod(startDeg, 360.0f);
    if (s < 0.0f) s += 360.0f;
    float e = std::fmod(endDeg, 360.0f);
    if (e < 0.0f) e += 360.0f;
    if (e <= s) e += 360.0f;

    int x = 0;
    int y = radius;
    int d = 1 - radius;
    unsigned step = 0; // 只对画出的像素递增，虚线沿弧连续

    while (x <= y) {
        const int offs[8][2] = {
            {x, y}, {y, x}, {y, -x}, {x, -y}, {-x, -y}, {-y, -x}, {-y, x}, {-x, y},
        };
        for (const auto& off : offs) {
            const int px = center.x + off[0];
            const int py = center.y + off[1];
            if (inArcRange(angleOf(px, py, center.x, center.y), s, e)) {
                plotPen(fb, px, py, color, style, step++);
            }
        }
        if (d < 0) {
            d += 2 * x + 3;
        } else {
            d += 2 * (x - y) + 5;
            --y;
        }
        ++x;
    }
}

} // namespace cg::raster
