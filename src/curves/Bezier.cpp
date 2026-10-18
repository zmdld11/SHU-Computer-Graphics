// Bezier.cpp
// 实现实验四（必做）的 de Casteljau 递推与均匀采样。
#include "curves/Bezier.hpp"

#include <algorithm>

namespace cg::curves {

FPoint deCasteljauAt(const std::vector<FPoint>& controlPoints, float t) {
    if (controlPoints.empty()) {
        return {0.0f, 0.0f};
    }
    // 工作副本逐层割角：第 r 层后 p[i] 是 B 上参数 t 处子折线的第 i 个顶点
    std::vector<FPoint> p = controlPoints;
    const int n = static_cast<int>(p.size()) - 1;
    for (int r = 1; r <= n; ++r) {
        for (int i = 0; i <= n - r; ++i) {
            p[i].x = (1.0f - t) * p[i].x + t * p[i + 1].x;
            p[i].y = (1.0f - t) * p[i].y + t * p[i + 1].y;
        }
    }
    return p[0];
}

std::vector<FPoint> sampleBezier(const std::vector<FPoint>& controlPoints, int segments) {
    std::vector<FPoint> samples;
    if (controlPoints.empty()) {
        return samples;
    }
    if (segments < 1) {
        segments = 1;
    }
    samples.reserve(static_cast<std::size_t>(segments) + 1);
    // 复用同一块工作缓冲：每个 t 只覆盖数值，不再逐点复制/分配控制点 vector
    std::vector<FPoint> work(controlPoints.size());
    const int n = static_cast<int>(work.size()) - 1;
    for (int k = 0; k <= segments; ++k) {
        const float t = static_cast<float>(k) / static_cast<float>(segments);
        std::copy(controlPoints.begin(), controlPoints.end(), work.begin());
        for (int r = 1; r <= n; ++r) {
            for (int i = 0; i <= n - r; ++i) {
                work[i].x = (1.0f - t) * work[i].x + t * work[i + 1].x;
                work[i].y = (1.0f - t) * work[i].y + t * work[i + 1].y;
            }
        }
        samples.push_back(work[0]);
    }
    return samples;
}

} // namespace cg::curves
