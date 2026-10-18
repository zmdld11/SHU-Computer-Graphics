// FrameBuffer.cpp
// 实现 CPU 像素画板：一块按行存储的内存像素数组，putPixel 是全项目唯一的画点入口，所有实验算法都画在它上面。
#include "core/FrameBuffer.hpp"

#include <algorithm>
#include <cstring>

namespace cg {
namespace {

std::size_t rowOffset(int width, int y) {
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(width);
}

} // namespace

FrameBuffer::FrameBuffer(int width, int height)
    : width_(width), height_(height),
      pixels_(static_cast<std::size_t>(width > 0 ? width : 0) *
                  static_cast<std::size_t>(height > 0 ? height : 0),
              0) {}

// 画布大小改变时重新设定
void FrameBuffer::resize(int width, int height) {
    width_ = width;
    height_ = height;
    pixels_.assign(static_cast<std::size_t>(width > 0 ? width : 0) *
                       static_cast<std::size_t>(height > 0 ? height : 0),
                   0);
}

// 复制整块画板：同尺寸时一条 memcpy；尺寸变化才重新分配
void FrameBuffer::copyFrom(const FrameBuffer& other) {
    if (width_ == other.width_ && height_ == other.height_) {
        if (!pixels_.empty()) {
            std::memcpy(pixels_.data(), other.pixels_.data(),
                        pixels_.size() * sizeof(std::uint32_t));
        }
        return;
    }
    width_ = other.width_;
    height_ = other.height_;
    pixels_ = other.pixels_;
}

// 画布颜色填充：按 32 位像素整块填充，比逐字节循环快得多
void FrameBuffer::clear(const Color& color) {
    const std::uint32_t v = pack(color);
    std::fill(pixels_.begin(), pixels_.end(), v);
}

void FrameBuffer::fillRect(int x0, int y0, int x1, int y1, const Color& color) {
    if (x1 < x0 || y1 < y0) {
        return;
    }
    const int ya = std::max(0, y0);
    const int yb = std::min(height_ - 1, y1);
    if (ya > yb) {
        return;
    }
    for (int y = ya; y <= yb; ++y) {
        fillSpan(y, x0, x1, color);
    }
}

void FrameBuffer::fillSpan(int y, int x0, int x1, const Color& color) {
    if (y < 0 || y >= height_ || x1 < x0) {
        return;
    }
    const int xa = std::max(0, x0);
    const int xb = std::min(width_ - 1, x1);
    if (xa > xb) {
        return;
    }
    auto* row = pixels_.data() + rowOffset(width_, y);
    std::fill(row + xa, row + xb + 1, pack(color));
}

void FrameBuffer::copySpanFrom(const FrameBuffer& src, int y, int x0, int x1) {
    if (y < 0 || y >= height_ || y >= src.height_) {
        return;
    }
    const int xa = std::max(0, x0);
    const int xb = std::min(std::min(width_ - 1, src.width_ - 1), x1);
    if (xa > xb) {
        return;
    }
    const std::size_t srcRow = rowOffset(src.width_, y);
    const std::size_t dstRow = rowOffset(width_, y);
    std::memcpy(pixels_.data() + dstRow + static_cast<std::size_t>(xa),
                src.pixels_.data() + srcRow + static_cast<std::size_t>(xa),
                static_cast<std::size_t>(xb - xa + 1) * sizeof(std::uint32_t));
}

} // namespace cg
