// Text.cpp — 系统字体栅格化：GDI GetGlyphOutlineW 取 1bpp 点阵并懒缓存，逐像素写入 FrameBuffer。
#include "ui/Text.hpp"

#include <map>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX // MinGW 的 libstdc++ 可能已定义
#define NOMINMAX
#endif
#include <windows.h>
#else
#include "ui/Font5x7.hpp" // 非 Windows 回退：自写 5x7 点阵
#endif

namespace cg::text {

#ifdef _WIN32
namespace {

constexpr int kFontHeight = 16; // 宋体 16px：小字号汉字最清晰

// 一个字符的点阵：DWORD 对齐的 1bpp 行（最高位 = 最左像素），外加排版度量
struct GlyphBitmap {
    int w = 0;                        // 黑盒宽
    int h = 0;                        // 黑盒高
    int originY = 0;                  // 基线到黑盒顶的距离
    int advanceX = 0;                 // 步进宽
    std::vector<unsigned char> bits;  // 空 = 无字形（画空位）
};

HFONT gFont = nullptr;
HDC gDc = nullptr;
int gHeight = 0;
int gAscent = 0;
std::map<wchar_t, GlyphBitmap> gCache;

void ensureFont() {
    if (gFont != nullptr) {
        return;
    }
    gDc = GetDC(nullptr);
    gFont = CreateFontW(-kFontHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                        DEFAULT_PITCH | FF_DONTCARE, L"SimSun");
    SelectObject(gDc, gFont);
    TEXTMETRICW tm{};
    GetTextMetricsW(gDc, &tm);
    gHeight = tm.tmHeight;
    gAscent = tm.tmAscent;
}

const GlyphBitmap& glyph(wchar_t ch) {
    ensureFont();
    const auto it = gCache.find(ch);
    if (it != gCache.end()) {
        return it->second;
    }
    GlyphBitmap g;
    MAT2 mat{};
    mat.eM11.value = 1;
    mat.eM22.value = 1;
    GLYPHMETRICS gm{};
    const DWORD size = GetGlyphOutlineW(gDc, ch, GGO_BITMAP, &gm, 0, nullptr, &mat);
    if (size > 0 && size != GDI_ERROR) {
        std::vector<unsigned char> buf(size);
        GetGlyphOutlineW(gDc, ch, GGO_BITMAP, &gm, size, buf.data(), &mat);
        g.w = static_cast<int>(gm.gmBlackBoxX);
        g.h = static_cast<int>(gm.gmBlackBoxY);
        g.originY = static_cast<int>(gm.gmptGlyphOrigin.y);
        g.advanceX = static_cast<int>(gm.gmCellIncX);
        // 两次调用的度量可能不一致：按黑盒尺寸核算缓冲，不够就弃用点阵防越界。
        const std::size_t rowBytes = (static_cast<std::size_t>(g.w) + 31) / 32 * 4;
        if (rowBytes * static_cast<std::size_t>(g.h) > buf.size()) {
            g.bits.clear();
        } else {
            g.bits = std::move(buf);
        }
    } else if (GetCharWidth32W(gDc, ch, ch, &g.advanceX) == 0) {
        g.advanceX = kFontHeight / 2; // 取不到度量：留半个字宽的空位
    }
    return gCache.emplace(ch, std::move(g)).first->second;
}

std::wstring toWide(const char* s) {
    if (s == nullptr) {
        return {};
    }
    const int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    if (n <= 1) {
        return {};
    }
    std::wstring ws(static_cast<std::size_t>(n - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s, -1, ws.data(), n);
    return ws;
}

} // namespace
#endif // _WIN32

int height() {
#ifdef _WIN32
    ensureFont();
    return gHeight;
#else
    return font5x7::kGlyphRows;
#endif
}

int textWidth(const char* s) {
#ifdef _WIN32
    int w = 0;
    for (wchar_t ch : toWide(s)) {
        w += glyph(ch).advanceX;
    }
    return w;
#else
    int n = 0;
    for (; s != nullptr && *s != '\0'; ++s) {
        ++n;
    }
    return n * font5x7::kAdvance;
#endif
}

void drawText(FrameBuffer& fb, int x, int y, const char* s, const Color& c) {
#ifdef _WIN32
    for (wchar_t ch : toWide(s)) {
        const GlyphBitmap& g = glyph(ch);
        if (!g.bits.empty()) {
            const int rowBytes = ((g.w + 31) / 32) * 4;
            const int top = y + gAscent - g.originY; // 行框顶 → 字形顶
            for (int row = 0; row < g.h; ++row) {
                const unsigned char* line = g.bits.data() + static_cast<std::size_t>(row) * rowBytes;
                for (int col = 0; col < g.w; ++col) {
                    if ((line[col / 8] >> (7 - (col % 8))) & 0x01) {
                        fb.putPixel(x + col, top + row, c);
                    }
                }
            }
        }
        x += g.advanceX;
    }
#else
    font5x7::drawText(fb, x, y, s, c);
#endif
}

} // namespace cg::text
