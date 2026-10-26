// Text.hpp — 文字入口：Windows 用 GDI 栅格化系统字体，其他平台回退 5x7 点阵。
#pragma once
#include "core/Color.hpp"
#include "core/FrameBuffer.hpp"

namespace cg::text {

// 字体行高（像素）；任务栏布局按它动态计算。
int height();

// UTF-8 字符串显示宽度（像素）。
int textWidth(const char* s);

// 在首行行框顶部 (x, y) 绘制 UTF-8 文本。
void drawText(FrameBuffer& fb, int x, int y, const char* s, const Color& c);

} // namespace cg::text
