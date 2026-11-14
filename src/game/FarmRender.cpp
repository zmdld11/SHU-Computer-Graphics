// FarmRender.cpp
// 实现农场场景的纯矢量绘制。地图先画进 2× 画布再 2×2 均值降采样，
// 让中点线/扫描线填充的边缘在最终 1× 图上保持干净；每帧只做一次窗口拷贝。
#include "game/FarmRender.hpp"

#include "curves/BSpline.hpp"
#include "curves/Bezier.hpp"
#include "raster/Fill.hpp"
#include "raster/Rasterizer.hpp"
#include "ui/Text.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <utility>
#include <vector>

namespace cg::game {
namespace {

constexpr float kPi = 3.14159265358979f;

// ---- 调色板：暖色、低饱和，保证纯色叠放也有层次 ----
constexpr Color kGrass{118, 170, 88};
constexpr Color kGrassLight{138, 188, 102};
constexpr Color kGrassDark{98, 148, 74};
constexpr Color kGrassDeep{80, 126, 62};
constexpr Color kBorderGrass{70, 112, 56};

constexpr Color kPath{188, 156, 102};
constexpr Color kPathLight{204, 172, 116};
constexpr Color kPathDark{156, 124, 80};

constexpr Color kSoil{126, 92, 68};
constexpr Color kSoilDark{96, 66, 46};
constexpr Color kSoilLight{154, 116, 86};
constexpr Color kSoilWet{92, 62, 44};

constexpr Color kOutline{62, 46, 32};
constexpr Color kWood{150, 104, 58};
constexpr Color kWoodDark{104, 70, 40};
constexpr Color kWoodLight{182, 134, 80};

constexpr Color kWaterDeep{40, 104, 142};
constexpr Color kWaterMid{54, 132, 168};
constexpr Color kWaterLight{74, 164, 194};
constexpr Color kWaterShimmer{132, 210, 226};
constexpr Color kSand{216, 194, 146};
constexpr Color kSandDark{186, 160, 112};
constexpr Color kWaterRim{36, 88, 116};

constexpr Color kLeafDark{46, 96, 54};
constexpr Color kLeaf{62, 122, 64};
constexpr Color kLeafLight{88, 156, 80};
constexpr Color kLeafHi{116, 182, 96};
constexpr Color kLeafDarkB{40, 82, 50};
constexpr Color kLeafB{54, 108, 60};
constexpr Color kLeafLightB{76, 138, 74};
constexpr Color kTrunk{112, 72, 42};
constexpr Color kTrunkDark{78, 48, 28};

constexpr Color kCream{255, 244, 214};
constexpr Color kCreamShade{230, 204, 154};
constexpr Color kDragon{249, 199, 70};
constexpr Color kDragonShade{230, 164, 46};
constexpr Color kCheek{243, 152, 72};
constexpr Color kPink{246, 186, 196};
constexpr Color kFlowerYellow{250, 218, 96};
constexpr Color kWhite{255, 252, 240};
constexpr Color kStone{156, 158, 148};
constexpr Color kStoneLight{188, 190, 178};
constexpr Color kStoneDark{118, 120, 112};
constexpr Color kFish{44, 96, 118};
constexpr Color kFishLight{92, 148, 158};

// ---- 确定性伪随机：同一格每次构建都长同样的草，画面不会闪 ----
std::uint32_t hash2(int x, int y) {
    std::uint32_t h = static_cast<std::uint32_t>(x) * 374761393u +
                      static_cast<std::uint32_t>(y) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

float hashSigned(std::uint32_t h, int bits, float amp) {
    const std::uint32_t v = (h >> bits) & 0xFFu;
    return (static_cast<float>(v) / 255.0f - 0.5f) * 2.0f * amp;
}

Color darken(const Color& c, float factor) {
    return Color{static_cast<std::uint8_t>(c.r * factor),
                 static_cast<std::uint8_t>(c.g * factor),
                 static_cast<std::uint8_t>(c.b * factor)};
}

Color lerpColor(const Color& a, const Color& b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    auto mix = [t](std::uint8_t x, std::uint8_t y) {
        return static_cast<std::uint8_t>(static_cast<float>(x) +
                                         (static_cast<float>(y) - static_cast<float>(x)) * t);
    };
    return Color{mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b)};
}

// 平滑阶跃（Hermite）：[e0,e1] 映射到 0..1，两端导数为 0，用来做影子的淡入淡出。
float smoothStep(float e0, float e1, float x) {
    const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// 带缩放与偏移的小画笔：世界坐标一律按设计尺寸写，2× 超采样只改 scale；
// 偏移用于把一小块区域（农田层）单独烘到局部画布上，坐标不需要换算。
class Painter {
public:
    Painter(FrameBuffer& fb, float scale, FPoint offset = {0.0f, 0.0f})
        : fb_(fb), scale_(scale), ox_(offset.x), oy_(offset.y) {}

    int px(float v) const {
        return static_cast<int>(std::lround((v - ox_) * scale_));
    }
    int py(float v) const {
        return static_cast<int>(std::lround((v - oy_) * scale_));
    }
    float scale() const { return scale_; }
    // 像素 → 设计坐标的逆映射（py 的反函数），影子蒙版按目标行反推源行用。
    float unpy(int v) const {
        return static_cast<float>(v) / scale_ + oy_;
    }
    FrameBuffer& fb() const { return fb_; }
    Color sample(float x, float y) const { return fb_.pixel(px(x), py(y)); }

    void rect(float x, float y, float w, float h, const Color& c,
              std::uint8_t alpha = 255) {
        if (w <= 0.0f || h <= 0.0f) {
            return;
        }
        if (alpha >= 255) {
            fb_.fillRect(px(x), py(y), px(x + w) - 1, py(y + h) - 1, c);
        } else {
            fb_.blendRect(px(x), py(y), px(x + w) - 1, py(y + h) - 1, c, alpha);
        }
    }

    void line(float x0, float y0, float x1, float y1, const Color& c, float width = 1.0f) {
        int w = std::max(1, static_cast<int>(std::lround(width * scale_)));
        if ((w & 1) == 0) {
            ++w;
        }
        raster::drawLine(fb_, {px(x0), py(y0)}, {px(x1), py(y1)}, c, {0xFFFFu, w});
    }

    void rectOutline(float x, float y, float w, float h, const Color& c, float width) {
        line(x, y, x + w, y, c, width);
        line(x + w, y, x + w, y + h, c, width);
        line(x + w, y + h, x, y + h, c, width);
        line(x, y + h, x, y, c, width);
    }

    void ellipse(float cx, float cy, float rx, float ry, const Color& c, int segments = 40,
                 std::uint8_t alpha = 255) {
        if (rx <= 0.0f || ry <= 0.0f) {
            return;
        }
        const int n = std::max(12, segments);
        std::vector<IPoint> pts;
        pts.reserve(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i) {
            const float a = 2.0f * kPi * static_cast<float>(i) / static_cast<float>(n);
            pts.push_back({px(cx + rx * std::cos(a)), py(cy + ry * std::sin(a))});
        }
        raster::scanlineFillPolygon(fb_, pts, c, alpha);
    }

    // 椭圆内做左上→右下的双色渐变：水面靠岸偏浅、中心偏深，比一圈圈硬色带自然。
    void ellipseGradient(float cx, float cy, float rx, float ry, const Color& shallow,
                         const Color& deep) {
        const int cxi = px(cx);
        const int cyi = py(cy);
        const int rxi = px(rx);
        const int ryi = px(ry);
        if (rxi <= 0 || ryi <= 0) {
            return;
        }
        for (int y = -ryi; y <= ryi; ++y) {
            const float yn = static_cast<float>(y) / static_cast<float>(ryi);
            const float halfF = static_cast<float>(rxi) *
                                std::sqrt(std::max(0.0f, 1.0f - yn * yn));
            const int half = static_cast<int>(halfF);
            for (int x = -half; x <= half; ++x) {
                const float xn = static_cast<float>(x) / static_cast<float>(rxi);
                const float t = 0.5f + 0.32f * xn + 0.34f * yn;
                fb_.putPixel(cxi + x, cyi + y, lerpColor(shallow, deep, t));
            }
        }
    }

    void lineLoopEllipse(float cx, float cy, float rx, float ry, const Color& c,
                         float width, int segments = 40) {
        const int n = std::max(12, segments);
        float px0 = cx + rx;
        float py0 = cy;
        for (int i = 1; i <= n; ++i) {
            const float a = 2.0f * kPi * static_cast<float>(i) / static_cast<float>(n);
            const float px1 = cx + rx * std::cos(a);
            const float py1 = cy + ry * std::sin(a);
            line(px0, py0, px1, py1, c, width);
            px0 = px1;
            py0 = py1;
        }
    }

    void ellipseOutline(float cx, float cy, float rx, float ry, const Color& fill,
                        const Color& outline, float width, int segments = 40) {
        ellipse(cx, cy, rx, ry, fill, segments);
        lineLoopEllipse(cx, cy, rx, ry, outline, width, segments);
    }

    void polygon(const std::vector<FPoint>& pts, const Color& fill, const Color& outline,
                 float width, std::uint8_t alpha = 255) {
        if (pts.size() < 3) {
            return;
        }
        std::vector<IPoint> ip;
        ip.reserve(pts.size());
        for (const FPoint& p : pts) {
            ip.push_back({px(p.x), py(p.y)});
        }
        raster::scanlineFillPolygon(fb_, ip, fill, alpha);
        if (width > 0.0f) {
            for (std::size_t i = 0; i < pts.size(); ++i) {
                const FPoint& a = pts[i];
                const FPoint& b = pts[(i + 1) % pts.size()];
                line(a.x, a.y, b.x, b.y, outline, width);
            }
        }
    }

    void bezier(FPoint p0, FPoint p1, FPoint p2, const Color& c, float width, int segments) {
        const std::vector<FPoint> cps{p0, p1, p2};
        const std::vector<FPoint> pts = curves::sampleBezier(cps, segments);
        for (std::size_t i = 1; i < pts.size(); ++i) {
            line(pts[i - 1].x, pts[i - 1].y, pts[i].x, pts[i].y, c, width);
        }
    }

    void arc(float cx, float cy, float rx, float ry, float a0, float a1, const Color& c,
             float width, int segments = 16) {
        const int n = std::max(4, segments);
        float px0 = cx + rx * std::cos(a0);
        float py0 = cy + ry * std::sin(a0);
        for (int i = 1; i <= n; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(n);
            const float a = a0 + (a1 - a0) * t;
            const float px1 = cx + rx * std::cos(a);
            const float py1 = cy + ry * std::sin(a);
            line(px0, py0, px1, py1, c, width);
            px0 = px1;
            py0 = py1;
        }
    }

    // 径向衰减的加色光点：自发光专用（萤火虫、水面碎光）。
    // 辐射强度按 (1-d²)² 衰减，中心过曝、边缘无硬边，叠两三层就有"亮起来"的感觉。
    void glow(float cx, float cy, float radius, const Color& c, float intensity) {
        if (radius <= 0.0f || intensity <= 0.0f) {
            return;
        }
        const int cxi = px(cx);
        const int cyi = py(cy);
        const int r = std::max(1, static_cast<int>(std::lround(radius * scale_)));
        for (int dy = -r; dy <= r; ++dy) {
            for (int dx = -r; dx <= r; ++dx) {
                const float d2 = static_cast<float>(dx * dx + dy * dy) /
                                 static_cast<float>(r * r);
                if (d2 >= 1.0f) {
                    continue;
                }
                const float w = 1.0f - d2;
                const int a = static_cast<int>(w * w * intensity * 255.0f);
                if (a <= 0) {
                    continue;
                }
                fb_.blendAddPixel(cxi + dx, cyi + dy, c,
                                  static_cast<std::uint8_t>(std::min(255, a)));
            }
        }
    }

private:
    FrameBuffer& fb_;
    float scale_;
    float ox_ = 0.0f;
    float oy_ = 0.0f;
};

// ---------------- 草地、土路、农田 ----------------

void drawGrassTuft(Painter& p, float x, float y, float scale, const Color& c) {
    p.line(x, y, x - 4.0f * scale, y - 9.0f * scale, c, 1.0f);
    p.line(x, y, x + 0.5f * scale, y - 12.0f * scale, c, 1.0f);
    p.line(x, y, x + 4.0f * scale, y - 8.0f * scale, c, 1.0f);
}

void drawFlower(Painter& p, float x, float y, float scale, const Color& petal) {
    p.line(x, y, x, y + 4.0f * scale, kLeafDark, 1.0f);
    const float r = 2.4f * scale;
    p.ellipse(x - 2.6f * scale, y, r, r * 0.8f, petal, 10);
    p.ellipse(x + 2.6f * scale, y, r, r * 0.8f, petal, 10);
    p.ellipse(x, y - 2.6f * scale, r * 0.8f, r, petal, 10);
    p.ellipse(x, y + 2.6f * scale, r * 0.8f, r, petal, 10);
    p.ellipse(x, y, 1.5f * scale, 1.5f * scale, kFlowerYellow, 10);
}

void drawStone(Painter& p, float x, float y, float r, const Color& shadow) {
    p.ellipse(x + 2.0f, y + 2.0f, r, r * 0.7f, shadow, 16);
    p.ellipse(x, y, r, r * 0.72f, kStoneDark, 16);
    p.ellipse(x - 0.5f, y - 1.0f, r * 0.86f, r * 0.62f, kStone, 16);
    p.ellipse(x - r * 0.3f, y - r * 0.35f, r * 0.38f, r * 0.24f, kStoneLight, 12);
}

void drawMushroom(Painter& p, float x, float y, float scale) {
    p.ellipse(x + 1.0f, y + 1.0f, 6.0f * scale, 2.0f * scale, kGrassDeep, 12);
    p.rect(x - 1.5f * scale, y - 5.0f * scale, 3.0f * scale, 5.0f * scale, kCreamShade);
    p.ellipse(x, y - 6.0f * scale, 6.5f * scale, 4.0f * scale, Color(206, 82, 66), 16);
    p.ellipse(x - 2.0f * scale, y - 7.0f * scale, 1.0f * scale, 0.9f * scale, kCream, 8);
    p.ellipse(x + 2.2f * scale, y - 6.2f * scale, 0.9f * scale, 0.8f * scale, kCream, 8);
}

void drawBush(Painter& p, float x, float y, float scale) {
    p.ellipse(x + 3.0f * scale, y + 2.0f * scale, 22.0f * scale, 7.0f * scale, kGrassDeep,
              20);
    p.ellipse(x, y - 10.0f * scale, 21.0f * scale, 14.0f * scale, kLeafDark, 24);
    p.ellipse(x - 11.0f * scale, y - 12.0f * scale, 12.0f * scale, 10.0f * scale, kLeaf, 20);
    p.ellipse(x + 12.0f * scale, y - 11.0f * scale, 12.0f * scale, 10.0f * scale, kLeaf, 20);
    p.ellipse(x - 1.0f * scale, y - 18.0f * scale, 14.0f * scale, 9.0f * scale, kLeafLight,
              20);
    p.ellipse(x - 5.0f * scale, y - 20.0f * scale, 7.0f * scale, 4.0f * scale, kLeafHi, 14);
}

void drawTree(Painter& p, float x, float y, float scale, bool variant) {
    const Color dark = variant ? kLeafDarkB : kLeafDark;
    const Color mid = variant ? kLeafB : kLeaf;
    const Color light = variant ? kLeafLightB : kLeafLight;
    const Color hi = variant ? Color(132, 194, 110) : kLeafHi;

    p.ellipse(x + 4.0f * scale, y + 2.0f * scale, 54.0f * scale, 16.0f * scale, kGrassDeep,
              32);
    const std::vector<FPoint> trunk = {{x - 7.0f * scale, y},
                                       {x + 7.0f * scale, y},
                                       {x + 5.0f * scale, y - 34.0f * scale},
                                       {x - 5.0f * scale, y - 34.0f * scale}};
    p.polygon(trunk, kTrunk, kOutline, 1.5f);
    p.line(x - 3.0f * scale, y - 2.0f * scale, x - 3.0f * scale, y - 28.0f * scale,
           Color(146, 100, 60), 1.5f);

    p.ellipse(x, y - 58.0f * scale, 50.0f * scale, 46.0f * scale, dark, 36);
    p.ellipse(x - 7.0f * scale, y - 62.0f * scale, 45.0f * scale, 41.0f * scale, mid, 36);
    p.ellipse(x - 28.0f * scale, y - 58.0f * scale, 21.0f * scale, 19.0f * scale, mid, 24);
    p.ellipse(x + 26.0f * scale, y - 61.0f * scale, 22.0f * scale, 20.0f * scale, mid, 24);
    p.ellipse(x - 12.0f * scale, y - 82.0f * scale, 25.0f * scale, 21.0f * scale, light, 24);
    p.ellipse(x + 14.0f * scale, y - 78.0f * scale, 22.0f * scale, 19.0f * scale, light, 24);
    p.ellipse(x - 24.0f * scale, y - 72.0f * scale, 13.0f * scale, 10.0f * scale, hi, 18);
    p.ellipse(x + 2.0f * scale, y - 90.0f * scale, 15.0f * scale, 10.0f * scale, hi, 18);
    p.ellipse(x + 26.0f * scale, y - 70.0f * scale, 8.0f * scale, 6.0f * scale, light, 14);
}

void drawCrop(Painter& p, FPoint c, const Crop& crop) {
    if (crop.stage == CropStage::Empty) {
        return;
    }
    const float bx = c.x;
    const float by = c.y + 26.0f;

    if (crop.stage == CropStage::Seed) {
        p.ellipse(bx - 10.0f, by, 7.0f, 4.0f, kSoilDark, 12);
        p.ellipse(bx + 9.0f, by, 7.0f, 4.0f, kSoilDark, 12);
        p.ellipse(bx, by + 1.0f, 8.0f, 4.0f, kSoilDark, 12);
        for (int i = 0; i < 3; ++i) {
            const float sx = bx + (static_cast<float>(i) - 1.0f) * 9.0f;
            p.line(sx, by - 4.0f, sx, by - 11.0f, kLeaf, 1.5f);
            p.ellipse(sx, by - 12.0f, 2.2f, 1.6f, kLeafLight, 8);
        }
        return;
    }

    p.ellipse(bx + 3.0f, by + 1.0f, 20.0f, 5.0f, kSoilDark, 20);

    if (crop.stage == CropStage::Sprout) {
        p.ellipseOutline(bx, by, 10.0f, 3.0f, kSoilDark, kSoilDark, 0.0f, 14);
        p.line(bx, by, bx, by - 16.0f, kLeafDark, 2.0f);
        p.ellipseOutline(bx - 6.0f, by - 14.0f, 7.0f, 4.0f, kLeaf, kLeafDark, 1.0f, 16);
        p.ellipseOutline(bx + 6.0f, by - 17.0f, 7.0f, 4.0f, kLeafLight, kLeafDark, 1.0f, 16);
        p.line(bx - 6.0f, by - 14.0f, bx - 11.0f, by - 15.0f, kLeafDark, 1.0f);
        p.line(bx + 6.0f, by - 17.0f, bx + 12.0f, by - 19.0f, kLeafDark, 1.0f);
        return;
    }

    if (crop.kind == Seed::Cabbage) {
        // 白菜：外层深色叶摊开，内层浅色叶抱住奶白色的菜心。
        const std::vector<FPoint> leftOuter = {{bx - 2.0f, by - 3.0f},
                                               {bx - 22.0f, by - 9.0f},
                                               {bx - 29.0f, by - 19.0f},
                                               {bx - 13.0f, by - 17.0f},
                                               {bx - 3.0f, by - 12.0f}};
        const std::vector<FPoint> rightOuter = {{bx + 2.0f, by - 3.0f},
                                                {bx + 22.0f, by - 9.0f},
                                                {bx + 29.0f, by - 19.0f},
                                                {bx + 13.0f, by - 17.0f},
                                                {bx + 3.0f, by - 12.0f}};
        const std::vector<FPoint> backOuter = {{bx - 14.0f, by - 14.0f},
                                               {bx - 6.0f, by - 35.0f},
                                               {bx + 7.0f, by - 34.0f},
                                               {bx + 15.0f, by - 13.0f},
                                               {bx, by - 20.0f}};
        p.polygon(backOuter, kLeafDark, kLeafDark, 1.2f);
        p.polygon(leftOuter, kLeaf, kLeafDark, 1.2f);
        p.polygon(rightOuter, kLeaf, kLeafDark, 1.2f);

        const std::vector<FPoint> leftMid = {{bx - 1.0f, by - 7.0f},
                                             {bx - 17.0f, by - 13.0f},
                                             {bx - 20.0f, by - 24.0f},
                                             {bx - 7.0f, by - 22.0f},
                                             {bx + 1.0f, by - 15.0f}};
        const std::vector<FPoint> rightMid = {{bx + 1.0f, by - 7.0f},
                                              {bx + 17.0f, by - 13.0f},
                                              {bx + 20.0f, by - 24.0f},
                                              {bx + 7.0f, by - 22.0f},
                                              {bx - 1.0f, by - 15.0f}};
        p.polygon(leftMid, kLeafLight, kLeafDark, 1.0f);
        p.polygon(rightMid, kLeafLight, kLeafDark, 1.0f);
        p.ellipse(bx, by - 18.0f, 15.0f, 12.0f, kLeafLight, 24);

        const std::vector<FPoint> leftFront = {{bx - 2.0f, by - 9.0f},
                                               {bx - 13.0f, by - 20.0f},
                                               {bx - 8.0f, by - 31.0f},
                                               {bx + 1.0f, by - 22.0f}};
        const std::vector<FPoint> rightFront = {{bx + 2.0f, by - 9.0f},
                                                {bx + 13.0f, by - 20.0f},
                                                {bx + 8.0f, by - 31.0f},
                                                {bx - 1.0f, by - 22.0f}};
        p.polygon(leftFront, kLeafHi, kLeafDark, 1.0f);
        p.polygon(rightFront, kLeafHi, kLeafDark, 1.0f);

        p.ellipseOutline(bx, by - 22.0f, 9.0f, 11.0f, Color(228, 238, 192),
                         kLeafDark, 1.4f, 24);
        p.ellipse(bx - 2.5f, by - 25.0f, 3.2f, 4.0f, Color(244, 248, 218), 14);
        p.line(bx - 7.0f, by - 19.0f, bx - 2.0f, by - 16.0f, kLeafHi, 1.0f);
        p.line(bx + 7.0f, by - 20.0f, bx + 2.0f, by - 16.0f, kLeafHi, 1.0f);
    } else if (crop.kind == Seed::Tomato) {
        // 番茄：一根主茎挂三颗红果，果子顶部有绿色萼片。
        p.line(bx, by - 2.0f, bx + 1.0f, by - 46.0f, kLeafDark, 2.4f);
        p.ellipse(bx - 7.0f, by - 15.0f, 8.0f, 4.0f, kLeaf, 16);
        p.ellipse(bx + 8.0f, by - 24.0f, 8.0f, 4.0f, kLeafLight, 16);
        p.ellipse(bx - 7.0f, by - 33.0f, 7.0f, 3.5f, kLeaf, 14);
        p.ellipse(bx + 6.0f, by - 42.0f, 6.0f, 3.0f, kLeafLight, 14);
        p.line(bx + 1.0f, by - 10.0f, bx + 10.0f, by - 13.0f, kLeafDark, 1.0f);
        p.line(bx + 1.0f, by - 28.0f, bx - 10.0f, by - 31.0f, kLeafDark, 1.0f);

        const Color tomato = Color(206, 76, 54);
        const Color tomatoDark = Color(148, 44, 34);
        const Color tomatoHi = Color(240, 132, 104);
        const float fruitX[3] = {bx - 12.0f, bx + 11.0f, bx - 1.0f};
        const float fruitY[3] = {by - 19.0f, by - 29.0f, by - 44.0f};
        const float fruitR[3] = {8.0f, 7.5f, 7.0f};
        for (int i = 0; i < 3; ++i) {
            p.ellipseOutline(fruitX[i], fruitY[i], fruitR[i], fruitR[i] * 0.95f, tomato,
                             tomatoDark, 1.2f, 22);
            p.ellipse(fruitX[i] - 2.4f, fruitY[i] - 2.6f, 2.4f, 2.0f, tomatoHi, 10);
            p.line(fruitX[i] - 4.5f, fruitY[i] - fruitR[i] + 1.0f,
                   fruitX[i], fruitY[i] - fruitR[i] - 3.0f, kLeafDark, 1.8f);
            p.line(fruitX[i] + 4.5f, fruitY[i] - fruitR[i] + 1.0f,
                   fruitX[i], fruitY[i] - fruitR[i] - 3.0f, kLeafDark, 1.8f);
            p.line(fruitX[i], fruitY[i] - fruitR[i] + 1.0f,
                   fruitX[i] + 1.0f, fruitY[i] - fruitR[i] - 4.0f, kLeaf, 1.6f);
        }
        drawFlower(p, bx + 12.0f, by - 50.0f, 0.65f, kFlowerYellow);
    } else {
        // 胡萝卜：橙色根肩露出土面，上面是一丛羽状叶。
        p.line(bx, by - 12.0f, bx - 19.0f, by - 43.0f, kLeafDark, 1.8f);
        p.line(bx, by - 12.0f, bx - 12.0f, by - 50.0f, kLeaf, 1.6f);
        p.line(bx, by - 12.0f, bx - 4.0f, by - 54.0f, kLeafDark, 1.8f);
        p.line(bx, by - 12.0f, bx + 5.0f, by - 55.0f, kLeaf, 1.6f);
        p.line(bx, by - 12.0f, bx + 13.0f, by - 49.0f, kLeafDark, 1.8f);
        p.line(bx, by - 12.0f, bx + 18.0f, by - 40.0f, kLeaf, 1.5f);

        p.ellipse(bx - 14.0f, by - 30.0f, 8.0f, 3.2f, kLeaf, 14);
        p.ellipse(bx - 19.0f, by - 39.0f, 6.0f, 2.6f, kLeafLight, 12);
        p.ellipse(bx - 9.0f, by - 42.0f, 7.0f, 3.0f, kLeafLight, 14);
        p.ellipse(bx - 2.0f, by - 48.0f, 7.0f, 3.0f, kLeaf, 14);
        p.ellipse(bx + 7.0f, by - 47.0f, 6.0f, 2.6f, kLeafLight, 12);
        p.ellipse(bx + 15.0f, by - 36.0f, 7.0f, 3.0f, kLeaf, 14);
        p.ellipse(bx + 12.0f, by - 25.0f, 6.0f, 2.6f, kLeafLight, 12);

        const Color carrot = Color(226, 128, 48);
        const Color carrotDark = Color(166, 82, 28);
        const Color carrotHi = Color(244, 164, 86);
        const std::vector<FPoint> root = {{bx - 13.0f, by - 12.0f},
                                          {bx + 13.0f, by - 12.0f},
                                          {bx + 8.0f, by - 2.0f},
                                          {bx + 2.0f, by + 9.0f},
                                          {bx - 2.0f, by + 9.0f},
                                          {bx - 8.0f, by - 2.0f}};
        p.polygon(root, carrot, carrotDark, 1.4f);
        p.ellipse(bx, by - 12.0f, 13.0f, 5.0f, carrot, 22);
        p.ellipse(bx - 4.0f, by - 13.0f, 4.5f, 2.2f, carrotHi, 12);
        p.line(bx - 10.0f, by - 5.0f, bx + 10.0f, by - 5.0f, carrotDark, 1.2f);
        p.line(bx - 7.0f, by + 1.0f, bx + 7.0f, by + 1.0f, carrotDark, 1.0f);
        p.line(bx - 5.0f, by - 10.0f, bx - 8.0f, by - 16.0f, kLeafDark, 1.4f);
        p.line(bx + 5.0f, by - 10.0f, bx + 9.0f, by - 16.0f, kLeafDark, 1.4f);
    }
}

std::vector<FPoint> bedPolygon(const RectI& r, float inset, float amp,
                               std::uint32_t seed) {
    const float x0 = static_cast<float>(r.x) + inset;
    const float y0 = static_cast<float>(r.y) + inset;
    const float x1 = static_cast<float>(r.x + r.w) - inset;
    const float y1 = static_cast<float>(r.y + r.h) - inset;
    const float mx = (x0 + x1) * 0.5f;
    const float my = (y0 + y1) * 0.5f;
    auto j = [seed, amp](int k) {
        const std::uint32_t h = hash2(static_cast<int>(seed), k * 17 + 3);
        return (static_cast<float>(h & 0xFFu) / 255.0f - 0.5f) * 2.0f * amp;
    };
    return {{x0 + j(0), y0 + j(1)}, {mx, y0 + j(2)},     {x1 + j(3), y0 + j(4)},
            {x1 + j(5), my},       {x1 + j(6), y1 + j(7)}, {mx, y1 + j(8)},
            {x0 + j(9), y1 + j(10)}, {x0 + j(11), my}};
}

void drawPlot(Painter& p, int index, const Crop& crop) {
    const RectI r = plotRect(index);
    // 草地上的土床：深土色窄边 + 土面；边缘轻微起伏，避免像一块块巧克力。
    p.rect(static_cast<float>(r.x + 4), static_cast<float>(r.y + 5), static_cast<float>(r.w),
           static_cast<float>(r.h), kGrassDeep);
    const std::vector<FPoint> bed = bedPolygon(r, 0.0f, 3.0f, static_cast<std::uint32_t>(index) * 7u + 1u);
    p.polygon(bed, Color(84, 56, 36), Color(84, 56, 36), 0.0f);
    const std::vector<FPoint> soil =
        bedPolygon(r, 3.0f, 2.0f, static_cast<std::uint32_t>(index) * 7u + 4u);
    p.polygon(soil, crop.watered ? kSoilWet : kSoil, crop.watered ? kSoilWet : kSoil, 0.0f);

    // 短而错落的垄沟 + 土块：不画贯穿整块田的长线，避免读成一排木板。
    std::uint32_t h = hash2(index * 13 + 7, index * 5 + 3);
    for (int i = 0; i < 5; ++i) {
        const float fy = static_cast<float>(r.y + 16 + i * 15 + static_cast<int>(h % 3));
        const float fx = static_cast<float>(r.x + 10 + static_cast<int>((h >> (i * 3)) & 0x1Fu));
        const float len = 24.0f + static_cast<float>((h >> (i * 2)) & 0x1Fu);
        p.rect(fx, fy, len, 2.5f, kSoilDark);
        p.rect(fx, fy + 2.5f, len, 1.0f,
               crop.watered ? Color(108, 82, 62) : kSoilLight);
    }
    for (int i = 0; i < 4; ++i) {
        const float cx = static_cast<float>(r.x + 16 + static_cast<int>((h >> (i * 4)) & 0x3Fu));
        const float cy = static_cast<float>(r.y + 18 + static_cast<int>((h >> (i * 3 + 1)) & 0x3Fu));
        p.ellipse(cx, cy, 3.5f, 1.8f, (i & 1) ? kSoilLight : kSoilDark, 10);
    }

    if (crop.watered) {
        p.rect(static_cast<float>(r.x + 18), static_cast<float>(r.y + 18), 10.0f, 2.0f,
               Color(110, 128, 116));
        p.rect(static_cast<float>(r.x + 62), static_cast<float>(r.y + 70), 12.0f, 2.0f,
               Color(110, 128, 116));
        p.rect(static_cast<float>(r.x + 34), static_cast<float>(r.y + 78), 8.0f, 2.0f,
               Color(110, 128, 116));
    }

    for (int i = 0; i < 12; ++i) {
        const int sx = r.x + 14 + static_cast<int>((h >> (i * 2)) & 0x3Fu) % 68;
        const int sy = r.y + 14 + static_cast<int>((h >> (i * 2 + 1)) & 0x3Fu) % 68;
        p.rect(static_cast<float>(sx), static_cast<float>(sy), 2.0f, 2.0f,
               (i & 1) ? kSoilLight : kSoilDark);
    }

    drawCrop(p, plotCenter(index), crop);
}

void drawFarm(Painter& p, const FarmWorld& world) {
    // 12 张土床直接铺在草地上，块间保留草缝；不再画整块木色土台。
    for (int i = 0; i < kPlotCols * kPlotRows; ++i) {
        drawPlot(p, i, world.crop(i));
    }

    // 草缝和四周的草屑/小花：让农田融进草地，而不是一个整齐的棋盘。
    for (int r = 0; r < kPlotRows; ++r) {
        for (int c = 0; c < 3; ++c) {
            const float x = static_cast<float>(kFarmX + c * kPlotPitch +
                                               kPlotCells * kCell + kCell / 2);
            const float y = static_cast<float>(kFarmY + r * kPlotPitch + kPlotCells * kCell / 2);
            const std::uint32_t h = hash2(c + 7, r + 13);
            drawGrassTuft(p, x + hashSigned(h, 0, 3.0f), y, 0.75f, kGrassDark);
            if ((h & 3u) == 0u) {
                drawFlower(p, x + hashSigned(h, 8, 5.0f), y - 3.0f, 0.65f, kWhite);
            }
        }
    }
    for (int r = 0; r < 2; ++r) {
        for (int c = 0; c < kPlotCols; ++c) {
            const float x = static_cast<float>(kFarmX + c * kPlotPitch + kPlotCells * kCell / 2);
            const float y = static_cast<float>(kFarmY + r * kPlotPitch + kPlotCells * kCell +
                                               kCell / 2);
            const std::uint32_t h = hash2(c + 31, r + 5);
            drawGrassTuft(p, x + hashSigned(h, 0, 4.0f), y, 0.8f, kGrassDark);
            if ((h & 1u) == 0u) {
                drawFlower(p, x + 6.0f, y + 4.0f, 0.6f, kPink);
            }
        }
    }
    for (int i = 0; i < 6; ++i) {
        const std::uint32_t h = hash2(i + 40, 17);
        const float x = static_cast<float>(kFarmX + 20 + i * 84);
        drawGrassTuft(p, x + hashSigned(h, 0, 5.0f), static_cast<float>(kFarmY - 6), 0.75f,
                      kGrassDark);
    }
}

void drawPath(Painter& p) {
    // 土路用一串重叠椭圆叠出来：边缘自然起伏，不会像一条木板。
    auto pathY = [](float x) { return 640.0f + 4.0f * std::sin(x * 0.021f); };
    for (float x = 118.0f; x <= 962.0f; x += 34.0f) {
        p.ellipse(x, pathY(x), 41.0f, 34.0f, kPathDark, 24);
    }
    for (float x = 126.0f; x <= 954.0f; x += 34.0f) {
        p.ellipse(x, pathY(x), 33.0f, 27.0f, kPath, 24);
    }

    // 零散的浅色土斑，不连成一条浅色中心线。
    for (int i = 0; i < 8; ++i) {
        const float x = 210.0f + static_cast<float>(i) * 92.0f;
        p.ellipse(x, 632.0f + static_cast<float>(i % 3) * 5.0f, 24.0f, 5.5f, kPathLight,
                  16);
    }
    p.ellipse(420.0f, 654.0f, 30.0f, 5.0f, kPathDark, 16);
    p.ellipse(700.0f, 650.0f, 36.0f, 4.5f, kPathDark, 16);

    // 断续车辙。
    p.line(200.0f, 626.0f, 330.0f, 625.0f, kPathDark, 1.5f);
    p.line(380.0f, 627.0f, 520.0f, 625.0f, kPathDark, 1.5f);
    p.line(570.0f, 626.0f, 700.0f, 624.0f, kPathDark, 1.5f);
    p.line(215.0f, 656.0f, 345.0f, 654.0f, kPathDark, 1.5f);
    p.line(410.0f, 657.0f, 560.0f, 654.0f, kPathDark, 1.5f);
    p.line(620.0f, 655.0f, 770.0f, 652.0f, kPathDark, 1.5f);

    // 草从两侧爬进来，打断土路边缘。
    for (int i = 0; i < 14; ++i) {
        const std::uint32_t h = hash2(i + 21, 91);
        const float x = 170.0f + static_cast<float>(i) * 58.0f + hashSigned(h, 0, 14.0f);
        const bool top = (i & 1) == 0;
        const float y = top ? 607.0f + static_cast<float>(h % 6)
                            : 671.0f - static_cast<float>(h % 6);
        p.ellipse(x, y, 8.0f + static_cast<float>(h % 5), 3.0f, kGrassDark, 12);
        drawGrassTuft(p, x, y, 0.7f, kGrassDeep);
    }

    for (int i = 0; i < 12; ++i) {
        const std::uint32_t h = hash2(i + 5, 71);
        const float x = 190.0f + static_cast<float>(i) * 66.0f + hashSigned(h, 0, 12.0f);
        const float y = 618.0f + static_cast<float>(h % 40);
        if (i & 1) {
            drawStone(p, x, y, 3.5f + static_cast<float>(h % 3), kPathDark);
        } else {
            p.ellipse(x, y, 2.5f, 1.8f, kPathDark, 10);
        }
    }
}

// ---------------- 池塘与栈道 ----------------

void drawReedCluster(Painter& p, float x, float y, float scale, int count) {
    for (int i = 0; i < count; ++i) {
        const float dx = (static_cast<float>(i) - (static_cast<float>(count) - 1.0f) * 0.5f) *
                         5.5f * scale;
        const float h = (24.0f + static_cast<float>(i % 3) * 7.0f) * scale;
        const float lean = (i % 2 == 0 ? 1.0f : -1.0f) * 3.0f * scale;
        p.line(x + dx, y, x + dx + lean, y - h, kLeafDark, 1.2f);
        if (i % 2 == 0) {
            p.ellipse(x + dx + lean, y - h - 3.0f * scale, 2.4f * scale, 4.8f * scale,
                      Color(124, 86, 48), 14);
        } else {
            p.ellipse(x + dx + lean, y - h, 1.8f * scale, 3.4f * scale, kLeaf, 12);
        }
    }
}

void drawRock(Painter& p, float x, float y, float rx, float ry, const Color& shadow) {
    p.ellipse(x + 2.0f, y + 2.0f, rx, ry * 0.8f, shadow, 18);
    p.ellipse(x, y, rx, ry, kStoneDark, 18);
    p.ellipse(x - 1.0f, y - 1.0f, rx * 0.86f, ry * 0.8f, kStone, 18);
    p.ellipse(x - rx * 0.3f, y - ry * 0.35f, rx * 0.42f, ry * 0.3f, kStoneLight, 14);
}

void drawLilyPad(Painter& p, float x, float y, float r, const Color& water, bool flower) {
    p.ellipseOutline(x, y, r, r * 0.7f, kLeaf, kLeafDark, 1.2f, 20);
    p.line(x, y, x + r * 0.95f, y - r * 0.2f, water, 3.0f);
    p.ellipse(x - r * 0.25f, y - r * 0.18f, r * 0.32f, r * 0.16f, kLeafHi, 12);
    if (flower) {
        p.ellipse(x + r * 0.3f, y - r * 0.35f, 2.6f, 1.8f, kPink, 10);
        p.ellipse(x + r * 0.3f - 2.0f, y - r * 0.35f - 1.6f, 2.0f, 1.6f, kPink, 10);
        p.ellipse(x + r * 0.3f + 2.0f, y - r * 0.35f - 1.6f, 2.0f, 1.6f, kPink, 10);
        p.ellipse(x + r * 0.3f, y - r * 0.35f, 1.2f, 1.2f, kFlowerYellow, 10);
    }
}

// 荷叶是浮在水面的实体：不能烘进静态背景，否则每帧的锦鲤和水纹会盖在它上面。
// 每帧画在鱼与水纹之后、栈道盖回之前，再由影子层统一压暗。
// 传入相机偏移：荷叶长在池塘的固定世界坐标上，要跟着镜头一起滚动，
// 否则会像贴在屏幕上的贴纸（相机一平移就露馅）。
void drawLilyPads(Painter& p, float camX, float camY) {
    const float cx = kPondCx - camX;
    const float cy = kPondCy - camY;
    drawLilyPad(p, cx - 86.0f, cy - 46.0f, 15.0f, kWaterMid, false);
    drawLilyPad(p, cx + 52.0f, cy - 92.0f, 13.0f, kWaterLight, false);
    drawLilyPad(p, cx + 112.0f, cy - 48.0f, 14.0f, kWaterLight, true);
    drawLilyPad(p, cx - 38.0f, cy + 118.0f, 12.0f, kWaterMid, false);
    drawLilyPad(p, cx + 82.0f, cy + 126.0f, 14.0f, kWaterMid, false);
    drawLilyPad(p, cx + 136.0f, cy + 62.0f, 11.0f, kWaterMid, false);
}

// 闭合 B 样条在任意参数 u 处的点：直接调 Cox-de Boor 基函数求和。
// sampleBSpline 只能均匀采样一串点，锦鲤要按相位取任意 u，所以在这里用基函数。
// 关键是用**均匀节点**：控制点周期延拓后，u=0 与 u=1 求值落在同一点
// （(P0+4P1+P2)/6），且切线连续。此前用 clamped 节点，曲线在 u=1 固定到
// 最后一个控制点上——而末尾重复的是第 3 个控制点，于是每圈都会跳回起点。
FPoint evalClosedBSplineAt(const std::vector<FPoint>& cps, int degree, float u) {
    const std::vector<float> knots =
        curves::uniformKnots(static_cast<int>(cps.size()), degree);
    u = std::clamp(u, 0.0f, 1.0f);
    if (u >= 1.0f) {
        u = 1.0f - 1e-6f; // 均匀节点在右端点两段同时命中，取左极限逼近闭合点
    }
    FPoint sum{0.0f, 0.0f};
    for (std::size_t i = 0; i < cps.size(); ++i) {
        const float b = curves::bsplineBasis(static_cast<int>(i), degree, u, knots);
        sum.x += cps[i].x * b;
        sum.y += cps[i].y * b;
    }
    return sum;
}

// 三条锦鲤：每条一个环形控制多边形（8 点绕池塘一圈），末尾再追前 3 个点，
// 把控制多边形周期延拓；配均匀节点得到首尾相接、切线连续的闭合三次 B 样条。
// 位置取当前相位的 u，朝向取 u±ε 两点的切线；鱼身/尾鳍在局部坐标里定义后
// 按切线角旋转。
void drawKoi(Painter& p, const FarmWorld& world, float camX, float camY) {
    struct KoiSpec {
        float rx;    // 控制多边形的半宽（相对池塘半径）
        float ry;
        float phase; // 起始相位
        float speed; // 每秒走过的圈数
        float size;  // 鱼体缩放
        Color body;
        Color fin;
    };
    const KoiSpec kois[] = {
        {0.62f, 0.58f, 0.00f, 0.030f, 1.00f, Color(236, 132, 62), Color(255, 196, 132)},
        {0.44f, 0.40f, 0.37f, 0.043f, 0.82f, Color(246, 198, 118), Color(255, 230, 180)},
        {0.70f, 0.66f, 0.71f, 0.024f, 0.74f, Color(198, 92, 58), Color(238, 158, 108)},
    };

    for (std::size_t k = 0; k < 3; ++k) {
        const KoiSpec& spec = kois[k];
        // 8 个控制点绕一圈，半径带一点确定性抖动，循环才不显得像标准椭圆。
        std::vector<FPoint> ctrl;
        ctrl.reserve(11);
        for (int i = 0; i < 8; ++i) {
            const float a = 2.0f * kPi * static_cast<float>(i) / 8.0f;
            const float wob = 1.0f + 0.16f * hashSigned(hash2(i * 7 + 3, static_cast<int>(k) * 13 + 5),
                                                       12, 1.0f);
            ctrl.push_back({kPondCx + std::cos(a) * kPondRx * spec.rx * wob,
                            kPondCy + std::sin(a) * kPondRy * spec.ry * wob});
        }
        for (int i = 0; i < 3; ++i) { // 周期延拓：把前 3 点追到末尾
            ctrl.push_back(ctrl[static_cast<std::size_t>(i)]);
        }

        const float u = std::fmod(world.animTime() * spec.speed + spec.phase, 1.0f);
        const FPoint head = evalClosedBSplineAt(ctrl, 3, u);
        const FPoint ahead = evalClosedBSplineAt(ctrl, 3, std::fmod(u + 0.004f, 1.0f));
        const float dx = ahead.x - head.x;
        const float dy = ahead.y - head.y;
        if (std::fabs(dx) + std::fabs(dy) < 1e-4f) {
            continue;
        }
        const float ang = std::atan2(dy, dx);
        const float c = std::cos(ang);
        const float s = std::sin(ang);
        const float x = head.x - camX;
        const float y = head.y - camY;
        const float sc = spec.size;

        // 局部坐标 (前, 右) → 世界：旋转后平移。
        auto P = [&](float fwd, float side) {
            return FPoint{x + fwd * c - side * s, y + fwd * s + side * c};
        };
        auto blob = [&](float cx_, float cy_, float rx_, float ry_, const Color& col,
                        std::uint8_t alpha, int seg) {
            std::vector<FPoint> pts;
            pts.reserve(static_cast<std::size_t>(seg));
            for (int i = 0; i < seg; ++i) {
                const float a = 2.0f * kPi * static_cast<float>(i) / static_cast<float>(seg);
                const FPoint q = P(cx_ + std::cos(a) * rx_, cy_ + std::sin(a) * ry_);
                pts.push_back(q);
            }
            p.polygon(pts, col, col, 0.0f, alpha);
        };

        // 尾鳍会随相位摆，钓鱼的鱼线摆动同一套正弦。
        const float wag = std::sin(world.animTime() * 5.4f + spec.phase * 9.0f) * 3.6f * sc;
        const std::vector<FPoint> tail = {P(-10.0f * sc, 0.0f),
                                          P(-21.0f * sc, -7.5f * sc + wag),
                                          P(-19.0f * sc, 0.0f),
                                          P(-21.0f * sc, 7.5f * sc + wag)};
        p.polygon(tail, spec.fin, spec.fin, 0.0f, 205);

        // 身体：深浅两层叠出水下的体积，背鳍一条。
        blob(0.0f, 0.0f, 11.5f * sc, 6.2f * sc, spec.body, 214, 22);
        blob(1.6f * sc, 1.4f * sc, 8.0f * sc, 3.6f * sc, spec.fin, 210, 18);
        blob(-0.6f, -2.2f * sc, 7.0f * sc, 2.2f * sc, spec.body, 180, 16);
        // 眼睛
        blob(7.6f * sc, -1.8f * sc, 1.15f * sc, 1.15f * sc, Color(48, 32, 26), 235, 10);
    }
}

// 栈道尽头的小灯：暖色一点，给池塘一个视觉落点。它是水面上的立体摆件，
// 遮罩烘焙也单独调它，影子不会压在灯罩上。
void drawDockLamp(Painter& p) {
    p.line(static_cast<float>(kDockLampX), static_cast<float>(kDockY + kDockH - 46),
           static_cast<float>(kDockLampX), static_cast<float>(kDockY - 10), kWoodDark,
           4.0f);
    p.ellipse(static_cast<float>(kDockLampX), static_cast<float>(kDockLampY), 7.5f, 8.5f,
              Color(58, 42, 28), 18);
    p.ellipse(static_cast<float>(kDockLampX), static_cast<float>(kDockLampY), 5.3f, 6.0f,
              Color(250, 204, 90), 18);
    p.ellipse(static_cast<float>(kDockLampX) - 1.5f, static_cast<float>(kDockLampY) - 2.5f,
              1.8f, 2.2f, kCream, 10);
}

void drawDock(Painter& p) {
    p.rect(static_cast<float>(kDockX + 6), static_cast<float>(kDockY + 7),
           static_cast<float>(kDockW), static_cast<float>(kDockH), Color(30, 78, 96));

    p.rect(static_cast<float>(kDockX), static_cast<float>(kDockY),
           static_cast<float>(kDockW), static_cast<float>(kDockH), kWoodDark);
    const float plankW = (static_cast<float>(kDockW) - 8.0f) / 11.0f;
    for (int i = 0; i < 11; ++i) {
        const float px = static_cast<float>(kDockX) + 4.0f + static_cast<float>(i) * plankW;
        p.rect(px, static_cast<float>(kDockY) + 3.0f, plankW - 2.0f,
               static_cast<float>(kDockH) - 6.0f, (i & 1) ? kWood : kWoodLight);
    }
    p.rect(static_cast<float>(kDockX), static_cast<float>(kDockY),
           static_cast<float>(kDockW), 3.0f, kWoodDark);
    p.rect(static_cast<float>(kDockX), static_cast<float>(kDockY + kDockH - 3),
           static_cast<float>(kDockW), 3.0f, kWoodDark);

    const int postX[4] = {kDockX + 5, kDockX + kDockW - 13, kDockX + 5,
                          kDockX + kDockW - 13};
    const int postY[4] = {kDockY + 5, kDockY + 5, kDockY + kDockH - 13,
                          kDockY + kDockH - 13};
    for (int i = 0; i < 4; ++i) {
        p.rect(static_cast<float>(postX[i] - 1), static_cast<float>(postY[i] - 1), 11.0f,
               11.0f, kOutline);
        p.rect(static_cast<float>(postX[i]), static_cast<float>(postY[i]), 9.0f, 9.0f,
               kWoodLight);
        p.ellipse(static_cast<float>(postX[i] + 4), static_cast<float>(postY[i] + 4), 4.5f,
                  4.5f, kWood, 14);
    }

    drawDockLamp(p);
}

void drawPond(Painter& p) {
    const float cx = kPondCx;
    const float cy = kPondCy;
    const float rx = kPondRx;
    const float ry = kPondRy;

    p.ellipse(cx, cy, rx + 24.0f, ry + 22.0f, kSandDark, 72);
    p.ellipse(cx - 2.0f, cy - 3.0f, rx + 18.0f, ry + 16.0f, kSand, 72);
    p.ellipse(cx, cy, rx + 7.0f, ry + 6.0f, kSandDark, 72);
    p.ellipse(cx, cy, rx, ry, kWaterDeep, 72);
    p.ellipseGradient(cx, cy, rx, ry, Color(100, 192, 216), Color(38, 100, 140));
    p.lineLoopEllipse(cx, cy, rx, ry, kWaterRim, 2.0f, 72);

    // 静态水纹：正式版会在这里叠加动画波纹。
    p.arc(cx - 62.0f, cy - 104.0f, 58.0f, 17.0f, 3.45f, 5.75f, kWaterShimmer, 1.4f, 14);
    p.arc(cx - 48.0f, cy - 92.0f, 66.0f, 20.0f, 3.55f, 5.65f, kWaterShimmer, 1.2f, 14);
    p.arc(cx + 38.0f, cy + 44.0f, 44.0f, 12.0f, 3.6f, 5.7f, Color(104, 190, 208), 1.2f,
          12);
    p.arc(cx + 58.0f, cy + 78.0f, 58.0f, 15.0f, 3.55f, 5.7f, kWaterShimmer, 1.4f, 12);

    // 鱼不在静态层里画了：锦鲤沿 B 样条每帧游动，见 drawKoi。
    // 荷叶同样不烘进背景：它浮在水面，每帧画在鱼与水纹之上，见 drawLilyPads。

    drawReedCluster(p, cx - 164.0f, cy - 186.0f, 1.0f, 6);
    drawReedCluster(p, cx - 108.0f, cy - 216.0f, 0.85f, 5);
    drawReedCluster(p, cx + 180.0f, cy + 204.0f, 1.0f, 6);
    drawReedCluster(p, cx + 132.0f, cy + 236.0f, 0.85f, 5);
    drawReedCluster(p, cx - 180.0f, cy + 148.0f, 0.8f, 4);

    drawRock(p, cx + 218.0f, cy - 18.0f, 11.0f, 7.0f, kSandDark);
    drawRock(p, cx + 228.0f, cy + 42.0f, 7.0f, 5.0f, kSandDark);
    drawRock(p, cx - 190.0f, cy - 104.0f, 8.0f, 5.0f, kSandDark);
    drawRock(p, cx - 142.0f, cy + 206.0f, 10.0f, 6.0f, kSandDark);

    drawDock(p);
}

// ---------------- 散落装饰与树林 ----------------

// 树与灌木的布局表：绘制和影子投射共用（影子要按太阳方位逐帧重算）。
struct TreeSpot {
    float x;
    float y;
    float scale;
    bool variant;
};
constexpr TreeSpot kTrees[] = {
    {90.0f, 100.0f, 1.05f, false},  {250.0f, 84.0f, 0.9f, true},
    {410.0f, 112.0f, 1.1f, false},  {570.0f, 92.0f, 0.95f, true},
    {730.0f, 108.0f, 1.0f, false},  {890.0f, 86.0f, 1.15f, true},
    {1050.0f, 110.0f, 0.9f, false}, {1210.0f, 92.0f, 1.05f, true},
    {1370.0f, 108.0f, 0.95f, false},{1490.0f, 88.0f, 1.0f, true},
    {110.0f, 1110.0f, 1.0f, false}, {280.0f, 1128.0f, 0.9f, true},
    {450.0f, 1098.0f, 1.1f, false}, {620.0f, 1120.0f, 0.95f, true},
    {790.0f, 1094.0f, 1.05f, false},{960.0f, 1124.0f, 0.9f, true},
    {1130.0f, 1100.0f, 1.0f, false},{1300.0f, 1120.0f, 1.05f, true},
    {1460.0f, 1096.0f, 0.9f, false},{70.0f, 300.0f, 1.0f, true},
    {60.0f, 490.0f, 0.9f, false},   {80.0f, 670.0f, 1.05f, true},
    {64.0f, 850.0f, 0.95f, false},  {78.0f, 1020.0f, 1.0f, true},
    {1470.0f, 290.0f, 0.95f, false},{1458.0f, 470.0f, 1.05f, true},
    {1474.0f, 650.0f, 0.9f, false}, {1462.0f, 830.0f, 1.0f, true},
    {1472.0f, 1010.0f, 1.05f, false}};
constexpr float kBushSpots[][3] = {
    {150.0f, 760.0f, 1.0f},  {150.0f, 930.0f, 0.85f},
    {1390.0f, 770.0f, 1.0f}, {1390.0f, 940.0f, 0.9f},
    {330.0f, 1000.0f, 1.1f}, {620.0f, 1015.0f, 0.9f},
    {900.0f, 995.0f, 1.0f},  {1180.0f, 1010.0f, 0.95f},
    {760.0f, 120.0f, 1.0f},  {1090.0f, 140.0f, 0.85f},
    {1260.0f, 980.0f, 1.0f}, {480.0f, 150.0f, 0.8f}};

bool meadowBlocked(float x, float y) {
    // 农田动态层重烘范围（含外圈缓冲）内不放静态装饰：这块区域会被局部
    // 重烘整片覆盖，跨界的装饰会在重烘后被凭空截断。
    return nearFarmBakeRect(x, y) || inFarmApron(x, y) || inPathArea(x, y) ||
           inPond(x, y, 28.0f) || inDock(x, y);
}

void drawLampPost(Painter& p);
void drawCabinExterior(Painter& p);

void drawMeadow(Painter& p) {
    for (int row = kWalkMinRow; row <= kWalkMaxRow; ++row) {
        for (int col = kWalkMinCol; col <= kWalkMaxCol; ++col) {
            const IPoint c = cellCenter(col, row);
            const float x = static_cast<float>(c.x);
            const float y = static_cast<float>(c.y);
            if (meadowBlocked(x, y)) {
                continue;
            }
            const std::uint32_t h = hash2(col, row);
            const float jx = x + hashSigned(h, 0, 10.0f);
            const float jy = y + hashSigned(h, 8, 8.0f);

            if (h % 5u == 0u) {
                drawGrassTuft(p, jx, jy, 0.8f + static_cast<float>(h % 5) * 0.06f,
                              kGrassDark);
            }
            if (h % 23u == 0u) {
                const Color petals[3] = {kWhite, kPink, kFlowerYellow};
                for (int i = 0; i < 3; ++i) {
                    drawFlower(p, jx + static_cast<float>(i - 1) * 8.0f,
                               jy - 4.0f + static_cast<float>(i % 2) * 4.0f, 0.8f,
                               petals[static_cast<std::size_t>((h + static_cast<unsigned>(i)) %
                                                               3u)]);
                }
            }
            if (h % 89u == 0u) {
                drawStone(p, jx, jy, 5.0f + static_cast<float>(h % 4) * 1.5f,
                          kGrassDeep);
            }
            if (h % 157u == 0u) {
                drawMushroom(p, jx, jy, 0.9f);
            }
        }
    }

    // 池塘外圈的芦苇/高草，让水岸不是一条光滑的线。
    for (int i = 0; i < 16; ++i) {
        const float a = 2.0f * kPi * static_cast<float>(i) / 16.0f;
        const float x = kPondCx + (kPondRx + 38.0f) * std::cos(a);
        const float y = kPondCy + (kPondRy + 34.0f) * std::sin(a);
        if (meadowBlocked(x, y) || inPathArea(x, y)) {
            continue;
        }
        drawGrassTuft(p, x, y, 1.15f, kGrassDark);
        if (i % 3 == 0) {
            drawGrassTuft(p, x + 7.0f, y + 3.0f, 0.9f, kGrassDeep);
        }
    }
}

// 路灯：灯杆+灯头+暖色灯泡。夜里 bulb 的亮度靠 drawLampGlow 每帧叠光。
void drawLampPost(Painter& p) {
    p.ellipse(kLampX + 3.0f, kLampY + 3.0f, 9.0f, 3.5f, kGrassDeep, 14);
    p.line(kLampX, kLampY, kLampX, kLampBulbY + 8.0f, kStoneDark, 5.0f);
    p.line(kLampX - 1.0f, kLampY, kLampX - 1.0f, kLampBulbY + 10.0f, kStoneLight, 1.5f);
    p.ellipse(kLampX, kLampBulbY + 8.0f, 4.5f, 3.0f, kStoneDark, 12);
    // 灯头：六边形灯罩里一颗暖黄灯泡。
    const std::vector<FPoint> cap = {{kLampX - 9.0f, kLampBulbY + 8.0f},
                                     {kLampX - 6.0f, kLampBulbY - 2.0f},
                                     {kLampX + 6.0f, kLampBulbY - 2.0f},
                                     {kLampX + 9.0f, kLampBulbY + 8.0f}};
    p.polygon(cap, kStoneDark, kOutline, 1.2f);
    p.line(kLampX - 9.0f, kLampBulbY - 2.0f, kLampX + 9.0f, kLampBulbY - 2.0f, kOutline,
           1.5f);
    p.ellipse(kLampX, kLampBulbY + 3.0f, 6.0f, 6.5f, Color(250, 204, 90), 16);
    p.ellipse(kLampX - 1.5f, kLampBulbY + 1.0f, 1.8f, 2.2f, kCream, 10);
}

// 小屋外观（静态）：石砌地基、奶油抹泥墙、深红坡屋顶、烟囱和暖色门窗。
void drawCabinExterior(Painter& p) {
    const float x = kCabinX;
    const float y = kCabinY;
    const float w = kCabinW;
    const float h = kCabinH;

    // 地基与草影
    p.ellipse(x + w * 0.5f, y + h + 6.0f, w * 0.55f, 20.0f, kGrassDeep, 32);
    p.rect(x - 10.0f, y + h - 26.0f, w + 20.0f, 30.0f, kStoneDark);
    p.rect(x - 8.0f, y + h - 24.0f, w + 16.0f, 24.0f, kStone);

    // 墙体：奶油抹泥 + 深木梁
    const std::vector<FPoint> wall = {{x, y + 52.0f}, {x + w, y + 52.0f},
                                      {x + w, y + h}, {x, y + h}};
    p.polygon(wall, Color(226, 208, 168), kOutline, 2.0f);
    for (int i = 0; i <= 4; ++i) {
        const float vx = x + 10.0f + static_cast<float>(i) * (w - 20.0f) / 4.0f;
        p.rect(vx - 4.0f, y + 54.0f, 8.0f, h - 78.0f, kWoodDark);
    }
    p.rect(x, y + 54.0f, w, 7.0f, kWoodDark);

    // 屋顶：双层深红坡顶，带出檐。
    const std::vector<FPoint> roof = {{x - 26.0f, y + 62.0f},
                                      {x + w * 0.5f, y - 34.0f},
                                      {x + w + 26.0f, y + 62.0f},
                                      {x + w + 12.0f, y + 68.0f},
                                      {x + w * 0.5f, y - 20.0f},
                                      {x - 12.0f, y + 68.0f}};
    p.polygon(roof, Color(156, 74, 52), kOutline, 2.0f);
    const std::vector<FPoint> roofFront = {{x - 12.0f, y + 68.0f},
                                           {x + w * 0.5f, y - 20.0f},
                                           {x + w + 12.0f, y + 68.0f},
                                           {x + w * 0.5f, y - 6.0f}};
    p.polygon(roofFront, Color(184, 92, 62), kOutline, 2.0f);

    // 烟囱：只画管体与顶盖；炊烟由 drawChimneySmoke 每帧实时绘制，不烘进背景。
    p.rect(x + w - 74.0f, y - 18.0f, 26.0f, 58.0f, kStoneDark);
    p.rect(x + w - 77.0f, y - 22.0f, 32.0f, 8.0f, kStone);

    // 门：圆头木门 + 把手，门前一块石板。
    const float dx = kCabinDoorX;
    p.rect(dx - 26.0f, kCabinDoorY - 12.0f, 52.0f, 12.0f, kSandDark);
    const std::vector<FPoint> door = {{dx - 22.0f, kCabinDoorY - 10.0f},
                                      {dx + 22.0f, kCabinDoorY - 10.0f},
                                      {dx + 22.0f, kCabinDoorY - 58.0f},
                                      {dx, kCabinDoorY - 74.0f},
                                      {dx - 22.0f, kCabinDoorY - 58.0f}};
    p.polygon(door, kWoodDark, kOutline, 2.0f);
    p.line(dx - 12.0f, kCabinDoorY - 16.0f, dx - 12.0f, kCabinDoorY - 60.0f,
           Color(78, 50, 30), 2.0f);
    p.line(dx + 12.0f, kCabinDoorY - 16.0f, dx + 12.0f, kCabinDoorY - 60.0f,
           Color(78, 50, 30), 2.0f);
    p.ellipse(dx + 14.0f, kCabinDoorY - 40.0f, 3.0f, 3.0f, kFlowerYellow, 10);

    // 两扇暖光窗
    for (float wx : {x + 44.0f, x + w - 92.0f}) {
        p.rect(wx - 4.0f, y + 88.0f, 52.0f, 46.0f, kWoodDark);
        p.rect(wx, y + 92.0f, 44.0f, 38.0f, Color(250, 216, 130));
        p.line(wx + 22.0f, y + 92.0f, wx + 22.0f, y + 130.0f, kWoodDark, 2.0f);
        p.line(wx, y + 111.0f, wx + 44.0f, y + 111.0f, kWoodDark, 2.0f);
    }
}

// 立体摆件：灌木、木桶、路灯、小屋。它们会挡住影子，单独成一组便于生成
// “影子不落在物件身上”的遮罩；绘制顺序与原 drawMeadow 末尾完全一致。
void drawProps(Painter& p) {
    const float bushes[][3] = {{150.0f, 760.0f, 1.0f},  {150.0f, 930.0f, 0.85f},
                               {1390.0f, 770.0f, 1.0f}, {1390.0f, 940.0f, 0.9f},
                               {330.0f, 1000.0f, 1.1f}, {620.0f, 1015.0f, 0.9f},
                               {900.0f, 995.0f, 1.0f},  {1180.0f, 1010.0f, 0.95f},
                               {760.0f, 120.0f, 1.0f},  {1090.0f, 140.0f, 0.85f},
                               {1260.0f, 980.0f, 1.0f}, {480.0f, 150.0f, 0.8f}};
    for (const auto& b : bushes) {
        drawBush(p, b[0], b[1], b[2]);
    }

    // 农田右下侧一只木桶（纯装饰，不挡路）。
    const float bx = 682.0f;
    const float by = 598.0f;
    p.ellipse(bx + 3.0f, by + 3.0f, 14.0f, 5.0f, kGrassDeep, 16);
    const std::vector<FPoint> bucket = {{bx - 11.0f, by - 20.0f},
                                        {bx + 11.0f, by - 20.0f},
                                        {bx + 8.0f, by},
                                        {bx - 8.0f, by}};
    p.polygon(bucket, kWoodDark, kOutline, 1.5f);
    p.ellipse(bx, by - 20.0f, 11.0f, 4.0f, kWood, 18);
    p.arc(bx, by - 21.0f, 12.0f, 8.0f, 3.4f, 6.0f, kWoodLight, 1.5f, 12);

    drawLampPost(p);
    drawCabinExterior(p);
}

void drawTrees(Painter& p) {
    std::vector<TreeSpot> sorted(std::begin(kTrees), std::end(kTrees));
    std::sort(sorted.begin(), sorted.end(),
              [](const TreeSpot& a, const TreeSpot& b) { return a.y < b.y; });
    for (const TreeSpot& t : sorted) {
        drawTree(p, t.x, t.y, t.scale, t.variant);
    }
}

void drawGround(Painter& p) {
    p.rect(-8.0f, -8.0f, static_cast<float>(kWorldW) + 16.0f,
           static_cast<float>(kWorldH) + 16.0f, kGrass);

    p.ellipse(250.0f, 170.0f, 160.0f, 80.0f, kGrassLight, 48);
    p.ellipse(760.0f, 180.0f, 120.0f, 55.0f, kGrassDark, 40);
    p.ellipse(1180.0f, 220.0f, 190.0f, 90.0f, kGrassLight, 48);
    p.ellipse(520.0f, 900.0f, 180.0f, 90.0f, kGrassDark, 40);
    p.ellipse(980.0f, 940.0f, 220.0f, 100.0f, kGrassLight, 48);
    p.ellipse(1380.0f, 900.0f, 120.0f, 60.0f, kGrassDark, 40);
    p.ellipse(700.0f, 780.0f, 90.0f, 40.0f, kGrassLight, 32);
    p.ellipse(300.0f, 1010.0f, 140.0f, 55.0f, kGrassDark, 32);
    p.ellipse(1260.0f, 1020.0f, 150.0f, 60.0f, kGrassLight, 32);

    p.rect(0.0f, 0.0f, static_cast<float>(kWorldW), 10.0f, kBorderGrass);
    p.rect(0.0f, static_cast<float>(kWorldH) - 10.0f, static_cast<float>(kWorldW), 10.0f,
           kBorderGrass);
    p.rect(0.0f, 0.0f, 10.0f, static_cast<float>(kWorldH), kBorderGrass);
    p.rect(static_cast<float>(kWorldW) - 10.0f, 0.0f, 10.0f,
           static_cast<float>(kWorldH), kBorderGrass);
}

void drawScene(Painter& p, const FarmWorld& world) {
    drawGround(p);
    drawPath(p);
    drawFarm(p, world);
    drawPond(p);
    drawMeadow(p);
    drawProps(p);
    drawTrees(p);
}

// ---------------- 胖龙 ----------------

// ---------------- 胖龙：六个候选造型 ----------------
// 六个造型共用同一套接口：脚底锚点、朝向翻转、走路起伏、双脚步态。
// 切换造型只改 kDragonVariant 一个常量，动画、影子、交互都不用动。
//
// 造型公式（六个都按这一套来，放在一起才像同一个世界里的生物）：
//   头宽 > 头高；眼睛压在头部中线偏上、间距大、瞳孔大；嘴小而宽；
//   腮红靠外；身体宽扁无脖子、两侧露出来；手脚短粗；
//   尾巴从身体内部起笔、由粗到细（二次贝塞尔扫出的带状多边形），不悬浮。

constexpr int kDragonVariant = 2; // 1 奶蛋 2 奶龙 3 奶鼠 4 奶娃 5 奶兔 6 奶猫（项目名胖龙农场，选定 2）

// 一双大眼睛：眼白 + 大虹膜 + 左上高光 + 右下反光。
// rim 用头部色的深色版而不是纯黑，边缘才不会像贴了两枚纽扣。
void dEyes(Painter& p, float x, float y, float f, float sep, float rx, float ry,
           const Color& rim) {
    for (int s = -1; s <= 1; s += 2) {
        const float ex = x + static_cast<float>(s) * sep * f;
        p.ellipseOutline(ex, y, rx, ry, kWhite, rim, 1.0f, 22);
        p.ellipse(ex + 0.9f * f, y - ry * 0.02f, rx * 0.72f, ry * 0.72f, Color(48, 34, 28),
                  18);
        p.ellipse(ex + 0.9f * f - rx * 0.24f, y - ry * 0.30f, rx * 0.24f, rx * 0.24f, kWhite,
                  8);
        p.ellipse(ex + 0.9f * f + rx * 0.20f, y + ry * 0.38f, rx * 0.13f, rx * 0.13f,
                  Color(200, 188, 178), 6);
    }
}

void dBlush(Painter& p, float x, float y, float f, float sep) {
    p.ellipse(x - sep * f, y, 5.0f, 3.2f, kCheek, 14, 140);
    p.ellipse(x + sep * f, y, 5.0f, 3.2f, kCheek, 14, 140);
}

// 微笑：控制点比两端更低（屏幕 y 向下），画出来就是嘴角上翘。
// 线宽用 1px：这个尺寸下再粗就糊成一团黑块了。
void dSmile(Painter& p, float x, float y, float f, float w, float depth) {
    p.bezier({x - w * f, y}, {x, y + depth}, {x + w * f, y}, kOutline, 1.0f, 10);
}

// 两只脚：走路时左右交替抬起，静止时贴地。
void dFeet(Painter& p, float x, float y, float f, bool moving, float phase,
           const Color& c, float sep, float rx, float ry) {
    for (int s = -1; s <= 1; s += 2) {
        const float lift =
            moving ? std::max(0.0f, std::sin(phase * 2.0f * kPi +
                                             (s > 0 ? 0.0f : kPi))) * 3.2f
                   : 0.0f;
        p.ellipseOutline(x + static_cast<float>(s) * sep * f, y - 2.0f - lift, rx, ry, c,
                         kOutline, 1.2f, 16);
    }
}

// 沿二次贝塞尔扫出一条由粗到细的带状多边形（尾巴用）。
// 每个采样点取切线法向、按插值宽度向两侧偏移，再把两条边收成一个闭合多边形。
void dTaper(Painter& p, FPoint a, FPoint c, FPoint b, float w0, float w1,
            const Color& fill, int seg = 12) {
    const std::vector<FPoint> pts = curves::sampleBezier({a, c, b}, seg);
    if (pts.size() < 2) {
        return;
    }
    std::vector<FPoint> loop;
    loop.reserve(pts.size() * 2);
    std::vector<FPoint> rightEdge;
    for (std::size_t i = 0; i < pts.size(); ++i) {
        const std::size_t i0 = (i == 0) ? 0 : i - 1;
        const std::size_t i1 = (i + 1 < pts.size()) ? i + 1 : i;
        float dx = pts[i1].x - pts[i0].x;
        float dy = pts[i1].y - pts[i0].y;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len > 1e-5f) {
            dx /= len;
            dy /= len;
        } else {
            dx = 1.0f;
            dy = 0.0f;
        }
        const float t = static_cast<float>(i) / static_cast<float>(pts.size() - 1);
        const float w = 0.5f * (w0 + (w1 - w0) * t);
        loop.push_back({pts[i].x - dy * w, pts[i].y + dx * w});
        rightEdge.push_back({pts[i].x + dy * w, pts[i].y - dx * w});
    }
    for (auto it = rightEdge.rbegin(); it != rightEdge.rend(); ++it) {
        loop.push_back(*it);
    }
    p.polygon(loop, fill, kOutline, 1.0f);
}

// 1 奶蛋：整只奶白蛋，脸长在蛋壳上，头顶一根嫩芽。
void drawDragonEgg(Painter& p, float x, float y, float f, bool moving, float phase,
                   float time) {
    const Color shell(255, 246, 220);
    const Color shellShade(238, 214, 170);
    const Color rim(176, 150, 110);
    dFeet(p, x, y, f, moving, phase, shellShade, 8.5f, 7.2f, 4.4f);

    // 蛋壳：参数方程点一串多边形，上窄下宽，一次闭合描边，没有接缝。
    std::vector<FPoint> egg;
    egg.reserve(44);
    for (int i = 0; i < 44; ++i) {
        const float t = 2.0f * kPi * static_cast<float>(i) / 44.0f;
        const float ex = 23.0f * std::sin(t) * (1.0f + 0.08f * std::cos(t));
        const float ey = 28.0f * std::cos(t);
        egg.push_back({x + ex, y - 30.0f - ey});
    }
    p.polygon(egg, shell, kOutline, 1.8f);
    p.ellipse(x - 9.0f * f, y - 24.0f, 6.0f, 9.5f, shellShade, 16, 90);

    // 小手：贴着蛋壳两侧
    p.ellipseOutline(x - 21.0f * f, y - 25.0f, 4.6f, 6.6f, shell, kOutline, 1.2f, 14);
    p.ellipseOutline(x + 21.0f * f, y - 25.0f, 4.6f, 6.6f, shell, kOutline, 1.2f, 14);

    dEyes(p, x, y - 39.0f, f, 9.6f, 5.6f, 6.6f, rim);
    dSmile(p, x, y - 28.5f, f, 5.0f, 3.2f);
    dBlush(p, x, y - 31.0f, f, 16.0f);

    // 头顶嫩芽
    const float sway = std::sin(time * 1.8f) * 2.6f;
    p.bezier({x, y - 57.0f}, {x + sway * 0.4f, y - 63.0f}, {x + sway, y - 67.0f},
             Color(78, 132, 58), 1.8f, 8);
    p.ellipseOutline(x + sway - 5.0f, y - 68.0f, 5.2f, 3.0f, kLeafLight, kOutline, 1.0f, 12);
    p.ellipseOutline(x + sway + 5.0f, y - 70.0f, 5.2f, 3.0f, kLeaf, kOutline, 1.0f, 12);
}

// 2 奶龙：宽圆大头 + 扁胖身体，一对短圆角、圆吻、粗尾带奶油尾尖。
void drawDragonBaby(Painter& p, float x, float y, float f, bool moving, float phase,
                    float time) {
    (void)time;
    const float b = -f; // 背后方向
    const Color rim(158, 104, 40);

    // 尾巴：粗带子，起点藏在身体里，末端一颗奶油色圆头
    dTaper(p, {x + 13.0f * b, y - 20.0f}, {x + 30.0f * b, y - 9.0f},
           {x + 33.0f * b, y - 27.0f}, 10.0f, 4.2f, kDragon);
    p.ellipseOutline(x + 33.0f * b, y - 27.0f, 3.6f, 3.6f, kDragonShade, kOutline, 1.2f,
                     12);

    dFeet(p, x, y, f, moving, phase, kDragon, 10.5f, 8.6f, 5.2f);

    // 身体：宽扁、无脖子；肚皮小一号，让两侧的黄露出来
    p.ellipseOutline(x, y - 22.0f, 20.0f, 16.0f, kDragon, kOutline, 1.8f, 30);
    p.ellipseOutline(x, y - 16.5f, 10.0f, 7.5f, kCream, kCreamShade, 1.0f, 22);

    // 手：贴在体侧偏下，和头拉开距离
    p.ellipseOutline(x - 20.0f * f, y - 23.0f, 5.0f, 6.6f, kDragon, kOutline, 1.2f, 14);
    p.ellipseOutline(x + 20.0f * f, y - 23.0f, 5.0f, 6.6f, kDragon, kOutline, 1.2f, 14);

    // 角：矮胖的圆锥（用尾巴同一套带状扫掠），颜色比头部深一档
    for (float s : {-1.0f, 1.0f}) {
        dTaper(p, {x + s * 9.0f * f, y - 70.0f}, {x + s * 13.0f * f, y - 76.0f},
               {x + s * 11.0f * f, y - 80.0f}, 9.0f, 3.4f, kDragonShade, 6);
    }

    // 头：宽大于高
    p.ellipseOutline(x, y - 54.0f, 24.0f, 19.0f, kDragon, kOutline, 1.8f, 36);

    // 口鼻：压在眼睛下方；这个尺寸下鼻孔会看成第二对眼睛，省略
    p.ellipseOutline(x, y - 39.0f, 8.5f, 5.0f, kCream, kCreamShade, 1.0f, 18);
    dSmile(p, x, y - 38.2f, f, 4.4f, 2.0f);

    dEyes(p, x, y - 55.0f, f, 10.5f, 5.4f, 6.2f, rim);
    dBlush(p, x, y - 44.0f, f, 16.0f);
}

// 3 奶鼠：大圆耳 + 圆身 + 细锥尾。
void drawDragonMouse(Painter& p, float x, float y, float f, bool moving, float phase,
                     float time) {
    (void)time;
    const Color fur(240, 231, 219);
    const Color furShade(214, 200, 182);
    const Color rim(150, 132, 112);
    const float b = -f;
    dTaper(p, {x + 12.0f * b, y - 13.0f}, {x + 30.0f * b, y - 22.0f},
           {x + 32.0f * b, y - 3.0f}, 3.6f, 1.4f, Color(232, 166, 166));

    dFeet(p, x, y, f, moving, phase, furShade, 9.5f, 7.6f, 4.6f);

    // 耳朵（先画，压在头后面）
    for (float s : {-1.0f, 1.0f}) {
        p.ellipseOutline(x + s * 17.0f * f, y - 67.0f, 10.5f, 10.5f, fur, kOutline,
                         1.4f, 20);
        p.ellipse(x + s * 17.0f * f, y - 67.0f, 6.2f, 6.2f, Color(246, 186, 190), 14);
    }

    // 身体
    p.ellipseOutline(x, y - 21.0f, 17.0f, 14.5f, fur, kOutline, 1.8f, 26);
    p.ellipseOutline(x, y - 18.0f, 10.0f, 8.0f, Color(252, 246, 236), furShade, 1.0f, 18);
    p.ellipseOutline(x - 17.0f * f, y - 25.0f, 5.0f, 7.2f, fur, kOutline, 1.2f, 14);
    p.ellipseOutline(x + 17.0f * f, y - 25.0f, 5.0f, 7.2f, fur, kOutline, 1.2f, 14);

    // 头
    p.ellipseOutline(x, y - 50.0f, 24.0f, 19.0f, fur, kOutline, 1.8f, 34);
    // 吻部 + 粉鼻头
    p.ellipseOutline(x, y - 39.0f, 8.5f, 5.8f, Color(252, 246, 236), furShade, 1.0f, 18);
    p.ellipse(x, y - 41.2f, 2.8f, 2.2f, Color(242, 154, 158), 10);
    dSmile(p, x, y - 36.0f, f, 4.2f, 2.2f);
    // 胡须
    for (float s : {-1.0f, 1.0f}) {
        p.line(x + s * 9.0f * f, y - 42.0f, x + s * 18.0f * f, y - 45.0f, furShade, 1.0f);
        p.line(x + s * 9.0f * f, y - 39.5f, x + s * 19.0f * f, y - 38.5f, furShade, 1.0f);
    }
    dEyes(p, x, y - 52.0f, f, 10.5f, 6.0f, 7.0f, rim);
    dBlush(p, x, y - 41.0f, f, 17.0f);
}

// 4 奶娃：胖娃娃，一撮呆毛、背带裤，肤色比其它造型暖。
void drawDragonBabyHuman(Painter& p, float x, float y, float f, bool moving, float phase,
                         float time) {
    const Color skin(255, 224, 198);
    const Color skinShade(238, 200, 172);
    const Color rim(186, 140, 112);
    const Color hair(126, 88, 60);
    dFeet(p, x, y, f, moving, phase, skinShade, 9.0f, 7.2f, 4.4f);

    // 身体：背带裤
    p.ellipseOutline(x, y - 21.0f, 16.0f, 15.0f, Color(250, 198, 92), kOutline, 1.6f, 24);
    p.line(x - 8.5f * f, y - 33.0f, x - 6.5f * f, y - 24.0f, kOutline, 2.0f);
    p.line(x + 8.5f * f, y - 33.0f, x + 6.5f * f, y - 24.0f, kOutline, 2.0f);
    p.ellipse(x, y - 27.0f, 2.8f, 2.8f, kCreamShade, 12);
    // 手
    p.ellipseOutline(x - 17.0f * f, y - 26.0f, 5.0f, 7.2f, skin, kOutline, 1.2f, 14);
    p.ellipseOutline(x + 17.0f * f, y - 26.0f, 5.0f, 7.2f, skin, kOutline, 1.2f, 14);

    // 呆毛（先画，从头顶冒出来）
    const float sway = std::sin(time * 2.2f) * 2.0f;
    p.bezier({x, y - 70.0f}, {x + sway, y - 78.0f}, {x + 9.0f * f + sway, y - 79.0f}, hair,
             2.6f, 10);

    // 头
    p.ellipseOutline(x, y - 52.0f, 24.0f, 20.0f, skin, kOutline, 1.8f, 34);
    // 刘海：两段弧压在额头
    p.bezier({x - 17.0f * f, y - 60.0f}, {x - 6.0f * f, y - 71.0f}, {x + 3.0f * f, y - 68.0f},
             hair, 3.2f, 10);
    p.bezier({x - 1.0f * f, y - 69.0f}, {x + 9.0f * f, y - 70.0f}, {x + 16.0f * f, y - 61.0f},
             hair, 3.2f, 10);

    dSmile(p, x, y - 38.0f, f, 5.0f, 2.8f);
    dEyes(p, x, y - 54.0f, f, 10.0f, 6.0f, 7.0f, rim);
    dBlush(p, x, y - 42.0f, f, 17.0f);
}

// 5 奶兔：长耳 + 圆身 + 棉花尾。
void drawDragonRabbit(Painter& p, float x, float y, float f, bool moving, float phase,
                      float time) {
    const Color fur(253, 250, 243);
    const Color furShade(226, 216, 202);
    const Color rim(170, 152, 132);
    const float b = -f;
    dFeet(p, x, y, f, moving, phase, furShade, 9.0f, 7.6f, 4.6f);

    // 棉花尾
    p.ellipseOutline(x + 16.0f * b, y - 16.0f, 6.5f, 6.5f, fur, kOutline, 1.2f, 14);

    // 长耳（先画，压在头后面）
    for (float s : {-1.0f, 1.0f}) {
        const float tilt = std::sin(time * 1.6f + static_cast<float>(s)) * 1.8f;
        p.ellipseOutline(x + s * 10.0f * f + tilt, y - 70.0f, 6.4f, 14.0f, fur, kOutline,
                         1.6f, 20);
        p.ellipse(x + s * 10.0f * f + tilt, y - 70.0f, 3.3f, 9.5f, Color(246, 190, 196), 14);
    }

    // 身体
    p.ellipseOutline(x, y - 21.0f, 17.0f, 15.0f, fur, kOutline, 1.8f, 26);
    p.ellipse(x, y - 18.0f, 9.5f, 8.0f, furShade, 16, 70);
    p.ellipseOutline(x - 17.0f * f, y - 25.0f, 5.0f, 7.4f, fur, kOutline, 1.2f, 14);
    p.ellipseOutline(x + 17.0f * f, y - 25.0f, 5.0f, 7.4f, fur, kOutline, 1.2f, 14);

    // 头
    p.ellipseOutline(x, y - 50.0f, 23.0f, 19.0f, fur, kOutline, 1.8f, 32);
    p.ellipse(x, y - 41.0f, 2.8f, 2.2f, Color(242, 158, 166), 10);
    dSmile(p, x, y - 37.5f, f, 4.4f, 2.4f);
    dEyes(p, x, y - 52.0f, f, 10.0f, 6.0f, 7.0f, rim);
    dBlush(p, x, y - 41.0f, f, 16.5f);
}

// 6 奶猫：橘猫，圆耳 + 额头虎斑 + 卷尾。
void drawDragonCat(Painter& p, float x, float y, float f, bool moving, float phase,
                   float time) {
    (void)time;
    const Color fur(246, 172, 104);
    const Color furShade(222, 142, 82);
    const Color stripe(198, 112, 62);
    const Color rim(168, 104, 54);
    const float b = -f;
    // 尾巴：粗带子 + 三道虎斑
    dTaper(p, {x + 12.0f * b, y - 14.0f}, {x + 32.0f * b, y - 26.0f},
           {x + 31.0f * b, y - 3.0f}, 6.0f, 2.6f, furShade);
    for (int i = 0; i < 3; ++i) {
        const float t = 0.28f + 0.22f * static_cast<float>(i);
        const float tx = x + (12.0f + 20.0f * t) * b;
        const float ty = y - 14.0f - 13.0f * std::sin(kPi * t);
        p.line(tx, ty - 3.0f, tx, ty + 3.0f, stripe, 1.6f);
    }

    dFeet(p, x, y, f, moving, phase, fur, 9.5f, 7.8f, 4.8f);

    // 圆润三角耳
    for (float s : {-1.0f, 1.0f}) {
        p.polygon({{x + s * 7.0f * f, y - 62.0f},
                   {x + s * 12.0f * f, y - 76.0f},
                   {x + s * 20.0f * f, y - 63.0f}},
                  fur, kOutline, 1.4f);
        p.ellipse(x + s * 12.5f * f, y - 68.0f, 2.6f, 3.0f, Color(246, 178, 178), 10);
    }

    // 身体
    p.ellipseOutline(x, y - 21.0f, 17.0f, 15.0f, fur, kOutline, 1.8f, 26);
    p.ellipse(x, y - 18.0f, 9.5f, 8.0f, Color(255, 238, 214), 16, 90);
    p.ellipseOutline(x - 17.0f * f, y - 25.0f, 5.0f, 7.4f, fur, kOutline, 1.2f, 14);
    p.ellipseOutline(x + 17.0f * f, y - 25.0f, 5.0f, 7.4f, fur, kOutline, 1.2f, 14);

    // 头
    p.ellipseOutline(x, y - 50.0f, 24.0f, 19.0f, fur, kOutline, 1.8f, 34);
    // 额头虎斑：三短竖，落在头顶内侧，不碰描边
    for (int i = -1; i <= 1; ++i) {
        p.line(x + static_cast<float>(i) * 5.5f * f, y - 66.0f,
               x + static_cast<float>(i) * 7.0f * f, y - 60.0f, stripe, 2.0f);
    }
    // 口鼻
    p.ellipseOutline(x, y - 38.0f, 8.0f, 5.6f, Color(255, 242, 224), furShade, 1.0f, 18);
    p.polygon({{x - 2.6f * f, y - 40.6f}, {x + 2.6f * f, y - 40.6f}, {x, y - 38.2f}},
              Color(238, 146, 150), Color(238, 146, 150), 0.0f);
    for (float s : {-1.0f, 1.0f}) {
        p.line(x + s * 8.5f * f, y - 39.0f, x + s * 18.0f * f, y - 42.0f, furShade, 1.0f);
        p.line(x + s * 8.5f * f, y - 37.0f, x + s * 19.0f * f, y - 36.0f, furShade, 1.0f);
    }
    dEyes(p, x, y - 52.0f, f, 10.5f, 6.0f, 7.0f, rim);
    dBlush(p, x, y - 41.0f, f, 17.0f);
}

// 造型分发：所有外部调用都走这里，切造型只改 kDragonVariant。
void drawDragonVariant(Painter& p, int variant, FPoint pos, bool moving, float walkPhase,
                       float time, int facing) {
    const float bob = moving ? -std::fabs(std::sin(walkPhase * 2.0f * kPi)) * 2.5f
                             : std::sin(time * 2.1f) * 1.0f;
    const float x = pos.x;
    const float y = pos.y + bob;
    const float f = facing < 0 ? -1.0f : 1.0f;

    // 脚下投影：取地面色压暗，任何地形上都贴得住。
    const Color ground = p.sample(pos.x, pos.y);
    p.ellipse(pos.x, pos.y + 1.0f, 23.0f, 6.5f, darken(ground, 0.72f), 24);

    switch (variant) {
    case 1: drawDragonEgg(p, x, y, f, moving, walkPhase, time); break;
    case 3: drawDragonMouse(p, x, y, f, moving, walkPhase, time); break;
    case 4: drawDragonBabyHuman(p, x, y, f, moving, walkPhase, time); break;
    case 5: drawDragonRabbit(p, x, y, f, moving, walkPhase, time); break;
    case 6: drawDragonCat(p, x, y, f, moving, walkPhase, time); break;
    default: drawDragonBaby(p, x, y, f, moving, walkPhase, time); break;
    }
}

void drawDragon(Painter& p, FPoint pos, bool moving, float walkPhase, float time,
                int facing) {
    drawDragonVariant(p, kDragonVariant, pos, moving, walkPhase, time, facing);
}

// 通用剪影：头 + 身 + 脚 + 耳/角/尾的外包，六个造型共用。
// 影子是低透明度的模糊一坨，不需要逐造型精确，也就不必跟着造型同步几何。
void drawDragonSilhouette(Painter& p, FPoint pos, int facing) {
    const float y = pos.y;
    const float f = facing < 0 ? -1.0f : 1.0f;
    p.ellipse(pos.x, y - 21.0f, 20.0f, 17.0f, Color::White(), 26);
    p.ellipse(pos.x, y - 54.0f, 24.0f, 19.0f, Color::White(), 30);
    p.ellipse(pos.x - 10.0f * f, y - 3.0f, 9.0f, 5.5f, Color::White(), 14);
    p.ellipse(pos.x + 10.0f * f, y - 3.0f, 9.0f, 5.5f, Color::White(), 14);
    p.ellipse(pos.x - 14.0f * f, y - 71.0f, 6.5f, 8.0f, Color::White(), 12);
    p.ellipse(pos.x + 14.0f * f, y - 71.0f, 6.5f, 8.0f, Color::White(), 12);
    p.ellipse(pos.x + 27.0f * f, y - 16.0f, 10.0f, 8.0f, Color::White(), 14);
}

// ---------------- 悬停提示 ----------------

void drawBracket(Painter& p, float x, float y, float sx, float sy, float len) {
    p.line(x, y, x + sx * len, y, kOutline, 4.0f);
    p.line(x, y, x, y + sy * len, kOutline, 4.0f);
    p.line(x, y, x + sx * len, y, kCream, 2.0f);
    p.line(x, y, x, y + sy * len, kCream, 2.0f);
}

void drawHoverFish(Painter& p, float cx, float cy) {
    p.ellipseOutline(cx, cy, 8.0f, 4.5f, kCream, kOutline, 1.0f, 18);
    const std::vector<FPoint> tail = {
        {cx + 7.0f, cy}, {cx + 13.0f, cy - 4.5f}, {cx + 13.0f, cy + 4.5f}};
    p.polygon(tail, kCream, kOutline, 1.0f);
    p.ellipse(cx - 4.0f, cy - 1.0f, 1.2f, 1.2f, kOutline, 8);
}

void drawHover(Painter& p, const Hover& h, float camX, float camY, float time) {
    if (h.kind == HoverKind::None) {
        return;
    }

    if (h.kind == HoverKind::Plot) {
        const RectI r = plotRect(h.plot);
        const float x = static_cast<float>(r.x) - camX;
        const float y = static_cast<float>(r.y) - camY;
        p.rectOutline(x - 3.0f, y - 3.0f, static_cast<float>(r.w) + 6.0f,
                      static_cast<float>(r.h) + 6.0f, kOutline, 5.0f);
        p.rectOutline(x - 1.0f, y - 1.0f, static_cast<float>(r.w) + 2.0f,
                      static_cast<float>(r.h) + 2.0f, kCream, 2.0f);
        const float t = 13.0f;
        drawBracket(p, x - 7.0f, y - 7.0f, -1.0f, -1.0f, t);
        drawBracket(p, x + static_cast<float>(r.w) + 7.0f, y - 7.0f, 1.0f, -1.0f, t);
        drawBracket(p, x - 7.0f, y + static_cast<float>(r.h) + 7.0f, -1.0f, 1.0f, t);
        drawBracket(p, x + static_cast<float>(r.w) + 7.0f,
                    y + static_cast<float>(r.h) + 7.0f, 1.0f, 1.0f, t);
        return;
    }

    const float x = static_cast<float>(h.cell.x * kCell) - camX;
    const float y = static_cast<float>(h.cell.y * kCell) - camY;
    const float m = h.kind == HoverKind::Pond ? 1.0f : 2.0f;
    const float t = 9.0f;
    drawBracket(p, x + m, y + m, 1.0f, 1.0f, t);
    drawBracket(p, x + static_cast<float>(kCell) - m, y + m, -1.0f, 1.0f, t);
    drawBracket(p, x + m, y + static_cast<float>(kCell) - m, 1.0f, -1.0f, t);
    drawBracket(p, x + static_cast<float>(kCell) - m,
                y + static_cast<float>(kCell) - m, -1.0f, -1.0f, t);

    const float cx = x + static_cast<float>(kCell) * 0.5f;
    if (h.kind == HoverKind::Pond) {
        const float bob = std::sin(time * 3.0f) * 2.0f;
        drawHoverFish(p, cx, y - 8.0f + bob);
    } else if (h.kind == HoverKind::Door) {
        // 进门提示：一扇奶油色小门。
        p.rectOutline(cx - 6.0f, y - 14.0f, 12.0f, 15.0f, kOutline, 3.0f);
        p.rect(cx - 4.0f, y - 12.0f, 8.0f, 11.0f, kCream);
        p.ellipse(cx + 1.5f, y - 7.0f, 1.0f, 1.0f, kOutline, 8);
    } else if (h.kind == HoverKind::Bed) {
        // 睡觉提示：奶油色 Z 字。
        p.line(cx - 5.0f, y - 13.0f, cx + 5.0f, y - 13.0f, kCream, 2.0f);
        p.line(cx + 5.0f, y - 13.0f, cx - 5.0f, y - 3.0f, kCream, 2.0f);
        p.line(cx - 5.0f, y - 3.0f, cx + 5.0f, y - 3.0f, kCream, 2.0f);
    } else {
        p.ellipse(cx, y + static_cast<float>(kCell) * 0.5f, 2.5f, 2.5f, kCream, 12);
    }
}

// ---------------- HUD ----------------

void drawOrderBoard(Painter& p, const FarmWorld& world, int viewW);

void drawSeedIcon(Painter& p, int kind, FPoint c) {
    if (kind == 0) {
        const std::vector<FPoint> outer = {{c.x - 18.0f, c.y + 10.0f},
                                           {c.x - 14.0f, c.y - 8.0f},
                                           {c.x, c.y - 16.0f},
                                           {c.x + 14.0f, c.y - 8.0f},
                                           {c.x + 18.0f, c.y + 10.0f},
                                           {c.x, c.y + 15.0f}};
        p.polygon(outer, kLeafDark, kOutline, 1.4f);
        p.ellipse(c.x, c.y - 1.0f, 12.0f, 11.0f, kLeaf, 22);
        p.ellipse(c.x - 4.0f, c.y - 4.0f, 4.0f, 5.0f, Color(228, 238, 192), 14);
        p.line(c.x - 12.0f, c.y + 8.0f, c.x - 3.0f, c.y - 1.0f, kLeafHi, 1.2f);
        p.line(c.x + 12.0f, c.y + 8.0f, c.x + 3.0f, c.y - 1.0f, kLeafHi, 1.2f);
    } else if (kind == 1) {
        const Color tomato = Color(206, 76, 54);
        p.ellipseOutline(c.x, c.y + 1.0f, 16.0f, 15.0f, tomato, Color(148, 44, 34), 1.4f,
                         24);
        p.ellipse(c.x - 5.0f, c.y - 4.0f, 4.0f, 3.0f, Color(240, 132, 104), 12);
        for (int i = 0; i < 5; ++i) {
            const float a = -3.14159f * 0.5f + static_cast<float>(i) * 1.25664f;
            p.line(c.x, c.y - 13.0f, c.x + std::cos(a) * 7.0f,
                   c.y - 13.0f + std::sin(a) * 5.0f, kLeafDark, 2.2f);
        }
        p.ellipse(c.x, c.y - 15.0f, 2.0f, 2.0f, kLeaf, 10);
    } else {
        const std::vector<FPoint> root = {{c.x - 11.0f, c.y - 10.0f},
                                          {c.x + 11.0f, c.y - 10.0f},
                                          {c.x + 7.0f, c.y - 1.0f},
                                          {c.x + 2.0f, c.y + 18.0f},
                                          {c.x - 2.0f, c.y + 18.0f},
                                          {c.x - 7.0f, c.y - 1.0f}};
        p.polygon(root, Color(226, 128, 48), Color(166, 82, 28), 1.4f);
        p.ellipse(c.x, c.y - 10.0f, 11.0f, 4.0f, Color(226, 128, 48), 18);
        p.line(c.x - 8.0f, c.y - 4.0f, c.x + 8.0f, c.y - 4.0f, Color(166, 82, 28), 1.2f);
        p.line(c.x - 6.0f, c.y + 3.0f, c.x + 6.0f, c.y + 3.0f, Color(166, 82, 28), 1.0f);
        p.line(c.x, c.y - 12.0f, c.x - 11.0f, c.y - 26.0f, kLeafDark, 2.0f);
        p.line(c.x, c.y - 12.0f, c.x - 3.0f, c.y - 29.0f, kLeaf, 1.8f);
        p.line(c.x, c.y - 12.0f, c.x + 5.0f, c.y - 29.0f, kLeafDark, 2.0f);
        p.line(c.x, c.y - 12.0f, c.x + 11.0f, c.y - 24.0f, kLeaf, 1.8f);
        p.ellipse(c.x - 9.0f, c.y - 22.0f, 5.0f, 2.4f, kLeaf, 10);
        p.ellipse(c.x - 3.0f, c.y - 26.0f, 4.5f, 2.2f, kLeafLight, 10);
        p.ellipse(c.x + 4.0f, c.y - 26.0f, 5.0f, 2.4f, kLeaf, 10);
        p.ellipse(c.x + 9.0f, c.y - 21.0f, 4.0f, 2.0f, kLeafLight, 10);
    }
}

// 钓鱼操作提示条：贴着热键栏上方，按钓鱼状态换文案。
// 试玩反馈"鱼钓不上来"的根因是咬钩窗口没有任何操作指引——提示条常驻钓鱼全程。
void drawFishingHint(Painter& p, const FarmWorld& world, int viewW, int viewH) {
    const char* s = nullptr;
    bool urgent = false;
    switch (world.fishState()) {
    case FishState::Casting:
        s = "抛竿…";
        break;
    case FishState::Waiting:
        s = "钓鱼中……";
        break;
    case FishState::Biting:
        s = "上钩了，点击左键！";
        urgent = true;
        break;
    case FishState::Caught:
        s = "钓到了！";
        break;
    case FishState::Missed:
        s = "鱼跑掉了……";
        break;
    case FishState::Idle:
        return;
    }

    const int w = 320;
    const int h = 34;
    const int x = viewW / 2 - w / 2;
    const int y = viewH - kHudSlot - 18 - h - 8;
    p.rect(static_cast<float>(x + 4), static_cast<float>(y + 5), static_cast<float>(w),
           static_cast<float>(h), kBorderGrass);
    p.rect(static_cast<float>(x), static_cast<float>(y), static_cast<float>(w),
           static_cast<float>(h), kOutline);
    p.rect(static_cast<float>(x + 2), static_cast<float>(y + 2),
           static_cast<float>(w - 4), static_cast<float>(h - 4), kWood);
    p.rect(static_cast<float>(x + 6), static_cast<float>(y + 6),
           static_cast<float>(w - 12), 2.0f, kWoodLight);

    // 咬钩时红白交替闪烁，逼玩家在 0.8 秒窗口里做出反应。
    const Color tc = !urgent ? kCream
                             : ((static_cast<int>(world.animTime() * 7.0f) & 1) != 0
                                    ? Color(206, 76, 54)
                                    : kWhite);
    text::drawText(p.fb(), x + (w - text::textWidth(s)) / 2, y + 11, s, tc);
}

void drawHud(Painter& p, const FarmWorld& world, int viewW, int viewH) {
    drawOrderBoard(p, world, viewW);

    const RectI plaque = titlePlaqueRect();
    p.rect(static_cast<float>(plaque.x + 4), static_cast<float>(plaque.y + 5),
           static_cast<float>(plaque.w), static_cast<float>(plaque.h), kBorderGrass);
    p.rect(static_cast<float>(plaque.x), static_cast<float>(plaque.y),
           static_cast<float>(plaque.w), static_cast<float>(plaque.h), kOutline);
    p.rect(static_cast<float>(plaque.x + 2), static_cast<float>(plaque.y + 2),
           static_cast<float>(plaque.w - 4), static_cast<float>(plaque.h - 4), kWood);
    p.rect(static_cast<float>(plaque.x + 6), static_cast<float>(plaque.y + 6),
           static_cast<float>(plaque.w - 12), 2.0f, kWoodLight);

    p.ellipse(static_cast<float>(plaque.x + 24), static_cast<float>(plaque.y + 24), 12.0f,
              11.0f, kDragon, 24);
    p.polygon({{static_cast<float>(plaque.x + 16), static_cast<float>(plaque.y + 15)},
               {static_cast<float>(plaque.x + 20), static_cast<float>(plaque.y + 14)},
               {static_cast<float>(plaque.x + 18), static_cast<float>(plaque.y + 7)}},
              kCreamShade, kOutline, 1.0f);
    p.polygon({{static_cast<float>(plaque.x + 32), static_cast<float>(plaque.y + 15)},
               {static_cast<float>(plaque.x + 28), static_cast<float>(plaque.y + 14)},
               {static_cast<float>(plaque.x + 30), static_cast<float>(plaque.y + 7)}},
              kCreamShade, kOutline, 1.0f);
    p.ellipse(static_cast<float>(plaque.x + 20), static_cast<float>(plaque.y + 23), 3.0f,
              3.5f, kWhite, 12);
    p.ellipse(static_cast<float>(plaque.x + 28), static_cast<float>(plaque.y + 23), 3.0f,
              3.5f, kWhite, 12);
    p.ellipse(static_cast<float>(plaque.x + 21), static_cast<float>(plaque.y + 24), 1.2f,
              1.4f, Color(32, 24, 20), 8);
    p.ellipse(static_cast<float>(plaque.x + 29), static_cast<float>(plaque.y + 24), 1.2f,
              1.4f, Color(32, 24, 20), 8);
    p.ellipse(static_cast<float>(plaque.x + 24), static_cast<float>(plaque.y + 29), 3.5f,
              2.0f, kCream, 12);

    text::drawText(p.fb(), plaque.x + 46, plaque.y + 16, "胖龙农场", kCream);

    const int selected = static_cast<int>(world.selectedSeed());
    for (int i = 0; i < 3; ++i) {
        RectI r = hotbarSlotRect(i, viewW, viewH);
        const bool sel = (i == selected);
        if (sel) {
            r.y -= 5;
        }
        p.rect(static_cast<float>(r.x + 4), static_cast<float>(r.y + 5),
               static_cast<float>(r.w), static_cast<float>(r.h), Color(48, 36, 24));
        p.rect(static_cast<float>(r.x), static_cast<float>(r.y), static_cast<float>(r.w),
               static_cast<float>(r.h), kOutline);
        p.rect(static_cast<float>(r.x + 2), static_cast<float>(r.y + 2),
               static_cast<float>(r.w - 4), static_cast<float>(r.h - 4),
               sel ? kWoodLight : kWood);
        p.rect(static_cast<float>(r.x + 5), static_cast<float>(r.y + 5),
               static_cast<float>(r.w - 10), static_cast<float>(r.h - 10),
               Color(94, 66, 42));
        p.rect(static_cast<float>(r.x + 5), static_cast<float>(r.y + 5),
               static_cast<float>(r.w - 10), 2.0f, Color(122, 88, 56));
        if (sel) {
            p.rectOutline(static_cast<float>(r.x - 3), static_cast<float>(r.y - 3),
                          static_cast<float>(r.w + 6), static_cast<float>(r.h + 6), kCream,
                          3.0f);
            const float cx = static_cast<float>(r.x) + static_cast<float>(r.w) * 0.5f;
            p.polygon({{cx - 6.0f, static_cast<float>(r.y) - 10.0f},
                       {cx + 6.0f, static_cast<float>(r.y) - 10.0f},
                       {cx, static_cast<float>(r.y) - 2.0f}},
                      kCream, kOutline, 1.0f);
        }
        drawSeedIcon(p, i,
                     {static_cast<float>(r.x) + static_cast<float>(r.w) * 0.5f,
                      static_cast<float>(r.y) + static_cast<float>(r.h) * 0.5f + 2.0f});
    }

    drawFishingHint(p, world, viewW, viewH);

    // 右下角常驻重开按钮：订单完成后游戏不结束，随时可以开新一局。
    const RectI rb = restartButtonRect(viewW, viewH);
    p.rect(static_cast<float>(rb.x + 3), static_cast<float>(rb.y + 4),
           static_cast<float>(rb.w), static_cast<float>(rb.h), Color(48, 36, 24), 140);
    p.rect(static_cast<float>(rb.x), static_cast<float>(rb.y), static_cast<float>(rb.w),
           static_cast<float>(rb.h), kOutline);
    p.rect(static_cast<float>(rb.x + 2), static_cast<float>(rb.y + 2),
           static_cast<float>(rb.w - 4), static_cast<float>(rb.h - 4), kWood);
    p.rect(static_cast<float>(rb.x + 5), static_cast<float>(rb.y + 5),
           static_cast<float>(rb.w - 10), 2.0f, kWoodLight);
    const char* restartLabel = "重开 R";
    text::drawText(p.fb(), rb.x + (rb.w - text::textWidth(restartLabel)) / 2, rb.y + 9,
                   restartLabel, kCream);
}

// ---------------- 钓鱼与粒子 ----------------

// 鱼线与鱼漂：抛竿时鱼线沿二次 Bezier 展开（实验四曲线的实际应用），
// 其余状态鱼线从竿尖直连鱼漂。
void drawFishing(Painter& p, const FarmWorld& world, float camX, float camY) {
    if (world.fishState() == FishState::Idle) {
        return;
    }
    const float f = world.facing() >= 0 ? 1.0f : -1.0f;
    const FPoint tip{world.playerPos().x - camX + 10.0f * f,
                     world.playerPos().y - camY - 40.0f};
    const float bx = world.bobberPos().x - camX;
    const float by = world.bobberPos().y - camY;

    if (world.fishState() == FishState::Casting) {
        const FPoint c{world.bobberCtrl().x - camX, world.bobberCtrl().y - camY};
        FPoint prev = tip;
        for (int i = 1; i <= 24; ++i) {
            const float t = static_cast<float>(i) / 24.0f;
            const float u = 1.0f - t;
            const FPoint q{u * u * tip.x + 2.0f * u * t * c.x + t * t * bx,
                           u * u * tip.y + 2.0f * u * t * c.y + t * t * by};
            p.line(prev.x, prev.y, q.x, q.y, kOutline, 1.0f);
            prev = q;
        }
    } else {
        p.line(tip.x, tip.y, bx, by, kOutline, 1.0f);
    }

    // 鱼漂：上红下白的小浮子；咬钩时位置已在水面下 5px。
    p.ellipseOutline(bx, by - 1.5f, 4.2f, 3.6f, Color(206, 76, 54), kOutline, 1.0f, 16);
    p.ellipseOutline(bx, by + 2.2f, 3.4f, 2.6f, kWhite, kOutline, 1.0f, 14);
}

// 咬钩提示气泡：画在角色头顶，随时间轻微浮动。
void drawBiteBubble(Painter& p, const FarmWorld& world, float camX, float camY) {
    if (world.fishState() != FishState::Biting) {
        return;
    }
    const float x = world.playerPos().x - camX;
    const float y = world.playerPos().y - camY - 92.0f +
                    std::sin(world.animTime() * 6.0f) * 2.0f;
    p.ellipseOutline(x, y, 30.0f, 17.0f, kCream, kOutline, 1.5f, 26);
    p.polygon({{x - 6.0f, y + 14.0f}, {x + 6.0f, y + 14.0f}, {x, y + 24.0f}}, kCream,
              kOutline, 1.0f);
    const char* s = "咬钩!";
    text::drawText(p.fb(), static_cast<int>(x) - text::textWidth(s) / 2,
                   static_cast<int>(y) - 8, s, kOutline);
}

// 粒子：水滴下坠、涟漪扩散、闪光收缩，按类型各画各的。
void drawParticles(Painter& p, const FarmWorld& world, float camX, float camY) {
    for (const Particle& pt : world.particles()) {
        const float x = pt.pos.x - camX;
        const float y = pt.pos.y - camY;
        const float t = std::clamp(pt.life / pt.maxLife, 0.0f, 1.0f);
        switch (pt.type) {
        case ParticleType::Droplet:
            p.ellipse(x, y, 2.2f, 2.0f, t < 0.5f ? kWaterShimmer : kWaterLight, 10);
            break;
        case ParticleType::Ring: {
            const float r = 4.0f + 30.0f * t;
            p.lineLoopEllipse(x, y, r, r * 0.6f, kWaterShimmer, 1.5f, 24);
            break;
        }
        case ParticleType::Sparkle: {
            const float s = (1.0f - t) * 6.0f + 2.0f;
            const Color c = (static_cast<int>(pt.life * 20.0f) & 1) != 0 ? kFlowerYellow
                                                                        : kWhite;
            p.line(x - s, y, x + s, y, c, 1.0f);
            p.line(x, y - s, x, y + s, c, 1.0f);
            break;
        }
        }
    }
}

// 池塘动态水纹：几段高光弧线随时间明灭游走，水面不再是一张死图。
void drawPondShimmer(Painter& p, const FarmWorld& world, float camX, float camY) {
    struct Shimmer {
        float cx;
        float cy;
        float rx;
        float ry;
        float phase;
    };
    const Shimmer spots[] = {{kPondCx - 62.0f, kPondCy - 104.0f, 58.0f, 17.0f, 0.0f},
                             {kPondCx + 38.0f, kPondCy + 44.0f, 44.0f, 12.0f, 2.1f},
                             {kPondCx + 58.0f, kPondCy + 78.0f, 58.0f, 15.0f, 4.2f},
                             {kPondCx - 20.0f, kPondCy + 10.0f, 50.0f, 13.0f, 1.2f}};
    for (const Shimmer& s : spots) {
        // 明灭靠颜色向水面色插值（线段是不透明绘制），暗相位直接融入水面。
        const float t = std::clamp(std::sin(world.animTime() * 1.6f + s.phase), 0.0f, 1.0f);
        if (t < 0.12f) {
            continue;
        }
        const float drift = std::sin(world.animTime() * 0.7f + s.phase) * 6.0f;
        p.arc(s.cx + drift - camX, s.cy - camY, s.rx, s.ry, 3.45f, 5.75f,
              lerpColor(kWaterMid, kWaterShimmer, t * 0.85f), 1.4f, 14);
    }
}

// 小屋炊烟：三缕烟循环上升、放大、变淡。
void drawChimneySmoke(Painter& p, const FarmWorld& world, float camX, float camY) {
    const float topX = kCabinX + kCabinW - 61.0f;
    const float topY = kCabinY - 30.0f;
    for (int i = 0; i < 3; ++i) {
        const float t = std::fmod(world.animTime() * 0.22f + static_cast<float>(i) / 3.0f,
                                  1.0f);
        const float x = topX + t * 20.0f - camX;
        const float y = topY - t * 52.0f - camY;
        const float r = 5.5f + t * 10.0f;
        // 前一半行程保持接近实心，避免地面树影从烟里透出硬边；升高后再线性淡出。
        const float fade = std::clamp(1.0f - (t - 0.5f) / 0.5f, 0.0f, 1.0f);
        const std::uint8_t a = static_cast<std::uint8_t>(fade * 235.0f);
        p.ellipse(x, y, r, r * 0.8f, Color(238, 238, 232), 14, a);
    }
}

// ---------------- 订单板 ----------------

void drawMiniIcon(Painter& p, int kind, float cx, float cy) {
    switch (kind) {
    case 0: // 白菜
        p.ellipseOutline(cx, cy, 9.0f, 8.0f, kLeaf, kLeafDark, 1.0f, 16);
        p.ellipse(cx - 2.0f, cy - 2.0f, 3.4f, 3.0f, Color(228, 238, 192), 10);
        break;
    case 1: // 番茄
        p.ellipseOutline(cx, cy + 1.0f, 8.5f, 8.0f, Color(206, 76, 54),
                         Color(148, 44, 34), 1.0f, 18);
        p.line(cx - 3.0f, cy - 6.0f, cx, cy - 9.0f, kLeafDark, 1.4f);
        p.line(cx + 3.0f, cy - 6.0f, cx, cy - 9.0f, kLeafDark, 1.4f);
        break;
    case 2: // 胡萝卜
        p.polygon({{cx - 5.0f, cy - 6.0f}, {cx + 5.0f, cy - 6.0f}, {cx, cy + 8.0f}},
                  Color(226, 128, 48), Color(166, 82, 28), 1.0f);
        p.line(cx - 1.0f, cy - 7.0f, cx - 5.0f, cy - 12.0f, kLeafDark, 1.4f);
        p.line(cx + 1.0f, cy - 7.0f, cx + 5.0f, cy - 12.0f, kLeafDark, 1.4f);
        break;
    default: // 鱼
        p.ellipseOutline(cx - 2.0f, cy, 7.0f, 4.0f, kFish, kOutline, 1.0f, 16);
        p.polygon({{cx + 4.0f, cy}, {cx + 10.0f, cy - 4.0f}, {cx + 10.0f, cy + 4.0f}},
                  kFish, kOutline, 1.0f);
        p.ellipse(cx - 6.0f, cy - 1.0f, 1.0f, 1.0f, kOutline, 8);
        break;
    }
}

void drawOrderBoard(Painter& p, const FarmWorld& world, int viewW) {
    const RectI b = orderBoardRect(viewW);
    p.rect(static_cast<float>(b.x + 4), static_cast<float>(b.y + 5),
           static_cast<float>(b.w), static_cast<float>(b.h), kBorderGrass);
    p.rect(static_cast<float>(b.x), static_cast<float>(b.y), static_cast<float>(b.w),
           static_cast<float>(b.h), kOutline);
    p.rect(static_cast<float>(b.x + 2), static_cast<float>(b.y + 2),
           static_cast<float>(b.w - 4), static_cast<float>(b.h - 4), kWood);
    p.rect(static_cast<float>(b.x + 6), static_cast<float>(b.y + 6),
           static_cast<float>(b.w - 12), 2.0f, kWoodLight);

    text::drawText(p.fb(), b.x + 12, b.y + 12, "订单板", kCream);
    p.line(static_cast<float>(b.x + 10), static_cast<float>(b.y + 36),
           static_cast<float>(b.x + b.w - 10), static_cast<float>(b.y + 36), kWoodDark,
           1.5f);

    struct Row {
        int kind;
        int need;
        int have;
    };
    const Row rows[4] = {{0, kOrderCabbage, world.inventory().cabbage},
                         {1, kOrderTomato, world.inventory().tomato},
                         {2, kOrderCarrot, world.inventory().carrot},
                         {3, kOrderFish, world.inventory().fish}};
    int y = b.y + 46;
    for (const Row& r : rows) {
        drawMiniIcon(p, r.kind, static_cast<float>(b.x + 30), static_cast<float>(y + 8));
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%d/%d", r.have, r.need);
        const bool done = r.have >= r.need;
        text::drawText(p.fb(), b.x + 52, y, buf,
                       done ? Color(134, 196, 108) : kCream);
        y += 26;
    }

    // 任务全部完成后在清单正下方标一行状态；不弹结算面板、不结束游戏。
    if (world.won()) {
        text::drawText(p.fb(), b.x + 12, y + 2, "全部任务已完成",
                       Color(134, 196, 108));
    }

    const int total = static_cast<int>(world.playSeconds());
    char t[16];
    std::snprintf(t, sizeof(t), "%02d:%02d", total / 60, total % 60);
    text::drawText(p.fb(), b.x + 12, y + 30, t, kCream);
}

// ---------------- 昼夜光照：影子 / 环境色 / 灯光 ----------------

constexpr Color kShadowTint{18, 22, 14};       // 最终合成到画面上的影子色
constexpr Color kShadowStamp{255, 255, 255};   // 影子蒙版里用的"强度色"，只在蒙版里读写
constexpr Color kMaskKey{255, 0, 255};         // 立体物件遮罩烘焙用的色键

// 路灯（点光源）的影子：沿背离灯的方向拉长的椭圆投影，和太阳影的椭圆同一套
// 观感——近端落在根部、中段最宽、远端渐收。长度随物体高度和到灯距离变化，
// 每帧重算。此前用四边形近似，近宽远窄的直边在夜里像一块梯形板。
// 离灯太近时径向方向不可靠（灯下奇异），影子淡出，避免翻转时突变。
// lampX/lampY 必须与 baseX/baseY 同一坐标系（视口坐标），否则镜头一动影子就偏。
void lampShadowBlob(Painter& p, float baseX, float baseY, float lampX, float lampY,
                    float radius, float height, std::uint8_t alpha) {
    const float dx = baseX - lampX;
    const float dy = baseY - lampY;
    const float dist = std::sqrt(dx * dx + dy * dy);
    if (dist < 1.0f) {
        return;
    }
    const std::uint8_t a = static_cast<std::uint8_t>(
        static_cast<float>(alpha) * smoothStep(16.0f, 64.0f, dist));
    if (a == 0) {
        return;
    }
    const float ux = dx / dist;
    const float uy = dy / dist;
    const float len = std::clamp(radius + height * (0.9f + dist * 0.0035f), 40.0f, 340.0f);
    // 椭圆长轴从根部指向外，中心放在 len 的一半处，近端正好贴在基座上。
    const float cx = baseX + ux * len * 0.5f;
    const float cy = baseY + uy * len * 0.5f;
    const float semiU = len * 0.5f;
    const float semiV = radius * 0.72f;
    constexpr int kSegments = 24;
    std::vector<FPoint> pts;
    pts.reserve(kSegments);
    for (int i = 0; i < kSegments; ++i) {
        const float a =
            2.0f * kPi * static_cast<float>(i) / static_cast<float>(kSegments);
        const float fwd = std::cos(a) * semiU;
        const float side = std::sin(a) * semiV;
        pts.push_back({cx + ux * fwd - uy * side, cy + uy * fwd + ux * side});
    }
    p.polygon(pts, kShadowStamp, kShadowStamp, 0.0f, a);
}

// 胖龙的剪影影子：先把剪影以不透明色画进局部蒙版，再以统一 alpha 盖进影子层。
// 不能直接拿半透明形状往上叠——龙由 20 多个形状组成，肚皮、头身交界等重叠区
// 会被多次 src-over 混合而明显更深，剪影发花；蒙版法保证影子浓度处处一致。
// 盖章是真正的地面投影（以脚跟为锚的切变）：高度 h 的剪影点沿影子向量位移
// sv·h——脚底 h=0 原地不动、影子贴着脚跟，头顶被推得最远；晨昏自动拉长、
// 正午缩在脚下，影子永远与角色相连。
void stampDragonShadow(Painter& p, FrameBuffer& mask, FPoint base, FPoint sv,
                       std::uint8_t alpha, int facing) {
    // 龙最外包围（与 drawDragon/drawDragonSilhouette 几何一致）：
    // 尾尖最远 ±63、角顶 y-74、脚底 y+8，四周各留 3px 余量。
    const float minX = base.x - 66.0f;
    const float maxX = base.x + 66.0f;
    const float minY = base.y - 78.0f;
    const float maxY = base.y + 9.0f;
    const float scale = p.scale();
    const int mw = std::max(1, static_cast<int>(std::lround((maxX - minX) * scale)));
    const int mh = std::max(1, static_cast<int>(std::lround((maxY - minY) * scale)));
    mask.resize(mw, mh);
    mask.fillRect(0, 0, mw - 1, mh - 1, Color::Black());

    Painter mp(mask, scale, {minX, minY});
    drawDragonSilhouette(mp, base, facing);

    // 目标行反推源行：dy = base.y + sv.y·h 单调，最近邻采样即可（避免压扁后
    // 多行叠进一行造成竖向加深），每个目标像素只混一次影子色。
    // 调用方保证 |sv.y| ≥ 0.35，反推不发散。
    FrameBuffer& fb = p.fb();
    const float dTop = base.y + sv.y * (base.y - maxY);
    const float dBot = base.y + sv.y * (base.y - minY);
    const int fy0 = std::max(0, p.py(std::min(dTop, dBot)));
    const int fy1 = std::min(fb.height() - 1, p.py(std::max(dTop, dBot)));
    for (int fy = fy0; fy <= fy1; ++fy) {
        const float h = (p.unpy(fy) - base.y) / sv.y; // 目标行对应的离地高度
        const float sy = base.y - h;                  // 源剪影设计坐标
        const int my = static_cast<int>(std::lround((sy - minY) * scale - 0.5f));
        if (my < 0 || my >= mh) {
            continue;
        }
        const float shiftX = sv.x * h;
        for (int mx = 0; mx < mw; ++mx) {
            if (mask.pixel(mx, my) == Color::Black()) {
                continue; // 蒙版未覆盖
            }
            const float sx = minX + (static_cast<float>(mx) + 0.5f) / scale;
            const int fx = p.px(sx + shiftX);
            if (fx < 0 || fx >= fb.width()) {
                continue;
            }
            fb.blendPixel(fx, fy, kShadowStamp, alpha);
        }
    }
}

// 每帧把所有投影体的影子画进一张离屏强度蒙版，再由 compositeShadows 一次性
// 合成到画布：太阳方向光（晨昏拉长）与路灯点光（夜里）两套同时画、各自减淡。
// 影子集中在蒙版里管理，重叠区域只混一次浓度，不会叠加发黑。
void drawShadows(FrameBuffer& target, const FarmWorld& world, float camX, float camY,
                 const SkyState& sky, FrameBuffer& dragonMask) {
    target.clear(Color::Black());
    Painter p(target, 1.0f);
    const bool dual = sky.sunUp && sky.lampIntensity > 0.01f;
    const float ox = -camX;
    const float oy = -camY;
    // 路灯的视口坐标：与各投影体（已减相机）同一坐标系，方向计算才不错位。
    const float lampVX = kLampX + ox;
    const float lampVY = kLampY + oy;

    std::uint8_t sunA = 0;
    FPoint sv{0.0f, 0.0f};
    if (sky.sunUp) {
        sv = sunShadowVec(world.hour());
        float a = 0.30f - 0.10f * sky.sunElevation;
        if (dual) {
            a *= 0.7f;
        }
        sunA = static_cast<std::uint8_t>(a * 255.0f);
    }
    std::uint8_t lampA = 0;
    if (sky.lampIntensity > 0.01f) {
        float a = 0.30f * sky.lampIntensity;
        if (dual) {
            a *= 0.7f;
        }
        lampA = static_cast<std::uint8_t>(a * 255.0f);
    }

    // 树：影子锚在树根，长度按树冠高度折算。
    for (const TreeSpot& t : kTrees) {
        const float x = t.x + ox;
        const float y = t.y + oy;
        if (x < -260.0f || x > 1060.0f || y < -160.0f || y > 760.0f) {
            continue; // 视口外不画
        }
        const float h = 64.0f * t.scale;
        if (sunA > 0) {
            p.ellipse(x + sv.x * h, y + sv.y * h, 48.0f * t.scale, 15.0f * t.scale,
                      kShadowStamp, 26, sunA);
        }
        if (lampA > 0) {
            lampShadowBlob(p, x, y, lampVX, lampVY, 34.0f * t.scale, h, lampA);
        }
    }
    // 灌木
    for (const auto& b : kBushSpots) {
        const float x = b[0] + ox;
        const float y = b[1] + oy;
        if (x < -160.0f || x > 960.0f || y < -120.0f || y > 720.0f) {
            continue;
        }
        const float h = 26.0f * b[2];
        if (sunA > 0) {
            p.ellipse(x + sv.x * h, y + sv.y * h, 20.0f * b[2], 7.0f * b[2], kShadowStamp,
                      20, sunA);
        }
        if (lampA > 0) {
            lampShadowBlob(p, x, y, lampVX, lampVY, 15.0f * b[2], h, lampA);
        }
    }
    // 作物：田里长出来的都投一点影子，成熟期最高。
    for (int i = 0; i < kPlotCols * kPlotRows; ++i) {
        const Crop& c = world.crop(i);
        if (c.stage == CropStage::Empty || c.stage == CropStage::Seed) {
            continue;
        }
        const FPoint pc = plotCenter(i);
        const float x = pc.x + ox;
        const float y = pc.y + 26.0f + oy;
        const float h = c.stage == CropStage::Mature ? 34.0f : 18.0f;
        if (sunA > 0) {
            p.ellipse(x + sv.x * h, y + sv.y * h, 17.0f, 5.5f, kShadowStamp, 18, sunA);
        }
        if (lampA > 0) {
            lampShadowBlob(p, x, y, lampVX, lampVY, 12.0f, h, lampA);
        }
    }
    // 木桶
    {
        const float x = 682.0f + ox;
        const float y = 598.0f + oy;
        if (sunA > 0) {
            p.ellipse(x + sv.x * 20.0f, y + sv.y * 20.0f, 13.0f, 4.5f, kShadowStamp, 16,
                      sunA);
        }
        if (lampA > 0) {
            lampShadowBlob(p, x, y, lampVX, lampVY, 9.0f, 20.0f, lampA);
        }
    }
    // 胖龙：影子就是龙的剪影（离屏蒙版统一透明度盖章），以脚跟为锚做地面投影：
    // 高度 h 的点沿影子向量位移 sv·h，晨昏被推向西/东拉长、正午缩在脚下；
    // 夜里影子向量改为"背离路灯方向 × 与灯影四边形相同的生长率"。
    {
        const FPoint base{world.playerPos().x + ox, world.playerPos().y + 1.0f + oy};
        if (sunA > 0) {
            stampDragonShadow(p, dragonMask, base, sv, sunA, world.facing());
        }
        if (lampA > 0) {
            const float dx = base.x - lampVX;
            const float dy = base.y - lampVY;
            const float dist = std::sqrt(dx * dx + dy * dy);
            if (dist > 1.0f) {
                const float ux = dx / dist;
                const float uy = dy / dist;
                // 两个退化方向都淡出：离灯太近时径向方向奇异；影子方向接近水平
                // 时剪影会被压成一条线。角色从灯上方走到下方恰好经过这两处，
                // 淡出再淡入让影子自然收没、反向展开，而不是瞬间翻转。
                const float fade = smoothStep(16.0f, 64.0f, dist) *
                                   smoothStep(0.08f, 0.35f, std::fabs(uy));
                const float a = static_cast<float>(lampA) * fade;
                if (a >= 1.0f) {
                    const float k = 0.9f + dist * 0.0035f;
                    float sy = uy;
                    if (std::fabs(sy) < 0.10f) {
                        sy = sy < 0.0f ? -0.10f : 0.10f; // 仅防除零，此时已接近全透明
                    }
                    stampDragonShadow(p, dragonMask, base, {ux * k, sy * k},
                                      static_cast<std::uint8_t>(a), world.facing());
                }
            }
        }
    }
}

// 影子蒙版合成：把离屏强度蒙版一次性染成 kShadowTint 盖到画布上。静态立体
// 物件（树/灌木/木桶/路灯/小屋）覆盖的像素跳过，影子永远不会压在自己的树冠、
// 桶身或墙面上——影子层始终在立体物件之下。
void compositeShadows(FrameBuffer& fb, const FrameBuffer& shadow,
                      const std::vector<std::uint8_t>& propMask, int camX, int camY) {
    if (propMask.size() != static_cast<std::size_t>(kWorldW) * kWorldH) {
        return;
    }
    for (int y = 0; y < shadow.height() && y < fb.height(); ++y) {
        const int wy = camY + y;
        if (wy < 0 || wy >= kWorldH) {
            continue;
        }
        const std::size_t row = static_cast<std::size_t>(wy) * kWorldW;
        for (int x = 0; x < shadow.width() && x < fb.width(); ++x) {
            const std::uint8_t a = shadow.pixel(x, y).r;
            if (a == 0) {
                continue;
            }
            const int wx = camX + x;
            if (wx < 0 || wx >= kWorldW) {
                continue;
            }
            if (propMask[row + static_cast<std::size_t>(wx)] != 0) {
                continue;
            }
            fb.blendPixel(x, y, kShadowTint, a);
        }
    }
}

// 全屏环境色：夜里压暗偏蓝，晨昏扫一层暖橙。室内系数 <1（屋里亮一点）。
// HUD 在它之后绘制，不受影响。
void applyAmbient(FrameBuffer& fb, float hour, float indoorScale = 1.0f) {
    AmbientTint blue;
    AmbientTint warm;
    ambientTintAt(hour, blue, warm);
    if (blue.alpha > 0) {
        const std::uint8_t a =
            static_cast<std::uint8_t>(static_cast<float>(blue.alpha) * indoorScale);
        fb.blendRect(0, 0, fb.width() - 1, fb.height() - 1, Color(blue.r, blue.g, blue.b),
                     a);
    }
    if (warm.alpha > 0) {
        const std::uint8_t a =
            static_cast<std::uint8_t>(static_cast<float>(warm.alpha) * indoorScale);
        fb.blendRect(0, 0, fb.width() - 1, fb.height() - 1, Color(warm.r, warm.g, warm.b),
                     a);
    }
}

// 路灯光晕：同心暖色圆由外向内加强，灯泡核心最亮；画在夜色之后，把灯周"点亮"。
void drawLampGlow(Painter& p, const FarmWorld& world, float camX, float camY,
                  const SkyState& sky) {
    if (sky.lampIntensity <= 0.01f) {
        return;
    }
    const float flicker = 0.94f + 0.06f * std::sin(world.animTime() * 7.3f);
    const float x = kLampX - camX;
    const float y = kLampBulbY - camY;
    for (int i = 0; i < 8; ++i) {
        const float t = static_cast<float>(i) / 7.0f; // 0 外圈 → 1 内圈
        const float r = 250.0f - 190.0f * t;
        const std::uint8_t a = static_cast<std::uint8_t>(
            (3.0f + 22.0f * t) * sky.lampIntensity * flicker);
        p.ellipse(x, y, r, r * 0.92f, Color(255, 196, 102), 36, a);
    }
    const std::uint8_t core = static_cast<std::uint8_t>(170.0f * sky.lampIntensity);
    p.ellipse(x, y + 3.0f, 9.0f, 9.5f, Color(255, 236, 170), 18, core);
}

// ---------------- 夜间的自发光 ----------------

// 夜色权重：日落后 1.5 小时内淡入、日出前 1.5 小时内淡出，白昼为 0。
float nightFactor(float hour) {
    hour = std::fmod(hour + 24.0f, 24.0f);
    const float dusk = std::clamp((hour - 18.8f) / 1.5f, 0.0f, 1.0f);
    const float dawn = std::clamp((5.7f - hour) / 1.5f, 0.0f, 1.0f);
    return std::max(dusk, dawn);
}

// 萤火虫：草丛上空的自发光粒子，呼吸式明灭 + 缓慢漂移。
// 位置用确定性哈希撒点（同一次运行每帧一致），避开池塘与农田土台。
void drawFireflies(Painter& p, const FarmWorld& world, float camX, float camY) {
    const float night = nightFactor(world.hour());
    if (night <= 0.01f) {
        return;
    }
    struct Anchor {
        float x;
        float y;
    };
    static const std::vector<Anchor> anchors = [] {
        std::vector<Anchor> v;
        for (int i = 0; v.size() < 26 && i < 600; ++i) {
            const std::uint32_t h = hash2(i * 37 + 11, i * 91 + 29);
            const float x = 60.0f + static_cast<float>(h & 0x3FFu) / 1023.0f * (kWorldW - 120.0f);
            const float y =
                80.0f + static_cast<float>((h >> 10) & 0x3FFu) / 1023.0f * (kWorldH - 160.0f);
            if (inPond(x, y, 26.0f)) {
                continue;
            }
            if (x > kFarmX - 30.0f && x < kFarmX + kFarmW + 30.0f && y > kFarmY - 30.0f &&
                y < kFarmY + kFarmH + 30.0f) {
                continue; // 土台上不落萤火
            }
            v.push_back({x, y});
        }
        return v;
    }();

    const float t = world.animTime();
    for (std::size_t i = 0; i < anchors.size(); ++i) {
        const float fi = static_cast<float>(i);
        const float x = anchors[i].x + std::sin(t * 0.51f + fi * 1.7f) * 26.0f +
                        std::sin(t * 0.19f + fi * 0.6f) * 13.0f;
        const float y = anchors[i].y + std::cos(t * 0.44f + fi * 2.3f) * 18.0f +
                        std::sin(t * 0.27f + fi * 1.1f) * 9.0f;
        const float sx = x - camX;
        const float sy = y - camY;
        if (sx < -20.0f || sy < -20.0f || sx > static_cast<float>(p.fb().width()) + 20.0f ||
            sy > static_cast<float>(p.fb().height()) + 20.0f) {
            continue;
        }
        // 单个亮点：外圈柔光 + 内圈高光，两层加色叠出过曝的核心。
        const float blink = 0.25f + 0.75f * std::pow(
                                          0.5f + 0.5f * std::sin(t * 1.7f + fi * 2.1f), 3.0f);
        const float k = blink * night;
        p.glow(sx, sy, 9.0f + 3.0f * blink, Color(168, 236, 108), 0.34f * k);
        p.glow(sx, sy, 3.2f, Color(236, 255, 176), 0.85f * k);
    }
}

// 水面碎光：太阳（夜里是月亮）的方位投影到池塘，形成一条随时刻横移的碎光带。
// 它证明画面里的光有方向——光斑会从池塘东侧一路漂到西侧，不是画死的贴图。
void drawWaterGlint(Painter& p, const FarmWorld& world, float camX, float camY) {
    const float hour = std::fmod(world.hour() + 24.0f, 24.0f);
    const SkyState sky = skyAt(hour);
    float phase = 0.0f; // 0=东 1=西
    float intensity = 0.0f;
    Color tint = Color(255, 226, 168);
    if (sky.sunUp) {
        phase = sky.sunPhase;
        // 正午最亮、日出日落最弱；晨昏给暖橙，正午偏白。
        intensity = 0.22f + 0.55f * sky.sunElevation;
        tint = lerpColor(Color(255, 176, 96), Color(255, 240, 206), sky.sunElevation);
    } else {
        // 月亮与太阳反相：18:00 在东，06:00 在西，中途过顶。
        const float moonHour = std::fmod(hour + 6.0f, 24.0f); // 18 → 0
        phase = std::clamp(moonHour / 12.0f, 0.0f, 1.0f);
        intensity = 0.58f * std::sin(kPi * phase);
        tint = Color(186, 214, 255);
    }
    if (intensity <= 0.01f) {
        return;
    }

    // 光斑中心在地面上的横坐标：东（phase=0）→ 西（phase=1），
    // 直接按比例铺到池塘宽度的 60% 内，扫过整池而不是贴在岸边。
    // 注意 phase 是"太阳走到哪"，光斑要在它的对面一侧，所以用 (0.5 - phase)。
    const float bandX = kPondCx + (0.5f - phase) * 2.0f * kPondRx * 0.6f;
    const float t = world.animTime();

    // 每一行画两道错开的短横，整体才像"碎"光；一整条长横看着像划痕。
    for (int i = 0; i < 24; ++i) {
        const float v = static_cast<float>(i) / 23.0f;
        const float y = kPondCy + (v - 0.5f) * kPondRy * 1.74f;
        const float yn = (y - kPondCy) / kPondRy;
        if (std::fabs(yn) >= 0.97f) {
            continue;
        }
        const float halfW = kPondRx * std::sqrt(1.0f - yn * yn) * 0.92f;
        const float reach = 1.0f - std::fabs(yn) * 1.05f;
        for (int k = 0; k < 2; ++k) {
            const float fi = static_cast<float>(i) * 2.0f + static_cast<float>(k);
            const float wobble = std::sin(t * 1.3f + fi * 0.9f) * 7.0f +
                                 std::sin(t * 2.7f + fi * 2.3f) * 3.5f;
            const float lane = (static_cast<float>(k) - 0.5f) * (7.0f + 15.0f * reach);
            const float cx =
                std::clamp(bandX + wobble + lane - kPondCx, -halfW + 5.0f, halfW - 5.0f);
            const float pulse = 0.55f + 0.45f * std::sin(t * 2.1f + fi * 1.7f) *
                                            std::sin(t * 2.1f + fi * 1.7f);
            const float len = (3.0f + 13.0f * reach) * pulse + 2.0f;
            const float a = intensity * (0.15f + 0.55f * pulse) * (0.35f + 0.65f * reach);
            // 世界坐标算完再减相机：碎光只在水面上，用世界坐标判断栈道遮挡最直接。
            // 每道碎光再上下抖 2px，免得排成一条虚线。
            const float wy = y + std::sin(t * 3.1f + fi * 2.1f) * 2.0f;
            const float wx0 = kPondCx + cx - len * 0.5f;
            const float wx1 = kPondCx + cx + len * 0.5f;
            // 栈道横穿池塘西缘，碎光不落在木板上。
            if (wy >= kDockY - 4 && wy <= kDockY + kDockH + 4 && wx0 < kDockX + kDockW + 10) {
                continue;
            }
            const int yi = static_cast<int>(std::lround(wy - camY));
            const int x0 = static_cast<int>(std::lround(wx0 - camX));
            const int x1 = static_cast<int>(std::lround(wx1 - camX));
            p.fb().blendAddSpan(yi, x0, x1, tint,
                                static_cast<std::uint8_t>(std::min(150.0f, a * 255.0f)));
            // 再补一行淡的，碎光才有厚度，不是一根头发丝
            p.fb().blendAddSpan(yi + 1, x0, x1, tint,
                                static_cast<std::uint8_t>(std::min(90.0f, a * 150.0f)));
        }
    }
}

// ---------------- 小屋室内 ----------------

// 室内静态布置：墙、地板、地毯、床、储物箱、桌子、门垫。窗户玻璃的"天色"
// 由 frame() 每帧按当前时刻重画，其余全部烘进 cabinBackground_。
void drawCabinRoom(Painter& p) {
    const float W = static_cast<float>(kRoomW);
    const float H = static_cast<float>(kRoomH);

    // 墙：上墙抹泥 + 木梁，侧墙/下墙木纹板。
    p.rect(0.0f, 0.0f, W, 64.0f, Color(216, 198, 160));
    p.rect(0.0f, 0.0f, W, 6.0f, kWoodDark);
    for (int i = 0; i < 5; ++i) {
        p.rect(24.0f + static_cast<float>(i) * 176.0f, 0.0f, 10.0f, 64.0f, kWoodDark);
    }
    p.rect(0.0f, 56.0f, W, 8.0f, kWoodDark);
    p.rect(0.0f, 64.0f, 32.0f, H - 64.0f, kWoodDark);
    p.rect(W - 32.0f, 64.0f, 32.0f, H - 64.0f, kWoodDark);
    for (int i = 0; i < 4; ++i) {
        p.rect(0.0f, 64.0f + static_cast<float>(i) * 8.0f, 32.0f, 2.0f, kTrunk);
        p.rect(W - 32.0f, 64.0f + static_cast<float>(i) * 8.0f, 32.0f, 2.0f, kTrunk);
    }
    p.rect(32.0f, H - 64.0f, W - 64.0f, 64.0f, kWoodDark);
    // 底墙出门的开口（地垫就在开口里）。
    p.rect(352.0f, H - 64.0f, 96.0f, 64.0f, Color(24, 18, 14));
    p.rect(32.0f, H - 64.0f, 320.0f, 4.0f, kTrunk);
    p.rect(448.0f, H - 64.0f, 320.0f, 4.0f, kTrunk);

    // 地板：横向木条 + 板缝。
    p.rect(32.0f, 64.0f, W - 64.0f, H - 128.0f, kWood);
    for (int row = 0; row < 17; ++row) {
        const float y = 64.0f + static_cast<float>(row) * 32.0f;
        p.rect(32.0f, y + 30.0f, W - 64.0f, 2.0f, kWoodDark);
        for (int col = 0; col < 7; ++col) {
            const std::uint32_t v = hash2(row * 7 + col, row + col * 3);
            const float x = 40.0f + static_cast<float>(((v >> (col * 3)) & 0x7Fu) % 720u);
            p.rect(x, y + 4.0f + static_cast<float>((v >> 5) & 0x7u), 26.0f, 2.0f,
                   (v & 1u) ? kWoodLight : kWoodDark);
        }
    }

    // 地毯：圆角椭圆 + 菱形花纹。
    p.ellipse(400.0f, 300.0f, 130.0f, 68.0f, Color(168, 92, 70), 48);
    p.ellipse(400.0f, 300.0f, 116.0f, 58.0f, Color(196, 116, 88), 44);
    p.ellipse(400.0f, 300.0f, 84.0f, 40.0f, Color(168, 92, 70), 40);
    p.ellipse(400.0f, 300.0f, 52.0f, 24.0f, Color(222, 150, 112), 28);
    for (int i = 0; i < 6; ++i) {
        const float a = kPi * static_cast<float>(i) / 3.0f;
        p.ellipse(400.0f + std::cos(a) * 100.0f, 300.0f + std::sin(a) * 49.0f, 5.0f, 3.0f,
                  kCream, 10);
    }

    // 床：木框 + 床垫 + 白枕 + 绿被（放左半屏，右侧留给订单板 HUD）。
    const float bx = 4 * kCell;
    const float by = 3 * kCell;
    const float bw = 4 * kCell;
    const float bh = 6 * kCell;
    p.rect(bx + 5.0f, by + 7.0f, bw, bh, Color(40, 28, 18));
    p.rect(bx, by, bw, bh, kWoodDark);
    p.rect(bx + 5.0f, by + 5.0f, bw - 10.0f, bh - 10.0f, kCream);
    p.rect(bx + 9.0f, by + 9.0f, bw - 18.0f, 30.0f, kWhite);
    p.ellipse(bx + bw * 0.5f, by + 26.0f, bw * 0.28f, 11.0f, Color(240, 240, 230), 20);
    p.rect(bx + 5.0f, by + 46.0f, bw - 10.0f, bh - 56.0f, kLeaf);
    p.rect(bx + 5.0f, by + 46.0f, bw - 10.0f, 6.0f, kLeafLight);
    p.line(bx + 5.0f, by + 96.0f, bx + bw - 5.0f, by + 96.0f, kLeafDark, 2.0f);
    p.line(bx + 5.0f, by + 128.0f, bx + bw - 5.0f, by + 128.0f, kLeafDark, 2.0f);
    for (int i = 0; i < 3; ++i) {
        p.rect(bx + 5.0f, by + 56.0f + static_cast<float>(i) * 44.0f, bw - 10.0f, 2.0f,
               kLeafDark);
    }
    p.rect(bx, by + bh - 8.0f, bw, 8.0f, kTrunk);

    // 储物箱：两只木箱摞一只（右上角，在订单板下方仍露出一截）。
    p.rect(608.0f, 100.0f, 88.0f, 78.0f, kOutline);
    p.rect(612.0f, 104.0f, 80.0f, 70.0f, kWood);
    p.line(612.0f, 104.0f, 692.0f, 174.0f, kWoodDark, 3.0f);
    p.line(692.0f, 104.0f, 612.0f, 174.0f, kWoodDark, 3.0f);
    p.rect(612.0f, 136.0f, 80.0f, 5.0f, kWoodDark);
    p.rect(628.0f, 180.0f, 72.0f, 62.0f, kOutline);
    p.rect(632.0f, 184.0f, 64.0f, 54.0f, kWoodLight);
    p.rect(632.0f, 184.0f, 64.0f, 6.0f, kWoodDark);

    // 桌子 + 蜡烛 + 书。
    p.ellipse(304.0f, 120.0f, 52.0f, 20.0f, Color(40, 28, 18), 24);
    p.ellipse(300.0f, 114.0f, 52.0f, 20.0f, kWoodDark, 24);
    p.ellipse(300.0f, 112.0f, 44.0f, 15.0f, kWood, 24);
    p.rect(296.0f, 92.0f, 8.0f, 18.0f, kCream);
    p.line(300.0f, 92.0f, 300.0f, 84.0f, kFlowerYellow, 2.0f);
    p.ellipse(300.0f, 82.0f, 2.5f, 4.0f, Color(255, 216, 120), 10);
    p.rect(318.0f, 100.0f, 26.0f, 8.0f, Color(148, 62, 52));
    p.rect(318.0f, 100.0f, 26.0f, 2.0f, kCream);

    // 窗框：玻璃区域 (198,14)-(282,50)，天色由 frame() 每帧画。
    p.rect(192.0f, 8.0f, 96.0f, 48.0f, kWoodDark);
    p.rect(198.0f, 14.0f, 84.0f, 36.0f, Color(30, 40, 70));
    p.line(240.0f, 14.0f, 240.0f, 50.0f, kWoodDark, 3.0f);
    p.line(198.0f, 32.0f, 282.0f, 32.0f, kWoodDark, 3.0f);

    // 门垫：编织纹 + 下箭头。
    p.rect(352.0f, 512.0f, 96.0f, 32.0f, kOutline);
    p.rect(356.0f, 516.0f, 88.0f, 24.0f, kSandDark);
    for (int i = 0; i < 4; ++i) {
        p.rect(356.0f, 518.0f + static_cast<float>(i) * 6.0f, 88.0f, 2.0f, kSand);
    }
    p.polygon({{388.0f, 530.0f}, {412.0f, 530.0f}, {400.0f, 542.0f}}, kCream, kOutline,
              1.0f);
}

// ---------------- 时钟 HUD ----------------

// ---------------- 时间轴 HUD ----------------
// 顶部中央：一行时刻文字 + 24h 轨道 + 可拖拽旋钮。
// 轨道底色直接由 skyAt/ambientTintAt 采样生成，和场景里的光照共用同一份模型，
// 所以条上的"天亮/黄昏/入夜"和画面是同步的，不是另画一套配色。

void drawTimeBar(Painter& p, const FarmWorld& world, int viewW) {
    const RectI panel = timeBarRect(viewW);
    const RectI track = timeTrackRect(viewW);
    const float px = static_cast<float>(panel.x);
    const float py = static_cast<float>(panel.y);

    // 木牌底：与左上标题牌、订单板同一套木色。
    p.rect(px + 4, py + 5, panel.w, panel.h, kBorderGrass);
    p.rect(px, py, panel.w, panel.h, kOutline);
    p.rect(px + 2, py + 2, panel.w - 4, panel.h - 4, kWood);
    p.rect(px + 6, py + 6, panel.w - 12, 2.0f, kWoodLight);

    // 日照弧：6:00 到 18:00 的太阳轨迹，用中点圆弧的角度筛选画一段浅弧。
    // 圆心放在轨道下方很远的位置，半径大、弧就平；两端的角度由半宽反算。
    {
        constexpr float kArcR = 520.0f;
        const float halfW = static_cast<float>(track.w) / 2.0f;
        const float dy = kArcR - std::sqrt(kArcR * kArcR - halfW * halfW); // 弧顶抬高量
        const int cx = track.x + track.w / 2;
        const int cy = static_cast<int>(std::lround(track.y + kArcR - dy));
        const float span = std::asin(halfW / kArcR) * 180.0f / kPi;
        raster::drawArc(p.fb(), {cx, cy}, static_cast<int>(kArcR), 270.0f - span,
                        270.0f + span, Color(246, 214, 142), {});
    }

    // 24 段轨道：每段半小时到一小时，颜色取该时段中点的天光与环境色。
    const float segW = static_cast<float>(track.w) / 24.0f;
    for (int i = 0; i < 24; ++i) {
        const float h = static_cast<float>(i) + 0.5f;
        const SkyState sky = skyAt(h);
        AmbientTint warm;
        AmbientTint blue;
        ambientTintAt(h, blue, warm);
        Color c = sky.sunUp ? lerpColor(Color(70, 96, 128), Color(150, 196, 228),
                                       0.35f + 0.65f * sky.sunElevation)
                            : Color(30, 40, 66);
        if (warm.alpha > 0) {
            c = lerpColor(c, Color(warm.r, warm.g, warm.b),
                          static_cast<float>(warm.alpha) / 320.0f);
        }
        if (sky.lampIntensity > 0.0f && !sky.sunUp) {
            c = lerpColor(c, Color(96, 78, 44), 0.25f * sky.lampIntensity);
        }
        const int x0 = static_cast<int>(std::lround(static_cast<float>(track.x) + segW * i));
        const int x1 = static_cast<int>(std::lround(static_cast<float>(track.x) + segW * (i + 1))) - 1;
        p.rect(static_cast<float>(x0), static_cast<float>(track.y),
               static_cast<float>(x1 - x0 + 1), static_cast<float>(track.h), c);
    }
    // 轨道外框与整点刻度（0/6/12/18/24）。
    p.rectOutline(static_cast<float>(track.x) - 1.0f, static_cast<float>(track.y) - 1.0f,
                  static_cast<float>(track.w) + 1.0f, static_cast<float>(track.h) + 1.0f,
                  kOutline, 1.0f);
    for (int i = 0; i <= 24; i += 6) {
        const float tx = static_cast<float>(track.x) + segW * static_cast<float>(i);
        p.line(tx, static_cast<float>(track.y + track.h), tx,
               static_cast<float>(track.y + track.h + 4), kCreamShade, 1.0f);
    }

    // 旋钮：中点圆画环 + 实心圆盘，盘里太阳/月牙随时刻切换。
    const float hour = world.hour();
    const float kx = trackXFromTime(hour, viewW);
    const float ky = static_cast<float>(track.y) + static_cast<float>(track.h) / 2.0f;
    const SkyState sky = skyAt(hour);
    raster::drawCircle(p.fb(), {static_cast<int>(std::lround(kx)),
                                static_cast<int>(std::lround(ky))},
                       9, kOutline, {});
    p.ellipse(kx, ky, 7.5f, 7.5f, world.timeScrubbing() ? kWhite : kCream, 18);
    if (sky.sunUp) {
        p.ellipse(kx, ky, 4.2f, 4.2f, Color(248, 186, 62), 16);
        p.ellipse(kx - 1.2f, ky - 1.4f, 1.3f, 1.3f, Color(255, 236, 178), 10);
    } else {
        p.ellipse(kx - 0.8f, ky, 4.0f, 4.0f, Color(226, 232, 246), 16);
        p.ellipse(kx + 2.6f, ky - 1.2f, 3.6f, 3.6f, kCream, 14);
    }

    // 时刻文字：天 + 24 小时制。
    const float minutesF = hour * 60.0f;
    char buf[40];
    std::snprintf(buf, sizeof(buf), "第 %d 天   %02d:%02d", world.day(),
                  static_cast<int>(hour), static_cast<int>(minutesF) % 60);
    const int tw = text::textWidth(buf);
    text::drawText(p.fb(), panel.x + (panel.w - tw) / 2, panel.y + 6, buf, kCream);
}

// ---------------- 超采样降采样 ----------------

void downsample2x(const FrameBuffer& src, FrameBuffer& dst) {
    dst.resize(src.width() / 2, src.height() / 2);
    for (int y = 0; y < dst.height(); ++y) {
        for (int x = 0; x < dst.width(); ++x) {
            const Color a = src.pixel(x * 2, y * 2);
            const Color b = src.pixel(x * 2 + 1, y * 2);
            const Color c = src.pixel(x * 2, y * 2 + 1);
            const Color d = src.pixel(x * 2 + 1, y * 2 + 1);
            dst.putPixel(x, y,
                         Color{static_cast<std::uint8_t>((a.r + b.r + c.r + d.r + 2) / 4),
                               static_cast<std::uint8_t>((a.g + b.g + c.g + d.g + 2) / 4),
                               static_cast<std::uint8_t>((a.b + b.b + c.b + d.b + 2) / 4)});
        }
    }
}

// 色键画布 → 1× 覆盖度遮罩：每个 2×2 块里非键色子像素的比例（0..255）。
// 用于生成"立体物件遮罩"——影子合成时跳过这些像素，影子就压不到树冠上。
void downsampleMask(const FrameBuffer& src, const Color& key, std::uint8_t* dst, int dstW,
                    int dstX, int dstY, int w, int h) {
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int covered = 0;
            for (int sy = 0; sy < 2; ++sy) {
                for (int sx = 0; sx < 2; ++sx) {
                    if (src.pixel(x * 2 + sx, y * 2 + sy) != key) {
                        ++covered;
                    }
                }
            }
            dst[static_cast<std::size_t>(dstY + y) * static_cast<std::size_t>(dstW) +
                static_cast<std::size_t>(dstX + x)] =
                static_cast<std::uint8_t>(covered * 255 / 4);
        }
    }
}

// 窗户玻璃的天色：白天淡蓝、夜里深蓝，晨昏透一点暖——屋里也能看出外面的时段。
void drawWindowSky(Painter& p, const FarmWorld& world, float camX, float camY) {
    AmbientTint blue;
    AmbientTint warm;
    ambientTintAt(world.hour(), blue, warm);
    const float night = static_cast<float>(blue.alpha) / 108.0f;
    const float dusk = static_cast<float>(warm.alpha) / 46.0f;
    Color sky = lerpColor(Color(150, 200, 236), Color(18, 26, 58), night);
    sky = lerpColor(sky, Color(250, 170, 110), dusk * 0.55f);
    const float x = 198.0f - camX;
    const float y = 14.0f - camY;
    p.rect(x, y, 84.0f, 36.0f, sky);
    // 白天窗外挂个太阳光斑，夜里换两颗星。
    if (night > 0.6f) {
        p.ellipse(x + 18.0f, y + 9.0f, 1.2f, 1.2f, kCream, 8);
        p.ellipse(x + 62.0f, y + 24.0f, 1.0f, 1.0f, kCream, 8);
    } else {
        p.ellipse(x + 60.0f, y + 10.0f, 6.0f, 6.0f, Color(255, 244, 200), 12);
    }
}

} // namespace

FarmRender::FarmRender()
    : background_(kWorldW, kWorldH), cabinBackground_(1, 1), farmLayer_(1, 1),
      shadowMask_(1, 1), dragonMask_(1, 1) {}

void FarmRender::build(const FarmWorld& world) {
    // 静态层：草地/土路/池塘/花草/树木/小屋外观，一次烘成。
    FrameBuffer hi(kWorldW * 2, kWorldH * 2);
    Painter p(hi, 2.0f);
    drawGround(p);
    drawPath(p);
    drawPond(p);
    drawMeadow(p);
    drawProps(p);
    drawTrees(p);
    downsample2x(hi, background_);

    // 立体物件遮罩：把树/灌木/木桶/路灯/小屋/桥头灯单独画到色键画布上，
    // 降采样成 1× 覆盖度。影子合成时跳过被遮的像素，影子就永远压在
    // 树冠、桶身、墙面之下，而不是浮在它们上面。
    // 复用刚才的背景 2× 画布，省一块 28MB 的临时缓冲。
    hi.clear(kMaskKey);
    Painter mp(hi, 2.0f);
    drawProps(mp);
    drawTrees(mp);
    drawDockLamp(mp);
    propMask_.assign(static_cast<std::size_t>(kWorldW) * kWorldH, 0);
    downsampleMask(hi, kMaskKey, propMask_.data(), kWorldW, 0, 0, kWorldW, kWorldH);

    // 室内：整间房一次烘成（窗户玻璃天色每帧另画）。
    FrameBuffer chi(kRoomW * 2, kRoomH * 2);
    Painter cp(chi, 2.0f);
    drawCabinRoom(cp);
    downsample2x(chi, cabinBackground_);

    bakeFarmLayer(world);
}

void FarmRender::bakeFarmLayer(const FarmWorld& world) {
    // 农田层：土台附近的小矩形里重画 草地+土路+农田（与整图同序），再盖回大背景。
    // 注：AET 增量累加的交点 x 依赖画布绝对坐标，局部重烘与整图烘培在多边形
    // 边缘会有 ±1 子像素的翻转（降采样后 ≤2/255），属可接受的固有差异。
    farmBakeRect_ = {kFarmBakeX, kFarmBakeY, kFarmBakeW, kFarmBakeH};
    FrameBuffer hi(kFarmBakeW * 2, kFarmBakeH * 2);
    Painter p(hi, 2.0f, {static_cast<float>(kFarmBakeX), static_cast<float>(kFarmBakeY)});
    drawGround(p);
    drawPath(p);
    drawFarm(p, world);
    downsample2x(hi, farmLayer_);
    background_.copyRectFrom(farmLayer_, 0, 0, kFarmBakeW, kFarmBakeH, kFarmBakeX,
                             kFarmBakeY);
    bakedFarmVersion_ = world.farmVersion();
}

void FarmRender::frame(FrameBuffer& fb, const FarmWorld& world, bool hud) {
    // 农田版本号落后（播种/浇水/生长进阶/收获）时局部重烘农田层。
    if (world.farmVersion() != bakedFarmVersion_) {
        bakeFarmLayer(world);
    }
    fb.clear(kBorderGrass);
    const int camX = static_cast<int>(std::lround(world.camera().x));
    const int camY = static_cast<int>(std::lround(world.camera().y));
    Painter p(fb, 1.0f);

    if (world.scene() == GameScene::Cabin) {
        // 室内：拷房间缓存，窗户天色随时刻变，不画影子与路灯光。
        fb.copyRectFrom(cabinBackground_, camX, camY, fb.width(), fb.height(), 0, 0);
        drawWindowSky(p, world, static_cast<float>(camX), static_cast<float>(camY));
        drawHover(p, world.hover(), static_cast<float>(camX), static_cast<float>(camY),
                  world.animTime());
        drawDragon(p,
                   {world.playerPos().x - static_cast<float>(camX),
                    world.playerPos().y - static_cast<float>(camY)},
                   world.moving(), world.walkPhase(), world.animTime(), world.facing());
        drawParticles(p, world, static_cast<float>(camX), static_cast<float>(camY));
        applyAmbient(fb, world.hour(), 0.72f);
    } else {
        fb.copyRectFrom(background_, camX, camY, fb.width(), fb.height(), 0, 0);
        const SkyState sky = skyAt(world.hour());

        // 锦鲤在背景之上、水纹之下：鱼在水里，水面的反光和波浪盖在它上面。
        drawKoi(p, world, static_cast<float>(camX), static_cast<float>(camY));

        // 水面波纹与炊烟：环境动画，画在背景之上、实体之下。
        drawPondShimmer(p, world, static_cast<float>(camX), static_cast<float>(camY));
        drawWaterGlint(p, world, static_cast<float>(camX), static_cast<float>(camY));
        // 荷叶浮在水面：压在锦鲤与水纹之上，再由下面的栈道盖回桥面。
        drawLilyPads(p, static_cast<float>(camX), static_cast<float>(camY));
        // 栈道是水面之上的实体：鱼和水纹画完后，把烘焙背景里的桥面按相机
        // 位置原样盖回来——锦鲤从桥下游过时被桥板挡住，而不是浮在桥面上。
        fb.copyRectFrom(background_, kDockX, kDockY, kDockW, kDockH, kDockX - camX,
                        kDockY - camY);
        // 桥头小灯的灯柱伸到水面上方，灯头这块也一并盖回（±8×10 的包围盒）。
        fb.copyRectFrom(background_, kDockLampX - 8, kDockLampY - 10, 16,
                        kDockY - (kDockLampY - 10), kDockLampX - 8 - camX,
                        kDockLampY - 10 - camY);

        // 影子层：所有投影先画进离屏蒙版，再一次性合成到背景上。合成时跳过
        // 立体物件遮罩覆盖的像素，影子就压不到树冠、桶身、墙面上；随后画的
        // 实体（角色/鱼漂/粒子）也自然盖在影子之上。
        if (shadowMask_.width() != fb.width() || shadowMask_.height() != fb.height()) {
            shadowMask_.resize(fb.width(), fb.height());
        }
        drawShadows(shadowMask_, world, static_cast<float>(camX), static_cast<float>(camY),
                    sky, dragonMask_);
        compositeShadows(fb, shadowMask_, propMask_, camX, camY);

        // 炊烟比地面高，画在影子之后。
        drawChimneySmoke(p, world, static_cast<float>(camX), static_cast<float>(camY));

        drawHover(p, world.hover(), static_cast<float>(camX), static_cast<float>(camY),
                  world.animTime());
        drawFishing(p, world, static_cast<float>(camX), static_cast<float>(camY));
        drawDragon(p,
                   {world.playerPos().x - static_cast<float>(camX),
                    world.playerPos().y - static_cast<float>(camY)},
                   world.moving(), world.walkPhase(), world.animTime(), world.facing());
        drawBiteBubble(p, world, static_cast<float>(camX), static_cast<float>(camY));
        drawParticles(p, world, static_cast<float>(camX), static_cast<float>(camY));

        // 夜色/晨昏环境色压在场景与角色上，再由路灯光晕在夜里"点亮"灯周。
        applyAmbient(fb, world.hour());
        drawLampGlow(p, world, static_cast<float>(camX), static_cast<float>(camY), sky);
        // 萤火虫是自发光，要压在环境色之后，否则会被夜色一起压暗。
        drawFireflies(p, world, static_cast<float>(camX), static_cast<float>(camY));
    }

    if (hud) {
        drawHud(p, world, fb.width(), fb.height());
        drawTimeBar(p, world, fb.width());
    }

    // 转场渐变压在最上层：0.5s 渐黑（中点切场景/跳时间）+ 0.5s 渐亮。
    const float fade = world.fadeAlpha();
    if (fade > 0.0f) {
        fb.blendRect(0, 0, fb.width() - 1, fb.height() - 1, Color(8, 8, 10),
                     static_cast<std::uint8_t>(std::clamp(fade, 0.0f, 1.0f) * 255.0f));
    }
}

FrameBuffer FarmRender::captureWorld(const FarmWorld& world) const {
    FrameBuffer hi(kWorldW * 2, kWorldH * 2);
    Painter p(hi, 2.0f);
    drawScene(p, world);
    drawChimneySmoke(p, world, 0.0f, 0.0f); // 整图导出也带上固定相位的炊烟
    drawKoi(p, world, 0.0f, 0.0f); // 整图导出也带上锦鲤（固定相位）
    drawLilyPads(p, 0.0f, 0.0f);   // 荷叶压在鱼上，与运行时同序
    drawDock(p);                   // 桥面盖回鱼上，和运行时图层一致
    drawDragon(p, world.playerPos(), false, 0.0f, 0.0f, world.facing());
    FrameBuffer out(kWorldW, kWorldH);
    downsample2x(hi, out);
    return out;
}

FrameBuffer FarmRender::captureScene(const FarmWorld& world) const {
    // 与整图 drawScene 完全同序，用于校验"静态层+农田层"分炉烘培的一致性。
    FrameBuffer hi(kWorldW * 2, kWorldH * 2);
    Painter p(hi, 2.0f);
    drawScene(p, world);
    FrameBuffer out(kWorldW, kWorldH);
    downsample2x(hi, out);
    return out;
}

FrameBuffer FarmRender::captureBackground() const {
    return background_;
}

std::vector<FrameBuffer> FarmRender::captureLayers(const FarmWorld& world) const {
    FrameBuffer hi(kWorldW * 2, kWorldH * 2);
    Painter p(hi, 2.0f);
    std::vector<FrameBuffer> layers;
    layers.reserve(6);
    auto snapshot = [&]() {
        FrameBuffer out(kWorldW, kWorldH);
        downsample2x(hi, out);
        layers.push_back(std::move(out));
    };

    drawGround(p);
    snapshot();
    drawPath(p);
    snapshot();
    drawFarm(p, world);
    snapshot();
    drawPond(p);
    snapshot();
    drawMeadow(p);
    drawProps(p);
    snapshot();
    drawTrees(p);
    snapshot();
    return layers;
}

FrameBuffer FarmRender::captureDragonSheet() const {
    constexpr int kCols = 6;
    constexpr int kCellW = 190;
    constexpr int kCellH = 210;
    constexpr int kHead = 42;
    FrameBuffer fb(kCols * kCellW, kHead + 2 * kCellH);
    Painter p(fb, 1.0f);

    p.rect(0.0f, 0.0f, static_cast<float>(fb.width()), static_cast<float>(fb.height()), kGrass);
    p.rect(0.0f, 0.0f, static_cast<float>(fb.width()), static_cast<float>(kHead), kBorderGrass);
    text::drawText(fb, 14, 12, "胖龙农场 · 角色造型候选（上排站立，下排走路）", kCream);

    const char* names[] = {"1 奶蛋", "2 奶龙", "3 奶鼠", "4 奶娃", "5 奶兔", "6 奶猫"};
    for (int i = 0; i < kCols; ++i) {
        const float cx = static_cast<float>(i * kCellW + kCellW / 2);
        if ((i & 1) != 0) {
            p.rect(static_cast<float>(i * kCellW), static_cast<float>(kHead),
                   static_cast<float>(kCellW), static_cast<float>(2 * kCellH), kGrassDark);
        }
        p.line(static_cast<float>(i * kCellW), static_cast<float>(kHead),
               static_cast<float>(i * kCellW), static_cast<float>(fb.height()), kBorderGrass,
               1.0f);
        const int tw = text::textWidth(names[i]);
        text::drawText(fb, i * kCellW + (kCellW - tw) / 2, kHead + 10, names[i], kCream);
        // 站立：相位 0；走路：把步态相位拨到抬脚的中段，两只脚一高一低看得出来。
        drawDragonVariant(p, i + 1, {cx, static_cast<float>(kHead + 176)}, false, 0.0f, 0.6f,
                          1);
        drawDragonVariant(p, i + 1,
                          {cx, static_cast<float>(kHead + kCellH + 176)}, true, 0.25f, 0.6f,
                          1);
    }
    return fb;
}

} // namespace cg::game
