#pragma once
#include "core/Point.hpp"
#include <vector>

namespace cg::curves {

// 实验四实现（届时另建 issue）。要求用 de Casteljau 算法。
// 接口为初稿，实验实现时可调整，调整理由写进 issue。

// 对 n 阶 Bezier 曲线按 [0,1] 均匀采样 segments+1 个点，依次连线即得曲线
std::vector<FPoint> sampleBezier(const std::vector<FPoint>& controlPoints, int segments);

} // namespace cg::curves
