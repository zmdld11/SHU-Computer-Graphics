// Fill.cpp — 扫描线多边形填充（NET + AET）与扫描线种子填充。
#include "raster/Fill.hpp"
#include <algorithm>
#include <cmath>

namespace cg::raster {
namespace {

// 活性边表（AET）中的一条边
struct ActiveEdge {
    int yTop = 0;      // 该边参与求交的最后一个扫描线 y（= 上端点 y - 1，半开区间）
    double x = 0.0;    // 当前扫描线与该边的交点 x（随扫描线递增 dxdy）
    double dxdy = 0.0; // 斜率倒数：扫描线每下移一行，交点 x 的增量
};

} // namespace

void scanlineFillPolygon(FrameBuffer& fb, const std::vector<IPoint>& polygon,
                         const Color& color, std::uint8_t alpha) {
    const int n = static_cast<int>(polygon.size());
    if (n < 3 || fb.width() <= 0 || fb.height() <= 0) {
        return;
    }

    // 多边形 y 范围与画布求交。
    int yLow = polygon[0].y;
    int yHigh = polygon[0].y;
    for (const auto& p : polygon) {
        yLow = std::min(yLow, p.y);
        yHigh = std::max(yHigh, p.y);
    }
    const int scanBegin = std::max(yLow, 0);
    const int scanEnd = std::min(yHigh, fb.height() - 1);
    if (scanBegin > scanEnd) {
        return;
    }

    // NET：下标为扫描线 y，桶里存从该行开始参与的边。
    std::vector<std::vector<ActiveEdge>> net(static_cast<std::size_t>(scanEnd) + 1);
    auto addEdge = [&](IPoint p1, IPoint p2) {
        if (p1.y == p2.y) {
            return; // 水平边不参与求交
        }
        if (p1.y > p2.y) {
            std::swap(p1, p2);
        }
        const int top = p2.y - 1; // 半开区间 [p1.y, p2.y)：上端缩 1 像素，顶点不重复计数
        const int begin = std::max(p1.y, scanBegin);
        if (begin > top || begin > scanEnd) {
            return;
        }
        const double dxdy =
            static_cast<double>(p2.x - p1.x) / static_cast<double>(p2.y - p1.y);
        const double xAtBegin = static_cast<double>(p1.x) + dxdy * (begin - p1.y);
        net[static_cast<std::size_t>(begin)].push_back({top, xAtBegin, dxdy});
    };
    for (int i = 0; i < n; ++i) {
        addEdge(polygon[i], polygon[(i + 1) % n]);
    }

    // 逐条扫描线：维护 AET → 排序 → 配对填充。
    std::vector<ActiveEdge> aet;
    for (int y = scanBegin; y <= scanEnd; ++y) {
        for (const auto& e : net[static_cast<std::size_t>(y)]) {
            aet.push_back(e);
        }
        // 越过该边半开区间顶端 → 移出 AET。
        aet.erase(std::remove_if(aet.begin(), aet.end(),
                                 [y](const ActiveEdge& e) { return y > e.yTop; }),
                  aet.end());
        std::sort(aet.begin(), aet.end(),
                  [](const ActiveEdge& a, const ActiveEdge& b) { return a.x < b.x; });

        // 两两配对得到内部区间（奇数个交点时忽略最后一个）。
        for (std::size_t i = 0; i + 1 < aet.size(); i += 2) {
            const int xStart = std::max(0, static_cast<int>(std::ceil(aet[i].x)));
            const int xEnd =
                std::min(fb.width() - 1, static_cast<int>(std::floor(aet[i + 1].x)));
            if (alpha >= 255) {
                fb.fillSpan(y, xStart, xEnd, color);
            } else {
                fb.blendSpan(y, xStart, xEnd, color, alpha);
            }
        }

        for (auto& e : aet) {
            e.x += e.dxdy;
        }
    }
}

void scanlineSeedFill(FrameBuffer& fb, IPoint seed, const Color& fillColor) {
    if (seed.x < 0 || seed.x >= fb.width() || seed.y < 0 || seed.y >= fb.height()) {
        return;
    }
    const Color target = fb.pixel(seed.x, seed.y); // 目标色
    if (target == fillColor) {
        return;
    }

    std::vector<IPoint> stack;
    stack.push_back(seed);

    while (!stack.empty()) {
        const IPoint p = stack.back();
        stack.pop_back();
        if (fb.pixel(p.x, p.y) != target) {
            continue;
        }

        // 1) 向左右扩展求整段 [xl, xr] 并填充。
        int xl = p.x;
        while (xl - 1 >= 0 && fb.pixel(xl - 1, p.y) == target) {
            --xl;
        }
        int xr = p.x;
        while (xr + 1 < fb.width() && fb.pixel(xr + 1, p.y) == target) {
            ++xr;
        }
        fb.fillSpan(p.y, xl, xr, fillColor);

        // 2) 上下相邻行找新的同色段，每段取最左代表像素入栈。
        for (int dy = -1; dy <= 1; dy += 2) {
            const int y = p.y + dy;
            if (y < 0 || y >= fb.height()) {
                continue;
            }
            bool inSpan = false;
            for (int x = xl; x <= xr; ++x) {
                const bool fillable = (fb.pixel(x, y) == target);
                if (fillable && !inSpan) {
                    stack.push_back({x, y}); // 新段代表像素（最左）
                    inSpan = true;
                } else if (!fillable) {
                    inSpan = false;
                }
            }
        }
    }
}

} // namespace cg::raster
