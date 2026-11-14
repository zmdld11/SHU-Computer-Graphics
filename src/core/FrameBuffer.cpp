// FrameBuffer.cpp — CPU 像素画板实现。
#include "core/FrameBuffer.hpp"

#include <algorithm>
#include <cmath>
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

void FrameBuffer::resize(int width, int height) {
    width_ = width;
    height_ = height;
    pixels_.assign(static_cast<std::size_t>(width > 0 ? width : 0) *
                       static_cast<std::size_t>(height > 0 ? height : 0),
                   0);
}

// 同尺寸走 memcpy，尺寸变化才重新分配。
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

void FrameBuffer::blendAddSpan(int y, int x0, int x1, const Color& color, std::uint8_t alpha) {
    if (y < 0 || y >= height_ || alpha == 0) {
        return;
    }
    const int xa = std::max(0, x0);
    const int xb = std::min(width_ - 1, x1);
    if (xa > xb) {
        return;
    }
    auto* row = pixels_.data() + rowOffset(width_, y);
    const std::uint32_t a = alpha;
    const std::uint32_t cr = static_cast<std::uint32_t>(color.r) * a / 255u;
    const std::uint32_t cg = static_cast<std::uint32_t>(color.g) * a / 255u;
    const std::uint32_t cb = static_cast<std::uint32_t>(color.b) * a / 255u;
    for (int x = xa; x <= xb; ++x) {
        const std::uint32_t d = row[x];
        const std::uint32_t r = std::min<std::uint32_t>(255u, (d & 0xFFu) + cr);
        const std::uint32_t g = std::min<std::uint32_t>(255u, ((d >> 8) & 0xFFu) + cg);
        const std::uint32_t b = std::min<std::uint32_t>(255u, ((d >> 16) & 0xFFu) + cb);
        row[x] = r | (g << 8) | (b << 16) | (0xFFu << 24);
    }
}

void FrameBuffer::blendAddRect(int x0, int y0, int x1, int y1, const Color& color,
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
        blendAddSpan(y, x0, x1, color, alpha);
    }
}

void FrameBuffer::copyRectFrom(const FrameBuffer& src, int srcX, int srcY, int w, int h,
                               int dstX, int dstY) {
    if (w <= 0 || h <= 0) {
        return;
    }
    // 目标矩形与源列分别夹到各自画布内。
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

// 缩放拷贝：整数倍放大与缩小走最近邻；非整数倍放大按课件第 4 章的加权区域采样：
// 每个目标像素均分 3×3 子像素，查 n=9 加权表（1/16 · [1 2 1; 2 4 2; 1 2 1]），
// 子像素中心落在哪个源像素就把对应权重累加到它。表可分离，最终等价 4 邻域乘加。
void FrameBuffer::blitScaledFrom(const FrameBuffer& src) {
    if (width_ <= 0 || height_ <= 0 || src.width_ <= 0 || src.height_ <= 0) {
        return;
    }
    if (width_ == src.width_ && height_ == src.height_) {
        copyFrom(src);
        return;
    }

    const float ratioX = static_cast<float>(src.width_) / static_cast<float>(width_);
    const float ratioY = static_cast<float>(src.height_) / static_cast<float>(height_);
    const bool magnify = width_ >= src.width_ && height_ >= src.height_;
    const bool integerScale =
        (width_ % src.width_ == 0) && (height_ % src.height_ == 0);
    const bool weighted = magnify && !integerScale;

    if (!weighted) {
        std::vector<int> xmap(static_cast<std::size_t>(width_));
        for (int x = 0; x < width_; ++x) {
            xmap[static_cast<std::size_t>(x)] = x * src.width_ / width_;
        }
        for (int y = 0; y < height_; ++y) {
            const int sy = y * src.height_ / height_;
            const std::uint32_t* srcRow = src.pixels_.data() + rowOffset(src.width_, sy);
            std::uint32_t* dstRow = pixels_.data() + rowOffset(width_, y);
            for (int x = 0; x < width_; ++x) {
                dstRow[x] = srcRow[xmap[static_cast<std::size_t>(x)]];
            }
        }
        return;
    }

    // n=9 加权表每行权重为 1:2:1（中心子像素权重 4）。列/行各算一次：
    // 3 个子像素中心映射到源像素，最多落到相邻两个源像素上，权重相加。
    const int subWeight[3] = {1, 2, 1};
    std::vector<int> x0(static_cast<std::size_t>(width_));
    std::vector<int> x1(static_cast<std::size_t>(width_));
    std::vector<int> wx0(static_cast<std::size_t>(width_));
    std::vector<int> wx1(static_cast<std::size_t>(width_));
    for (int x = 0; x < width_; ++x) {
        int first = -1;
        int second = -1;
        int wFirst = 0;
        int wSecond = 0;
        for (int s = 0; s < 3; ++s) {
            const float pos =
                (static_cast<float>(x) + (static_cast<float>(s) + 0.5f) / 3.0f) * ratioX -
                0.5f;
            const int ix = std::clamp(static_cast<int>(std::floor(pos + 0.5f)), 0,
                                      src.width_ - 1);
            if (ix == first) {
                wFirst += subWeight[s];
            } else if (ix == second) {
                wSecond += subWeight[s];
            } else if (first < 0) {
                first = ix;
                wFirst = subWeight[s];
            } else {
                second = ix;
                wSecond = subWeight[s];
            }
        }
        const std::size_t xi = static_cast<std::size_t>(x);
        x0[xi] = first;
        x1[xi] = second < 0 ? first : second;
        wx0[xi] = wFirst;
        wx1[xi] = wSecond;
    }

    for (int y = 0; y < height_; ++y) {
        int y0i = -1;
        int y1i = -1;
        int wy0 = 0;
        int wy1 = 0;
        for (int s = 0; s < 3; ++s) {
            const float pos =
                (static_cast<float>(y) + (static_cast<float>(s) + 0.5f) / 3.0f) * ratioY -
                0.5f;
            const int iy = std::clamp(static_cast<int>(std::floor(pos + 0.5f)), 0,
                                      src.height_ - 1);
            if (iy == y0i) {
                wy0 += subWeight[s];
            } else if (iy == y1i) {
                wy1 += subWeight[s];
            } else if (y0i < 0) {
                y0i = iy;
                wy0 = subWeight[s];
            } else {
                y1i = iy;
                wy1 = subWeight[s];
            }
        }
        if (y1i < 0) {
            y1i = y0i;
            wy1 = 0;
        }

        const std::uint32_t* row0 = src.pixels_.data() + rowOffset(src.width_, y0i);
        const std::uint32_t* row1 = src.pixels_.data() + rowOffset(src.width_, y1i);
        std::uint32_t* dstRow = pixels_.data() + rowOffset(width_, y);

        for (int x = 0; x < width_; ++x) {
            const std::size_t xi = static_cast<std::size_t>(x);
            const int w00 = wx0[xi] * wy0;
            const int w10 = wx1[xi] * wy0;
            const int w01 = wx0[xi] * wy1;
            const int w11 = wx1[xi] * wy1; // 四权重和恒为 16

            const std::uint32_t c00 = row0[x0[xi]];
            const std::uint32_t c10 = row0[x1[xi]];
            const std::uint32_t c01 = row1[x0[xi]];
            const std::uint32_t c11 = row1[x1[xi]];

            const int r = (static_cast<int>(c00 & 0xFFu) * w00 +
                           static_cast<int>(c10 & 0xFFu) * w10 +
                           static_cast<int>(c01 & 0xFFu) * w01 +
                           static_cast<int>(c11 & 0xFFu) * w11 + 8) >> 4;
            const int g = (static_cast<int>((c00 >> 8) & 0xFFu) * w00 +
                           static_cast<int>((c10 >> 8) & 0xFFu) * w10 +
                           static_cast<int>((c01 >> 8) & 0xFFu) * w01 +
                           static_cast<int>((c11 >> 8) & 0xFFu) * w11 + 8) >> 4;
            const int b = (static_cast<int>((c00 >> 16) & 0xFFu) * w00 +
                           static_cast<int>((c10 >> 16) & 0xFFu) * w10 +
                           static_cast<int>((c01 >> 16) & 0xFFu) * w01 +
                           static_cast<int>((c11 >> 16) & 0xFFu) * w11 + 8) >> 4;

            dstRow[x] = static_cast<std::uint32_t>(r) |
                        (static_cast<std::uint32_t>(g) << 8) |
                        (static_cast<std::uint32_t>(b) << 16) | (0xFFu << 24);
        }
    }
}

} // namespace cg
