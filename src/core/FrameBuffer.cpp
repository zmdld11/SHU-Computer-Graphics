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

void FrameBuffer::blendSpan(int y, int x0, int x1, const Color& color, std::uint8_t alpha) {
    if (y < 0 || y >= height_ || x1 < x0) {
        return;
    }
    const int xa = std::max(0, x0);
    const int xb = std::min(width_ - 1, x1);
    if (xa > xb) {
        return;
    }
    if (alpha >= 255) {
        fillSpan(y, xa, xb, color);
        return;
    }
    auto* row = pixels_.data() + rowOffset(width_, y);
    const std::uint32_t a = alpha;
    const std::uint32_t inv = 255u - a;
    const std::uint32_t cr = static_cast<std::uint32_t>(color.r) * a;
    const std::uint32_t cg = static_cast<std::uint32_t>(color.g) * a;
    const std::uint32_t cb = static_cast<std::uint32_t>(color.b) * a;
    for (int x = xa; x <= xb; ++x) {
        const std::uint32_t d = row[x];
        const std::uint32_t r = (cr + (d & 0xFFu) * inv + 127) / 255u;
        const std::uint32_t g = (cg + ((d >> 8) & 0xFFu) * inv + 127) / 255u;
        const std::uint32_t b = (cb + ((d >> 16) & 0xFFu) * inv + 127) / 255u;
        row[x] = r | (g << 8) | (b << 16) | (0xFFu << 24);
    }
}

void FrameBuffer::blendRect(int x0, int y0, int x1, int y1, const Color& color,
                            std::uint8_t alpha) {
    if (x1 < x0 || y1 < y0 || alpha == 0) {
        return;
    }
    const int ya = std::max(0, y0);
    const int yb = std::min(height_ - 1, y1);
    if (ya > yb) {
        return;
    }
    for (int y = ya; y <= yb; ++y) {
        blendSpan(y, x0, x1, color, alpha);
    }
}

void FrameBuffer::copyRectFrom(const FrameBuffer& src, int srcX, int srcY, int w, int h,
                               int dstX, int dstY) {
    if (w <= 0 || h <= 0) {
        return;
    }
    // 先把目标矩形夹到本画布，再把对应的源列夹到源画布；行方向逐行夹。
    const int dx0 = std::max(0, dstX);
    const int dx1 = std::min(width_, dstX + w);
    const int dy0 = std::max(0, dstY);
    const int dy1 = std::min(height_, dstY + h);
    if (dx0 >= dx1 || dy0 >= dy1) {
        return;
    }
    const int sx0 = std::max(0, srcX + (dx0 - dstX));
    const int sx1 = std::min(src.width_, srcX + (dx1 - dstX));
    if (sx0 >= sx1) {
        return;
    }
    const int count = sx1 - sx0;
    const int copyDx = dstX + (sx0 - srcX);
    for (int dy = dy0; dy < dy1; ++dy) {
        const int sy = srcY + (dy - dstY);
        if (sy < 0 || sy >= src.height_) {
            continue;
        }
        std::memcpy(pixels_.data() + rowOffset(width_, dy) + static_cast<std::size_t>(copyDx),
                    src.pixels_.data() + rowOffset(src.width_, sy) +
                        static_cast<std::size_t>(sx0),
                    static_cast<std::size_t>(count) * sizeof(std::uint32_t));
    }
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
