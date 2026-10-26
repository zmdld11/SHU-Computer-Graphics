// ImageIO.hpp — 无第三方依赖的 PPM(P6) 导出。
#pragma once
#include "core/FrameBuffer.hpp"

namespace cg {

// 把画布写成 PPM(P6)，不需要窗口/OpenGL。
bool writePPM(const char* path, const FrameBuffer& fb);

} // namespace cg
