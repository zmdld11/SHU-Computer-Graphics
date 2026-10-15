// BSpline.hpp
// 实验四（挑战题）：n 阶 B 样条曲线的 Cox-de Boor 生成算法（clamped 均匀节点矢量）。
#pragma once
#include "core/Point.hpp"
#include <vector>

namespace cg::curves {

// clamped 均匀节点矢量：两端 p+1 重节点（0 与 1），内部节点均布——
// 保证曲线过首末控制点（与 Bezier 行为一致，便于对比与对接拼接）。
// 控制点 m 个、阶数 p 时节点数为 m+p。
std::vector<float> clampedKnots(int ctrlCount, int degree);

// Cox-de Boor 递推求第 i 个 p 阶基函数 N_{i,p}(u)；0/0 型按 0 处理
float bsplineBasis(int i, int p, float u, const std::vector<float>& knots);

// n 阶（要求传入阶数 degree）clamped B 样条按 [0,1] 均匀采样 segments+1 个点。
// 实际阶数自动取 min(degree, 控制点数-1)：控制点不足时退化为低阶（2 点=直线），不越界。
std::vector<FPoint> sampleBSpline(const std::vector<FPoint>& controlPoints, int degree,
                                  int segments);

} // namespace cg::curves
