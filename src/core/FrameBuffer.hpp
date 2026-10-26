// FrameBuffer.hpp — CPU 像素画板；putPixel 是全项目唯一的画点入口。
#pragma once
#include "core/Color.hpp"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace cg {

// 软件光栅化的底座：所有手写算法最终都落到 putPixel。
class FrameBuffer {
public:
    FrameBuffer(int width, int height);

    // 窗口尺寸变化时重设画布（内容清空）
    void resize(int width, int height);

    // 拷贝另一块画布的内容（尺寸不同则先重建）。
    void copyFrom(const FrameBuffer& other);

    int width() const  { return width_; }
    int height() const { return height_; }

    void clear(const Color& color = Color::Black());

    // 唯一画点入口；行 0 在顶部，越界忽略。内联：每像素都会调用。
    void putPixel(int x, int y, const Color& color) {
        assertSizeInvariant();
        if (x < 0 || x >= width_ || y < 0 || y >= height_) {
            return;
        }
        const std::size_t i = static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) +
                              static_cast<std::size_t>(x);
        pixels_[i] = pack(color);
    }

    // 半透明画点（src-over）：dst = src*α + dst*(1-α)；255 等价 putPixel。
    void blendPixel(int x, int y, const Color& color, std::uint8_t alpha) {
        assertSizeInvariant();
        if (x < 0 || x >= width_ || y < 0 || y >= height_ || alpha == 0) {
            return;
        }
        const std::size_t i = static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) +
                              static_cast<std::size_t>(x);
        if (alpha == 255) {
            pixels_[i] = pack(color);
            return;
        }
        const std::uint32_t d = pixels_[i];
        const std::uint32_t dr = d & 0xFFu;
        const std::uint32_t dg = (d >> 8) & 0xFFu;
        const std::uint32_t db = (d >> 16) & 0xFFu;
        const std::uint32_t a = alpha;
        const std::uint32_t inv = 255u - a;
        const std::uint32_t r = (static_cast<std::uint32_t>(color.r) * a + dr * inv + 127) / 255u;
        const std::uint32_t g = (static_cast<std::uint32_t>(color.g) * a + dg * inv + 127) / 255u;
        const std::uint32_t b = (static_cast<std::uint32_t>(color.b) * a + db * inv + 127) / 255u;
        pixels_[i] = static_cast<std::uint32_t>(r) | (g << 8) | (b << 16) | (0xFFu << 24);
    }

    Color pixel(int x, int y) const {
        assertSizeInvariant();
        if (x < 0 || x >= width_ || y < 0 || y >= height_) {
            return Color::Black();
        }
        const std::uint32_t v =
            pixels_[static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) +
                    static_cast<std::size_t>(x)];
        return Color{static_cast<std::uint8_t>(v & 0xFFu),
                     static_cast<std::uint8_t>((v >> 8) & 0xFFu),
                     static_cast<std::uint8_t>((v >> 16) & 0xFFu)};
    }

    // 填充闭区间矩形 [x0,x1]×[y0,y1]，自动夹边界。
    void fillRect(int x0, int y0, int x1, int y1, const Color& color);

    // 填充第 y 行闭区间 [x0,x1]；扫描线/种子填充按 span 落色。
    void fillSpan(int y, int x0, int x1, const Color& color);

    // 半透明整行（src-over）。
    void blendSpan(int y, int x0, int x1, const Color& color, std::uint8_t alpha);

    // 半透明矩形，自动夹边界。
    void blendRect(int x0, int y0, int x1, int y1, const Color& color, std::uint8_t alpha);

    // 整行拷贝（闭区间，自动夹边界）。
    void copySpanFrom(const FrameBuffer& src, int y, int x0, int x1);

    // 任意矩形块拷贝，自动夹边界。
    void copyRectFrom(const FrameBuffer& src, int srcX, int srcY, int w, int h,
                      int dstX, int dstY);

    // RGBA 行主序数据，供 Canvas 上屏
    const std::uint8_t* data() const {
        return reinterpret_cast<const std::uint8_t*>(pixels_.data());
    }

private:
    // 打包成小端 RGBA：低字节到高字节为 r/g/b/a。
    static std::uint32_t pack(const Color& c) {
        return static_cast<std::uint32_t>(c.r) |
               (static_cast<std::uint32_t>(c.g) << 8) |
               (static_cast<std::uint32_t>(c.b) << 16) | (0xFFu << 24);
    }

    // 尺寸与缓冲必须同步，否则下标守卫失效。
    void assertSizeInvariant() const {
        assert(pixels_.size() == static_cast<std::size_t>(width_) *
                                     static_cast<std::size_t>(height_));
    }

    int width_ = 0;
    int height_ = 0;
    std::vector<std::uint32_t> pixels_; // 每个元素 = 一个 RGBA 像素（小端字节序）
};

} // namespace cg
