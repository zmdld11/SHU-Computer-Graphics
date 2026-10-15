// SmokeChecks.hpp
// 实现程序自检入口声明：runAlgorithmSelfChecks（--smoke 调用）、writeDemoImage（--demo 调用）、
// writePPM（通用 PPM 导出，--demo-ui 也用）。
#pragma once
#include "core/FrameBuffer.hpp"

namespace cg::tests {

// 算法自测：逐项断言各算法行为，返回失败项数（0 = 全部通过）。
// main.cpp 的 --smoke 模式调用它；不需要窗口/OpenGL。
int runAlgorithmSelfChecks();

// 把任意画布导出为 PPM（P6）文件。用途：无需窗口即可产出自查图/报告插图；无需任何第三方库。
bool writePPM(const char* path, const FrameBuffer& fb);

// 把"实验一 + 实验二"的演示场景渲染成一张图，导出为 PPM 文件。
bool writeDemoImage(const char* path);

} // namespace cg::tests
