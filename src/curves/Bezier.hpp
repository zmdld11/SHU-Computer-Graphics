// Bezier.hpp — n 阶 Bezier 曲线的 de Casteljau 生成算法。
#pragma once
#include "core/Point.hpp"
#include <vector>

namespace cg::curves {

// de Casteljau：求 n 阶 Bezier（n = 控制点数-1）在 t 处的点。
// 每层把相邻控制点按 (1-t):t 插值，层层收敛到曲线上一点（割角）。
FPoint deCasteljauAt(const std::vector<FPoint>& controlPoints, float t);

// 在 [0,1] 上均匀采样 segments+1 个点。
std::vector<FPoint> sampleBezier(const std::vector<FPoint>& controlPoints, int segments);

} // namespace cg::curves
