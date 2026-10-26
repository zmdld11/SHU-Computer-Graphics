// SmokeChecks.hpp — 自检入口：runAlgorithmSelfChecks / writeDemoImage / writePPM。
#pragma once
#include "core/FrameBuffer.hpp"

namespace cg::tests {

// 算法自测：逐项断言，返回失败项数（0 = 全通过）；--smoke 调用。
int runAlgorithmSelfChecks();

// 把任意画布导出为 PPM(P6)。
bool writePPM(const char* path, const FrameBuffer& fb);

// 把"实验一 + 实验二"的演示场景渲染成一张图，导出为 PPM 文件。
bool writeDemoImage(const char* path);

} // namespace cg::tests
