#pragma once
#include "core/Point.hpp"
#include <vector>

namespace cg::raster {

// 矩形裁剪窗口（屏幕坐标，y 向下）
struct Rect {
    int xmin = 0;
    int ymin = 0;
    int xmax = 0;
    int ymax = 0;
};

// 实验二（必做 3a）：Cohen-Sutherland 编码裁剪算法。
// 4 位区域编码：左 0001 / 右 0010 / 下 0100 / 上 1000（"上"= 屏幕上方 = y 更小）。
// 两端编码按位或为 0 → 完全可见；按位与非 0 → 完全不可见（同在窗口某一侧之外）。
// 返回是否可见；可见时把 a、b 改写为裁剪后的端点。
bool cohenSutherland(IPoint& a, IPoint& b, const Rect& win);

// 实验二（必做 3b）：中点分割裁剪算法。
// 不断对分线段、剪掉完全不可见的一半，逼近到 1 像素精度；全程只用整数加法与折半，
// 不需要乘除求交（便于硬件实现，是与 C-S 对比的考点）。返回是否可见并输出端点。
bool midpointSplit(IPoint& a, IPoint& b, const Rect& win);

// 实验二（挑战题）：Cyrus-Beck 参数化裁剪，窗口为任意凸多边形（顶点顺序需一致）。
// 返回是否可见；可见时把 a、b 改写为裁剪后的端点。
bool cyrusBeck(FPoint& a, FPoint& b, const std::vector<FPoint>& convexWin);

// 点是否落在凸多边形内（含边界）——供挑战题测试与判定使用
bool insideConvex(FPoint p, const std::vector<FPoint>& convexWin);

} // namespace cg::raster
