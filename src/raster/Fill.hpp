// Fill.hpp — 扫描线多边形填充（NET + AET）与扫描线种子填充。
#pragma once
#include "core/Color.hpp"
#include "core/FrameBuffer.hpp"
#include "core/Point.hpp"
#include <cstdint>
#include <vector>

namespace cg::raster {

// 扫描线多边形填充：NET 按下端点 y 分桶，AET 随扫描线加入/删除/排序/两两配对。
// 非水平边按半开区间 [ymin, ymax) 求交，扫描线过顶点时交点个数自动正确。
// alpha < 255 时按行 src-over 半透明落色。
void scanlineFillPolygon(FrameBuffer& fb, const std::vector<IPoint>& polygon,
                         const Color& color, std::uint8_t alpha = 255);

// 扫描线种子填充：从 seed 填同色连通区域；每次填一整段，只在上下相邻行
// 的新段取代表像素入栈，栈空间远小于逐像素版本。
void scanlineSeedFill(FrameBuffer& fb, IPoint seed, const Color& fillColor);

} // namespace cg::raster
