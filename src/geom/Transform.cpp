// Transform.cpp
// 实现实验三几何变换数学：3x3 齐次矩阵的平移/放缩/旋转（含绕任意参考点的组合）与点变换。
#include "geom/Transform.hpp"

#include <cmath>

namespace cg::geom {

Mat3 identity() {
    return {{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
}

Mat3 translate(float tx, float ty) {
    return {{{1, 0, tx}, {0, 1, ty}, {0, 0, 1}}};
}

Mat3 scale(float sx, float sy) {
    return {{{sx, 0, 0}, {0, sy, 0}, {0, 0, 1}}};
}

// 屏幕坐标 y 向下：正角在屏幕上是顺时针，与 drawArc 的角度约定一致
Mat3 rotate(float thetaRad) {
    const float c = std::cos(thetaRad);
    const float s = std::sin(thetaRad);
    return {{{c, -s, 0}, {s, c, 0}, {0, 0, 1}}};
}

Mat3 rotateAbout(float px, float py, float thetaRad) {
    // T(p) · R · T(-p)：先把参考点挪到原点，旋转，再挪回去
    return mul(translate(px, py), mul(rotate(thetaRad), translate(-px, -py)));
}

Mat3 scaleAbout(float px, float py, float sx, float sy) {
    return mul(translate(px, py), mul(scale(sx, sy), translate(-px, -py)));
}

// 列向量约定：M = A·B 先作用 B 再作用 A
Mat3 mul(const Mat3& a, const Mat3& b) {
    Mat3 r{};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            float sum = 0.0f;
            for (int k = 0; k < 3; ++k) {
                sum += a.m[i][k] * b.m[k][j];
            }
            r.m[i][j] = sum;
        }
    }
    return r;
}

FPoint apply(const Mat3& m, FPoint p) {
    return {m.m[0][0] * p.x + m.m[0][1] * p.y + m.m[0][2],
            m.m[1][0] * p.x + m.m[1][1] * p.y + m.m[1][2]};
}

} // namespace cg::geom
