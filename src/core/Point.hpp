// Point.hpp
// 实现两种二维点类型：IPoint 整数像素坐标（光栅化用）、FPoint 浮点坐标（几何/曲线用）。
#pragma once

namespace cg {

// 整数像素坐标（光栅化用）；浮点坐标（几何/曲线用）
struct IPoint {
    int x = 0;
    int y = 0;
};

struct FPoint {
    float x = 0.0f;
    float y = 0.0f;
};

} // namespace cg
