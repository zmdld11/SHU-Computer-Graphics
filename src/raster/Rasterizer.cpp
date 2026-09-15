#include "raster/Rasterizer.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace cg::raster {
namespace {

constexpr float kPi = 3.14159265358979f;

// 掩码过滤 + 方形刷子落点：实验一"线型、线宽属性"的统一出口
void plotPen(FrameBuffer& fb, int x, int y, const Color& c, const LineStyle& s, unsigned step) {
    // 线宽>1 时掩码下标按线宽放大（每个掩码位对应 width 个路径像素），
    // 否则方形刷子两侧外扩 w/2 会把虚线/点线的空档填满（#17）
    const unsigned idx =
        (s.width > 1 ? step / static_cast<unsigned>(s.width) : step) & 15u;
    if (((s.mask >> idx) & 1u) == 0u) {
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

struct CirclePixel {
    float angle;
    IPoint p;
};

// 整圆离散点集：中点算法走 1/8 弧 + 八分对称展开，再按角度升序排序去重。
// 排序后掩码计数沿圆周连续递增，虚线/点线在圆周上均匀（#13）。
std::vector<CirclePixel> circlePoints(IPoint center, int radius) {
    std::vector<CirclePixel> pts;
    pts.reserve(static_cast<std::size_t>(radius) * 8);

    int x = 0;
    int y = radius;
    int d = 1 - radius;
    while (x <= y) {
        const int offs[8][2] = {
            {x, y}, {y, x}, {y, -x}, {x, -y}, {-x, -y}, {-y, -x}, {-y, x}, {-x, y},
        };
        for (const auto& off : offs) {
            const IPoint p{center.x + off[0], center.y + off[1]};
            pts.push_back({angleOf(p.x, p.y, center.x, center.y), p});
        }
        if (d < 0) {
            d += 2 * x + 3;       // 取正右像素
        } else {
            d += 2 * (x - y) + 5; // 取右下像素
            --y;
        }
        ++x;
    }

    std::sort(pts.begin(), pts.end(),
              [](const CirclePixel& a, const CirclePixel& b) { return a.angle < b.angle; });
    // 45° 等对称轴上同一像素会从两个八分段各来一次，排序后相邻，去重
    pts.erase(std::unique(pts.begin(), pts.end(),
                          [](const CirclePixel& a, const CirclePixel& b) {
                              return a.p.x == b.p.x && a.p.y == b.p.y;
                          }),
              pts.end());
    return pts;
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
    unsigned step = 0;
    for (const auto& cp : circlePoints(center, radius)) {
        plotPen(fb, cp.p.x, cp.p.y, color, style, step++);
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

    unsigned step = 0; // 只对画出的像素递增，虚线沿弧线连续
    for (const auto& cp : circlePoints(center, radius)) {
        if (inArcRange(cp.angle, s, e)) {
            plotPen(fb, cp.p.x, cp.p.y, color, style, step++);
        }
    }
}

} // namespace cg::raster
