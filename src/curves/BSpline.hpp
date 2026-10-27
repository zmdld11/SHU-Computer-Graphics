// BSpline.hpp — B 样条 Cox-de Boor 算法（clamped / 均匀两种节点矢量）。
#pragma once
#include "core/Point.hpp"
#include <vector>

namespace cg::curves {

// clamped 节点矢量：两端 p+1 重（0 与 1），内部均布，曲线过首末控制点。
// 控制点 m 个、阶数 p 时节点数为 m+p。
std::vector<float> clampedKnots(int ctrlCount, int degree);

// 均匀（非固支）节点矢量：节点等距且首末不重复，曲线不过首末控制点。
// 归一化到定义域 [0,1]：u_i = (i - p) / (控制点数 - p)。
std::vector<float> uniformKnots(int ctrlCount, int degree);

// Cox-de Boor 递推求第 i 个 p 阶基函数 N_{i,p}(u)；0/0 型按 0 处理
float bsplineBasis(int i, int p, float u, const std::vector<float>& knots);

// B 样条在 [0,1] 上均匀采样 segments+1 个点。
// clamped=true 过首末控制点；false 用均匀节点（不过首末点）。
// 阶数自动取 min(degree, 控制点数-1)，控制点不足时降阶。
std::vector<FPoint> sampleBSpline(const std::vector<FPoint>& controlPoints, int degree,
                                  int segments, bool clamped = true);

} // namespace cg::curves
