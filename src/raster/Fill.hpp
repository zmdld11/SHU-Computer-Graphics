#pragma once
#include "core/Color.hpp"
#include "core/FrameBuffer.hpp"
#include "core/Point.hpp"
#include <vector>

namespace cg::raster {

// 实验二（必做 1）：扫描线算法填充任意多边形。
// 数据结构用教材的"新边表 NET + 活性边表 AET"：
//   - NET 按边的下端点 y 分桶挂边（起始扫描线即该桶下标）
//   - AET 随扫描线推进：加入新边、删除失效边、按 x 排序、两两配对填充
// 顶点交点计数规则：非水平边按半开区间 [ymin, ymax) 参与求交（上端缩 1 像素），
// 使"扫描线正过顶点"时的交点个数自动正确（局部极值点算 2 个，非极值点算 1 个）。
void scanlineFillPolygon(FrameBuffer& fb, const std::vector<IPoint>& polygon,
                         const Color& color);

// 实验二（必做 2）：连通区域填充的扫描线种子填充算法。
// 从 seed 出发，填充与之同色的连通区域（遇到不同颜色即边界，视为不可填）。
// 与逐像素入栈的简单种子填充相比：每次把一整段（像素段）填掉、只在上下相邻行
// 的新段上取代表像素入栈，栈空间大幅减少。
void scanlineSeedFill(FrameBuffer& fb, IPoint seed, const Color& fillColor);

} // namespace cg::raster
