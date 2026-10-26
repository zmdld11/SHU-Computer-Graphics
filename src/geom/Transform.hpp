// Transform.hpp — 3x3 齐次坐标矩阵；列向量约定 v' = M·v。
#pragma once
#include "core/Point.hpp"

namespace cg::geom {

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
