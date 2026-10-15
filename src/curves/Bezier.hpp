// Bezier.hpp
// 实验四（必做）：n 阶 Bezier 曲线的 de Casteljau 生成算法。
#pragma once
#include "core/Point.hpp"
#include <vector>

namespace cg::curves {

// de Casteljau 递推：求 n 阶 Bezier 曲线（n = 控制点数-1）在参数 t 处的点。
// 每层把相邻控制点按 (1-t):t 线性插值，层层收敛到曲线上一点——
// 比 direct 求和数值更稳定，几何直观（割角），也是鼠标拖拽演示的考点。
FPoint deCasteljauAt(const std::vector<FPoint>& controlPoints, float t);

// 对 n 阶 Bezier 曲线按 [0,1] 均匀采样 segments+1 个点，依次连线即得曲线
std::vector<FPoint> sampleBezier(const std::vector<FPoint>& controlPoints, int segments);

} // namespace cg::curves
