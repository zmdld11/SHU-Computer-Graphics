#pragma once
#include <cstdint>

namespace cg {

struct Color {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;

    constexpr Color() = default;
    constexpr Color(std::uint8_t r_, std::uint8_t g_, std::uint8_t b_)
        : r(r_), g(g_), b(b_) {}

    static constexpr Color Black() { return {0, 0, 0}; }
    static constexpr Color White() { return {255, 255, 255}; }
    static constexpr Color Red()   { return {255, 0, 0}; }
    static constexpr Color Green() { return {0, 255, 0}; }
    static constexpr Color Blue()  { return {0, 0, 255}; }
    static constexpr Color Yellow(){ return {255, 255, 0}; }
    static constexpr Color Gray()  { return {128, 128, 128}; }
    static constexpr Color Cyan()  { return {0, 220, 220}; }
    static constexpr Color Magenta() { return {230, 60, 200}; }
    static constexpr Color Orange() { return {240, 150, 40}; }

    constexpr bool operator==(const Color& o) const {
        return r == o.r && g == o.g && b == o.b;
    }
    constexpr bool operator!=(const Color& o) const { return !(*this == o); }
};

} // namespace cg
