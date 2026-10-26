// Clip.hpp — 裁剪：Cohen-Sutherland、中点分割、Cyrus-Beck。
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

// Cohen-Sutherland 编码裁剪：区域编码 左 0001/右 0010/下 0100/上 1000。
// 两端编码或为 0 → 完全可见；与非 0 → 完全不可见。返回可见性并改写端点。
bool cohenSutherland(IPoint& a, IPoint& b, const Rect& win);

// 中点分割裁剪：不断对分、剪掉完全不可见的一半，逼近到 1 像素精度；只用整数运算。
bool midpointSplit(IPoint& a, IPoint& b, const Rect& win);

// Cyrus-Beck 参数化裁剪，窗口为任意凸多边形（顶点顺序一致）。
bool cyrusBeck(FPoint& a, FPoint& b, const std::vector<FPoint>& convexWin);

// 点是否在凸多边形内（含边界）。
bool insideConvex(FPoint p, const std::vector<FPoint>& convexWin);

} // namespace cg::raster
