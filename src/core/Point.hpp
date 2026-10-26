// Point.hpp — 二维点：IPoint 整数像素坐标、FPoint 浮点坐标。
#pragma once

namespace cg {

struct IPoint {
    int x = 0;
    int y = 0;
};

struct FPoint {
    float x = 0.0f;
    float y = 0.0f;
};

} // namespace cg
