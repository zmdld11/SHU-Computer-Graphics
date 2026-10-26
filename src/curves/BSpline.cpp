// BSpline.cpp — clamped 节点矢量、Cox-de Boor 基函数与均匀采样。
#include "curves/BSpline.hpp"

#include <algorithm>

namespace cg::curves {

std::vector<float> clampedKnots(int ctrlCount, int degree) {
    // 节点数 = 控制点数 + 阶数；两端 p+1 重，内部均布。
    const int p = std::max(0, degree);
    const int m = ctrlCount + p;
    std::vector<float> knots(static_cast<std::size_t>(m) + 1, 1.0f);
    for (int i = 0; i <= p; ++i) {
        knots[static_cast<std::size_t>(i)] = 0.0f;
    }
    const int interior = m - 2 * p - 1;
    for (int k = 1; k <= interior; ++k) {
        knots[static_cast<std::size_t>(p + k)] =
            static_cast<float>(k) / static_cast<float>(interior + 1);
    }
    return knots;
}

float bsplineBasis(int i, int p, float u, const std::vector<float>& knots) {
    if (p == 0) {
        // 零次基：半开区间 [u_i, u_{i+1})；u=1 归最后一段。
        const bool inSpan = (u >= knots[static_cast<std::size_t>(i)] &&
                             u < knots[static_cast<std::size_t>(i) + 1]) ||
                            (u >= 1.0f && knots[static_cast<std::size_t>(i)] <= 1.0f &&
                             knots[static_cast<std::size_t>(i) + 1] >= 1.0f &&
                             knots[static_cast<std::size_t>(i)] <
                                 knots[static_cast<std::size_t>(i) + 1]);
        return inSpan ? 1.0f : 0.0f;
    }
    const float left = knots[static_cast<std::size_t>(i + p)] -
                       knots[static_cast<std::size_t>(i)];
    const float right = knots[static_cast<std::size_t>(i + p + 1)] -
                        knots[static_cast<std::size_t>(i + 1)];
    const float wL = left > 0.0f
                         ? (u - knots[static_cast<std::size_t>(i)]) / left *
                               bsplineBasis(i, p - 1, u, knots)
                         : 0.0f;
    const float wR = right > 0.0f
                         ? (knots[static_cast<std::size_t>(i + p + 1)] - u) / right *
                               bsplineBasis(i + 1, p - 1, u, knots)
                         : 0.0f;
    return wL + wR;
}

std::vector<FPoint> sampleBSpline(const std::vector<FPoint>& controlPoints, int degree,
                                  int segments) {
    std::vector<FPoint> samples;
    if (controlPoints.empty()) {
        return samples;
    }
    if (segments < 1) {
        segments = 1;
    }
    // 阶数 clamp 到 [0, 控制点数-1]：点不够就降阶。
    const int p =
        std::min(std::max(0, degree), static_cast<int>(controlPoints.size()) - 1);
    if (p == 0) {
        return controlPoints;
    }
    const std::vector<float> knots =
        clampedKnots(static_cast<int>(controlPoints.size()), p);
    samples.reserve(static_cast<std::size_t>(segments) + 1);
    for (int k = 0; k <= segments; ++k) {
        const float u = static_cast<float>(k) / static_cast<float>(segments);
        FPoint pt{0.0f, 0.0f};
        for (std::size_t i = 0; i < controlPoints.size(); ++i) {
            const float w = bsplineBasis(static_cast<int>(i), p, u, knots);
            pt.x += w * controlPoints[i].x;
            pt.y += w * controlPoints[i].y;
        }
        samples.push_back(pt);
    }
    return samples;
}

} // namespace cg::curves
