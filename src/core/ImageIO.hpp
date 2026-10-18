// ImageIO.hpp
// 实现无第三方依赖的 PPM(P6) 导出：算法自测、游戏地图预览和报告插图共用。
#pragma once
#include "core/FrameBuffer.hpp"

namespace cg {

// 把画布逐像素写成 PPM(P6)。不需要窗口/OpenGL，也不引入图片编解码库。
bool writePPM(const char* path, const FrameBuffer& fb);

} // namespace cg
