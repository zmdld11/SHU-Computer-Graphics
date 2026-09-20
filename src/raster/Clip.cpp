#include "raster/Clip.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <utility>

namespace cg::raster {
namespace {

// 区域编码位（屏幕坐标：y 向下，"上"指 y 小于窗口上边）
enum OutCode : int {
    kInside = 0,
    kLeft = 1,   // x < xmin
    kRight = 2,  // x > xmax
    kBottom = 4, // y > ymax
    kTop = 8,    // y < ymin
};

int outCode(IPoint p, const Rect& w) {
    int code = kInside;
    if (p.x < w.xmin) {
        code |= kLeft;
    } else if (p.x > w.xmax) {
        code |= kRight;
    }
    if (p.y < w.ymin) {
        code |= kTop;
    } else if (p.y > w.ymax) {
        code |= kBottom;
    }
    return code;
}

// 中点分割的递归核心：把子段中可见的部分端点收集到 pts
void collectVisible(IPoint p, IPoint q, const Rect& w, std::vector<IPoint>& pts) {
    const int c1 = outCode(p, w);
    const int c2 = outCode(q, w);
    if ((c1 & c2) != 0) {
        return; // 完全不可见：整段剪掉
    }
    if ((c1 | c2) == 0) {
        pts.push_back(p); // 完全可见：两端都保留
        pts.push_back(q);
        return;
    }
    if (std::abs(q.x - p.x) <= 1 && std::abs(q.y - p.y) <= 1) {
        // 已二分到 1 像素精度：把落在窗口内的那一端作为近似可见点
        if (c1 == 0) {
            pts.push_back(p);
        }
        if (c2 == 0) {
            pts.push_back(q);
        }
        return;
    }
    const IPoint m{(p.x + q.x) / 2, (p.y + q.y) / 2}; // 中点（整数折半）
    collectVisible(p, m, w, pts);
    collectVisible(m, q, w, pts);
}

} // namespace

bool cohenSutherland(IPoint& a, IPoint& b, const Rect& win) {
    int ca = outCode(a, win);
    int cb = outCode(b, win);

    // 每轮把某个窗外端点与它越过的窗口边界求交，端点单调向窗口收缩，必然收敛；
    // 上限 16 轮只是防御（正常 1~4 轮结束）
    for (int guard = 0; guard < 16; ++guard) {
        if ((ca | cb) == 0) {
            return true; // 完全可见
        }
        if ((ca & cb) != 0) {
            return false; // 完全不可见
        }
        // 取处在窗外的那一端，与它所越过的那条窗口边界求交。
        // 注意：交点可能落在“相邻边界之外”（例如越过上边界的交点其实在窗口左侧），
        // 此时不能夹取到窗口内——下一轮会继续改与真正的边界求交（教材算法的循环语义）
        const int c = (ca != 0) ? ca : cb;
        const bool atA = (c == ca);
        double x = 0.0;
        double y = 0.0;
        if ((c & kTop) != 0) { // 越过上边界 y = ymin（此时两端点 y 必不相同）
            y = static_cast<double>(win.ymin);
            x = a.x + (static_cast<double>(b.x - a.x) * (win.ymin - a.y)) /
                          static_cast<double>(b.y - a.y);
        } else if ((c & kBottom) != 0) {
            y = static_cast<double>(win.ymax);
            x = a.x + (static_cast<double>(b.x - a.x) * (win.ymax - a.y)) /
                          static_cast<double>(b.y - a.y);
        } else if ((c & kRight) != 0) {
            x = static_cast<double>(win.xmax);
            y = a.y + (static_cast<double>(b.y - a.y) * (win.xmax - a.x)) /
                          static_cast<double>(b.x - a.x);
        } else { // kLeft
            x = static_cast<double>(win.xmin);
            y = a.y + (static_cast<double>(b.y - a.y) * (win.xmin - a.x)) /
                          static_cast<double>(b.x - a.x);
        }
        const IPoint p{static_cast<int>(std::lround(x)), static_cast<int>(std::lround(y))};
        if (atA) {
            a = p;
            ca = outCode(a, win);
        } else {
            b = p;
            cb = outCode(b, win);
        }
    }
    return false; // 防御性返回，正常不会到达
}

bool midpointSplit(IPoint& a, IPoint& b, const Rect& win) {
    std::vector<IPoint> pts;
    collectVisible(a, b, win, pts);
    if (pts.empty()) {
        return false;
    }
    if (pts.size() == 1) {
        a = pts[0];
        b = pts[0];
        return true;
    }

    // 可见部分在一条线段上是连续的：按沿 a→b 方向的投影取最小/最大两点作为结果
    const long long dx = static_cast<long long>(b.x) - a.x;
    const long long dy = static_cast<long long>(b.y) - a.y;
    std::size_t iMin = 0;
    std::size_t iMax = 0;
    long long keyMin = 0;
    long long keyMax = 0;
    for (std::size_t i = 0; i < pts.size(); ++i) {
        const long long kx = static_cast<long long>(pts[i].x) - a.x;
        const long long ky = static_cast<long long>(pts[i].y) - a.y;
        const long long key = kx * dx + ky * dy;
        if (i == 0 || key < keyMin) {
            keyMin = key;
            iMin = i;
        }
        if (i == 0 || key > keyMax) {
            keyMax = key;
            iMax = i;
        }
    }
    const IPoint p0 = pts[iMin];
    const IPoint p1 = pts[iMax];
    a = p0;
    b = p1;
    return true;
}

bool cyrusBeck(FPoint& a, FPoint& b, const std::vector<FPoint>& convexWin) {
    const int n = static_cast<int>(convexWin.size());
    if (n < 3) {
        return false;
    }

    // 顶点绕向（有向面积符号）决定内法线的取法
    double area2 = 0.0;
    for (int i = 0; i < n; ++i) {
        const FPoint& p1 = convexWin[i];
        const FPoint& p2 = convexWin[(i + 1) % n];
        area2 += static_cast<double>(p1.x) * p2.y - static_cast<double>(p2.x) * p1.y;
    }
    const double winding = (area2 >= 0.0) ? 1.0 : -1.0;

    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    double t0 = 0.0;
    double t1 = 1.0;

    for (int i = 0; i < n; ++i) {
        const FPoint& p1 = convexWin[i];
        const FPoint& p2 = convexWin[(i + 1) % n];
        // 内法线：与边方向垂直、指向多边形内部的那一侧。
        // 屏幕坐标 y 向下：有向面积为正的环序下，内法线为 (-dy, dx)（推导见 issue 记录）
        double nx = -winding * (p2.y - p1.y);
        double ny = winding * (p2.x - p1.x);
        const double len = std::sqrt(nx * nx + ny * ny);
        if (len <= 0.0) {
            continue; // 退化边
        }
        nx /= len;
        ny /= len;

        const double f = nx * (a.x - p1.x) + ny * (a.y - p1.y); // a 到边界的有向距离
        const double w = -(nx * dx + ny * dy);                  // 线段方向上的变化率
        if (std::abs(w) < 1e-12) {
            if (f < 0.0) {
                return false; // 平行于该边且在窗外侧
            }
            continue;
        }
        // 由 n·(P0 - Pe) + t·(n·d) ≥ 0 解 t 区间：w<0 → 下限（进入），w>0 → 上限（离开）
        const double t = f / w;
        if (w < 0.0) {
            t0 = std::max(t0, t);
        } else {
            t1 = std::min(t1, t);
        }
        if (t0 > t1) {
            return false;
        }
    }

    const FPoint p0{a.x + static_cast<float>(t0 * dx), a.y + static_cast<float>(t0 * dy)};
    const FPoint p1{a.x + static_cast<float>(t1 * dx), a.y + static_cast<float>(t1 * dy)};
    a = p0;
    b = p1;
    return true;
}

bool insideConvex(FPoint p, const std::vector<FPoint>& convexWin) {
    const int n = static_cast<int>(convexWin.size());
    if (n < 3) {
        return false;
    }
    constexpr double kEps = 1e-6;
    int pos = 0;
    int neg = 0;
    for (int i = 0; i < n; ++i) {
        const FPoint& p1 = convexWin[i];
        const FPoint& p2 = convexWin[(i + 1) % n];
        const double cross = static_cast<double>(p2.x - p1.x) * (p.y - p1.y) -
                             static_cast<double>(p2.y - p1.y) * (p.x - p1.x);
        if (cross > kEps) {
            ++pos;
        } else if (cross < -kEps) {
            ++neg;
        }
    }
    return pos == 0 || neg == 0; // 叉积同号（或为 0）→ 在内部或边界上
}

} // namespace cg::raster
