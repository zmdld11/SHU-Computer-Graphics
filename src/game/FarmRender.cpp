// FarmRender.cpp
// 实现农场场景的纯矢量绘制。地图先画进 2× 画布再 2×2 均值降采样，
// 让中点线/扫描线填充的边缘在最终 1× 图上保持干净；每帧只做一次窗口拷贝。
#include "game/FarmRender.hpp"

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
    FrameBuffer& fb() const { return fb_; }
    Color sample(float x, float y) const { return fb_.pixel(px(x), py(y)); }

    void rect(float x, float y, float w, float h, const Color& c) {
        if (w <= 0.0f || h <= 0.0f) {
            return;
        }
        fb_.fillRect(px(x), py(y), px(x + w) - 1, py(y + h) - 1, c);
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

    void ellipse(float cx, float cy, float rx, float ry, const Color& c, int segments = 40) {
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
        raster::scanlineFillPolygon(fb_, pts, c);
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
                 float width) {
        if (pts.size() < 3) {
            return;
        }
        std::vector<IPoint> ip;
        ip.reserve(pts.size());
        for (const FPoint& p : pts) {
            ip.push_back({px(p.x), py(p.y)});
        }
        raster::scanlineFillPolygon(fb_, ip, fill);
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

void drawFishShadow(Painter& p, float x, float y, float scale) {
    p.ellipse(x, y, 13.0f * scale, 5.5f * scale, kFish, 18);
    const std::vector<FPoint> tail = {{x + 10.0f * scale, y},
                                      {x + 18.0f * scale, y - 5.0f * scale},
                                      {x + 18.0f * scale, y + 5.0f * scale}};
    p.polygon(tail, kFish, kFish, 0.0f);
    p.ellipse(x - 5.0f * scale, y - 1.0f * scale, 4.0f * scale, 2.2f * scale, kFishLight,
              12);
    p.ellipse(x - 10.0f * scale, y - 1.0f * scale, 1.2f * scale, 1.2f * scale, kOutline, 8);
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

    // 栈道尽头的小灯：暖色一点，给池塘一个视觉落点。
    p.line(1014.0f, 626.0f, 1014.0f, 598.0f, kWoodDark, 4.0f);
    p.ellipse(1014.0f, 593.0f, 7.5f, 8.5f, Color(58, 42, 28), 18);
    p.ellipse(1014.0f, 593.0f, 5.3f, 6.0f, Color(250, 204, 90), 18);
    p.ellipse(1012.5f, 590.5f, 1.8f, 2.2f, kCream, 10);
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

    drawFishShadow(p, cx - 62.0f, cy + 62.0f, 1.0f);
    drawFishShadow(p, cx + 92.0f, cy + 28.0f, 0.8f);

    drawLilyPad(p, cx - 86.0f, cy - 46.0f, 15.0f, kWaterMid, false);
    drawLilyPad(p, cx + 52.0f, cy - 92.0f, 13.0f, kWaterLight, false);
    drawLilyPad(p, cx + 112.0f, cy - 48.0f, 14.0f, kWaterLight, true);
    drawLilyPad(p, cx - 38.0f, cy + 118.0f, 12.0f, kWaterMid, false);
    drawLilyPad(p, cx + 82.0f, cy + 126.0f, 14.0f, kWaterMid, false);
    drawLilyPad(p, cx + 136.0f, cy + 62.0f, 11.0f, kWaterMid, false);

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

bool meadowBlocked(float x, float y) {
    // 农田动态层重烘范围（含外圈缓冲）内不放静态装饰：这块区域会被局部
    // 重烘整片覆盖，跨界的装饰会在重烘后被凭空截断。
    return nearFarmBakeRect(x, y) || inFarmApron(x, y) || inPathArea(x, y) ||
           inPond(x, y, 28.0f) || inDock(x, y);
}

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
}

void drawTrees(Painter& p) {
    struct Tree {
        float x;
        float y;
        float scale;
        bool variant;
    };
    const Tree trees[] = {
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

    std::vector<Tree> sorted(std::begin(trees), std::end(trees));
    std::sort(sorted.begin(), sorted.end(),
              [](const Tree& a, const Tree& b) { return a.y < b.y; });
    for (const Tree& t : sorted) {
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
    drawTrees(p);
}

// ---------------- 胖龙 ----------------

void drawDragon(Painter& p, FPoint pos, bool moving, float walkPhase, float time,
                int facing) {
    const float bob = moving
                          ? -std::fabs(std::sin(walkPhase * 2.0f * kPi)) * 2.5f
                          : std::sin(time * 2.1f) * 1.0f;
    const float x = pos.x;
    const float y = pos.y + bob;
    const float f = facing < 0 ? -1.0f : 1.0f;
    auto X = [x, f](float dx) { return x + dx * f; };

    const Color ground = p.sample(pos.x, pos.y);
    p.ellipse(pos.x, pos.y + 1.0f, 23.0f, 6.5f, darken(ground, 0.72f), 24);

    // 尾巴：从身体左侧伸出去，末端一个小奶油色尾尖。
    p.ellipseOutline(X(-22.0f), y - 15.0f, 10.0f, 8.0f, kDragonShade, kOutline, 1.0f, 20);
    p.ellipseOutline(X(-34.0f), y - 11.0f, 8.0f, 7.0f, kDragonShade, kOutline, 1.0f, 18);
    p.ellipseOutline(X(-45.0f), y - 15.0f, 7.0f, 6.0f, kDragon, kOutline, 1.0f, 16);
    const std::vector<FPoint> tailTip = {{X(-50.0f), y - 14.0f},
                                         {X(-58.0f), y - 21.0f},
                                         {X(-63.0f), y - 12.0f},
                                         {X(-54.0f), y - 6.0f}};
    p.polygon(tailTip, kCreamShade, kOutline, 1.0f);

    // 翅膀（在身体后面）：比身体宽一圈，远看也能认出是龙。
    for (float s : {-1.0f, 1.0f}) {
        const std::vector<FPoint> wing = {{X(s * 15.0f), y - 38.0f},
                                          {X(s * 32.0f), y - 57.0f},
                                          {X(s * 41.0f), y - 43.0f},
                                          {X(s * 31.0f), y - 29.0f},
                                          {X(s * 18.0f), y - 32.0f}};
        p.polygon(wing, kDragonShade, kOutline, 1.0f);
        p.line(X(s * 17.0f), y - 36.0f, X(s * 38.0f), y - 43.0f, kDragon, 1.0f);
        p.line(X(s * 19.0f), y - 33.0f, X(s * 31.0f), y - 30.0f, kDragon, 1.0f);
    }

    // 脚（在身体下面，先画）
    p.ellipseOutline(X(-11.0f), y - 3.0f, 9.0f, 5.5f, kDragon, kOutline, 1.2f, 20);
    p.ellipseOutline(X(11.0f), y - 3.0f, 9.0f, 5.5f, kDragon, kOutline, 1.2f, 20);
    p.ellipse(X(-13.0f), y - 1.5f, 2.2f, 1.8f, kCream, 10);
    p.ellipse(X(-8.0f), y - 1.5f, 2.2f, 1.8f, kCream, 10);
    p.ellipse(X(9.0f), y - 1.5f, 2.2f, 1.8f, kCream, 10);
    p.ellipse(X(14.0f), y - 1.5f, 2.2f, 1.8f, kCream, 10);

    // 身体与肚皮
    p.ellipseOutline(x, y - 26.0f, 20.0f, 23.0f, kDragon, kOutline, 2.0f, 40);
    p.ellipseOutline(x, y - 20.0f, 13.0f, 15.0f, kCream, kCreamShade, 1.0f, 28);

    // 手
    p.ellipseOutline(X(-19.0f), y - 30.0f, 6.5f, 10.0f, kDragon, kOutline, 1.2f, 20);
    p.ellipseOutline(X(19.0f), y - 30.0f, 6.5f, 10.0f, kDragon, kOutline, 1.2f, 20);
    p.ellipse(X(-20.0f), y - 24.0f, 2.0f, 1.6f, kCream, 10);
    p.ellipse(X(20.0f), y - 24.0f, 2.0f, 1.6f, kCream, 10);

    // 头
    p.ellipseOutline(x, y - 49.0f, 18.0f, 16.0f, kDragon, kOutline, 2.0f, 36);

    // 角
    for (float s : {-1.0f, 1.0f}) {
        const std::vector<FPoint> horn = {{X(s * 13.0f), y - 61.0f},
                                          {X(s * 6.0f), y - 64.0f},
                                          {X(s * 9.0f), y - 74.0f}};
        p.polygon(horn, kCreamShade, kOutline, 1.0f);
    }

    // 眼睛（轻微对眼，表情更呆）
    p.ellipseOutline(X(-7.0f), y - 51.0f, 5.5f, 6.5f, kWhite, kOutline, 1.0f, 24);
    p.ellipseOutline(X(7.0f), y - 51.0f, 5.5f, 6.5f, kWhite, kOutline, 1.0f, 24);
    p.ellipse(X(-6.0f), y - 50.0f, 2.5f, 2.8f, Color(32, 24, 20), 16);
    p.ellipse(X(8.0f), y - 50.0f, 2.5f, 2.8f, Color(32, 24, 20), 16);
    p.ellipse(X(-7.0f), y - 52.0f, 1.0f, 1.0f, kWhite, 10);
    p.ellipse(X(7.0f), y - 52.0f, 1.0f, 1.0f, kWhite, 10);

    // 腮红与口鼻
    p.ellipse(X(-13.0f), y - 44.0f, 4.0f, 2.8f, kCheek, 16);
    p.ellipse(X(13.0f), y - 44.0f, 4.0f, 2.8f, kCheek, 16);
    p.ellipseOutline(x, y - 43.0f, 9.0f, 5.5f, kCream, kCreamShade, 1.0f, 24);
    p.ellipse(X(-3.0f), y - 44.0f, 1.2f, 1.2f, Color(70, 48, 32), 10);
    p.ellipse(X(3.0f), y - 44.0f, 1.2f, 1.2f, Color(70, 48, 32), 10);
    p.bezier({X(-4.0f), y - 40.0f}, {x, y - 37.0f}, {X(4.0f), y - 40.0f}, kOutline, 1.2f,
             10);
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

    if (h.kind == HoverKind::Pond) {
        const float bob = std::sin(time * 3.0f) * 2.0f;
        drawHoverFish(p, x + static_cast<float>(kCell) * 0.5f, y - 8.0f + bob);
    } else {
        p.ellipse(x + static_cast<float>(kCell) * 0.5f,
                  y + static_cast<float>(kCell) * 0.5f, 2.5f, 2.5f, kCream, 12);
    }
}

// ---------------- HUD ----------------

void drawOrderBoard(Painter& p, const FarmWorld& world, int viewW);
void drawWonPanel(Painter& p, const FarmWorld& world, int viewW, int viewH);

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

// ---------------- 订单板与胜利面板 ----------------

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

    const int total = static_cast<int>(world.playSeconds());
    char t[16];
    std::snprintf(t, sizeof(t), "%02d:%02d", total / 60, total % 60);
    text::drawText(p.fb(), b.x + 12, y + 2, t, kCream);
}

void drawWonPanel(Painter& p, const FarmWorld& world, int viewW, int viewH) {
    const int w = 400;
    const int h = 180;
    const int x = viewW / 2 - w / 2;
    const int y = viewH / 2 - h / 2 - 30;
    p.rect(static_cast<float>(x + 6), static_cast<float>(y + 8), static_cast<float>(w),
           static_cast<float>(h), Color(40, 54, 34));
    p.rect(static_cast<float>(x), static_cast<float>(y), static_cast<float>(w),
           static_cast<float>(h), kOutline);
    p.rect(static_cast<float>(x + 3), static_cast<float>(y + 3),
           static_cast<float>(w - 6), static_cast<float>(h - 6), kWood);
    p.rect(static_cast<float>(x + 8), static_cast<float>(y + 8),
           static_cast<float>(w - 16), static_cast<float>(h - 16),
           Color(238, 226, 196));

    const char* title = "订单完成！";
    text::drawText(p.fb(), x + (w - text::textWidth(title)) / 2, y + 30, title, kOutline);
    const int total = static_cast<int>(world.playSeconds());
    char t[32];
    std::snprintf(t, sizeof(t), "用时 %02d:%02d", total / 60, total % 60);
    text::drawText(p.fb(), x + (w - text::textWidth(t)) / 2, y + 82, t,
                   Color(96, 72, 44));
    const char* hint = "按 R 重开";
    text::drawText(p.fb(), x + (w - text::textWidth(hint)) / 2, y + 122, hint,
                   Color(96, 72, 44));
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

} // namespace

FarmRender::FarmRender() : background_(kWorldW, kWorldH), farmLayer_(1, 1) {}

void FarmRender::build(const FarmWorld& world) {
    // 静态层：草地/土路/池塘/花草/树木，一次烘成。
    FrameBuffer hi(kWorldW * 2, kWorldH * 2);
    Painter p(hi, 2.0f);
    drawGround(p);
    drawPath(p);
    drawPond(p);
    drawMeadow(p);
    drawTrees(p);
    downsample2x(hi, background_);
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
    fb.copyRectFrom(background_, camX, camY, fb.width(), fb.height(), 0, 0);

    Painter p(fb, 1.0f);
    drawHover(p, world.hover(), static_cast<float>(camX), static_cast<float>(camY),
              world.animTime());
    drawFishing(p, world, static_cast<float>(camX), static_cast<float>(camY));
    drawDragon(p,
               {world.playerPos().x - static_cast<float>(camX),
                world.playerPos().y - static_cast<float>(camY)},
               world.moving(), world.walkPhase(), world.animTime(), world.facing());
    drawBiteBubble(p, world, static_cast<float>(camX), static_cast<float>(camY));
    drawParticles(p, world, static_cast<float>(camX), static_cast<float>(camY));
    if (hud) {
        drawHud(p, world, fb.width(), fb.height());
        if (world.won()) {
            drawWonPanel(p, world, fb.width(), fb.height());
        }
    }
}

FrameBuffer FarmRender::captureWorld(const FarmWorld& world) const {
    FrameBuffer hi(kWorldW * 2, kWorldH * 2);
    Painter p(hi, 2.0f);
    drawScene(p, world);
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
    snapshot();
    drawTrees(p);
    snapshot();
    return layers;
}

} // namespace cg::game
