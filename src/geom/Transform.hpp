#pragma once
#include "core/Point.hpp"

namespace cg::geom {

// 实验三实现（届时另建 issue）。约定：3x3 齐次坐标，列向量，v' = M * v。
// 接口为初稿，实验实现时可调整，调整理由写进 issue。

struct Mat3 {
    float m[3][3];
};

Mat3 identity();

Mat3 translate(float tx, float ty);
Mat3 scale(float sx, float sy);
Mat3 rotate(float thetaRad);                      // 绕原点
Mat3 rotateAbout(float px, float py, float thetaRad); // 绕任意点：T(px,py)·R·T(-px,-py)
Mat3 scaleAbout(float px, float py, float sx, float sy);

Mat3 mul(const Mat3& a, const Mat3& b);
FPoint apply(const Mat3& m, FPoint p);

} // namespace cg::geom
