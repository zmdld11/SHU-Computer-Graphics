#include "core/FrameBuffer.hpp"

namespace cg {

FrameBuffer::FrameBuffer(int width, int height)
    : width_(width), height_(height),
      pixels_(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4, 0) {}

void FrameBuffer::resize(int width, int height) {
    width_ = width;
    height_ = height;
    pixels_.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4, 0);
}

void FrameBuffer::clear(const Color& color) {
    for (std::size_t i = 0; i + 3 < pixels_.size(); i += 4) {
        pixels_[i + 0] = color.r;
        pixels_[i + 1] = color.g;
        pixels_[i + 2] = color.b;
        pixels_[i + 3] = 255;
    }
}

void FrameBuffer::putPixel(int x, int y, const Color& color) {
    if (x < 0 || x >= width_ || y < 0 || y >= height_) {
        return;
    }
    const std::size_t i = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x)) * 4;
    pixels_[i + 0] = color.r;
    pixels_[i + 1] = color.g;
    pixels_[i + 2] = color.b;
    pixels_[i + 3] = 255;
}

Color FrameBuffer::pixel(int x, int y) const {
    if (x < 0 || x >= width_ || y < 0 || y >= height_) {
        return Color::Black();
    }
    const std::size_t i = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x)) * 4;
    return Color{pixels_[i + 0], pixels_[i + 1], pixels_[i + 2]};
}

} // namespace cg
