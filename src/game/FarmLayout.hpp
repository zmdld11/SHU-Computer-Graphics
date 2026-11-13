// FarmLayout.hpp
// 胖龙农场世界布局：48×36 个 32px 格（1536×1152），4×3 块农田，右侧椭圆池塘，
// 一条从农田通向木栈道的土路。逻辑和渲染共用这一份坐标，避免两边各写一套数字。
#pragma once
#include "core/Point.hpp"

#include <algorithm>
#include <cmath>

namespace cg::game {

constexpr int kCell = 32;
constexpr int kCols = 48;
constexpr int kRows = 36;
constexpr int kWorldW = kCols * kCell;
constexpr int kWorldH = kRows * kCell;

// 视口适配：窗口不超过地图时 1:1 平移镜头；超过地图时整图等比放大充满窗口。
// viewW/viewH 是相机窗（世界坐标，渲染仍按 1:1），scale 是画上窗口的放大倍数。
struct ViewportFit {
    int viewW = kWorldW;
    int viewH = kWorldH;
    float scale = 1.0f;
};

inline ViewportFit fitViewport(int fbW, int fbH) {
    ViewportFit fit;
    const float rw = static_cast<float>(std::max(1, fbW)) / kWorldW;
    const float rh = static_cast<float>(std::max(1, fbH)) / kWorldH;
    fit.scale = std::max(1.0f, std::max(rw, rh));
    fit.viewW = std::min(kWorldW, static_cast<int>(std::ceil(fbW / fit.scale)));
    fit.viewH = std::min(kWorldH, static_cast<int>(std::ceil(fbH / fit.scale)));
    fit.viewW = std::max(1, fit.viewW);
    fit.viewH = std::max(1, fit.viewH);
    return fit;
}

// 可行走范围：四周留 6 格做树林边界，角色不会走到树冠底下。
constexpr int kWalkMinCol = 6;
constexpr int kWalkMaxCol = 41;
constexpr int kWalkMinRow = 6;
constexpr int kWalkMaxRow = 29;

// 4×3 块农田，每块 3×3 格，块与块之间留 1 格。
constexpr int kPlotCols = 4;
constexpr int kPlotRows = 3;
constexpr int kPlotCells = 3;
constexpr int kFarmCol = 5;
constexpr int kFarmRow = 8;
constexpr int kPlotPitchCells = kPlotCells + 1;
constexpr int kPlotPitch = kPlotPitchCells * kCell;
constexpr int kFarmX = kFarmCol * kCell;
constexpr int kFarmY = kFarmRow * kCell;
constexpr int kFarmW = kPlotCols * kPlotPitch - kCell;
constexpr int kFarmH = kPlotRows * kPlotPitch - kCell;

// 农田外圈土台（左右上各多 16px，底边与田块齐平，直接接土路）。
constexpr int kFarmApronX = kFarmX - 16;
constexpr int kFarmApronY = kFarmY - 16;
constexpr int kFarmApronW = kFarmW + 32;
constexpr int kFarmApronH = kFarmH + 16;

// 农田动态层重烘范围：与土台同宽（树冠最右 141px，不会伸进 144 以内），
// 上下外扩罩住田垄装饰与土床边缘的外溢；边界外一圈不放静态装饰，
// 保证局部重烘与整图烘培逐像素一致。
constexpr int kFarmBakeX = kFarmApronX;
constexpr int kFarmBakeY = kFarmApronY - 16;
constexpr int kFarmBakeW = kFarmApronW;
constexpr int kFarmBakeH = kFarmApronH + 32;

// 土路：横穿农田下方，接到池塘栈道。
constexpr int kPathX0 = 144;
constexpr int kPathX1 = 952;
constexpr int kPathY0 = 608;
constexpr int kPathY1 = 672;

// 池塘与伸进水的木栈道（栈道是水面上唯一可行走的格子）。
constexpr float kPondCx = 1120.0f;
constexpr float kPondCy = 576.0f;
constexpr float kPondRx = 192.0f;
constexpr float kPondRy = 224.0f;

// 路灯（夜间点光源）：立在土路与栈道岔口的草地上，灯头在 y-52。
constexpr float kLampX = 720.0f;
constexpr float kLampY = 560.0f;
constexpr float kLampBulbY = kLampY - 52.0f;

constexpr int kDockCol = 27;
constexpr int kDockRow = 19;
constexpr int kDockCols = 5;
constexpr int kDockRows = 2;
constexpr int kDockX = kDockCol * kCell;
constexpr int kDockY = kDockRow * kCell;
constexpr int kDockW = kDockCols * kCell;
constexpr int kDockH = kDockRows * kCell;
constexpr int kDockTipCol = kDockCol + kDockCols - 1;
constexpr int kDockTipRow = kDockRow + kDockRows - 1;

// 栈道尽头的小灯：灯头在水面上方（y 小于桥面），鱼绕过桥头时也要被它遮住。
constexpr int kDockLampX = 1014;
constexpr int kDockLampY = 593; // 灯头中心

constexpr int kSpawnCol = 20;
constexpr int kSpawnRow = 20;

// ---- 小屋（场景切换）----
// 农场北面草地上的木屋：外观是静态装饰，门前那格是进屋的交互点。
constexpr float kCabinX = 700.0f;
constexpr float kCabinY = 130.0f;
constexpr float kCabinW = 300.0f;
constexpr float kCabinH = 190.0f;
constexpr float kCabinDoorX = kCabinX + kCabinW * 0.5f; // 850
constexpr float kCabinDoorY = kCabinY + kCabinH;        // 320

// 农场侧：站在门前格（屋门正下方）即可进屋。
constexpr int kCabinFrontCol = 26;
constexpr int kCabinFrontRow = 10;

// 室内场景：25×19 格（800×608），单屏基本无滚动。行 0-1 顶墙、17-18 底墙、
// 列 0/24 侧墙；家具（床/储物箱/桌）不可穿行，底墙开口是出门的地垫。
constexpr int kCabinCols = 25;
constexpr int kCabinRows = 19;
constexpr int kRoomW = kCabinCols * kCell;
constexpr int kRoomH = kCabinRows * kCell;
constexpr int kCabinDoorCol = 12; // 地垫中心格：站上去点地垫出门
constexpr int kCabinDoorRow = 16;

inline bool inCabinFloor(int col, int row) {
    return col >= 1 && col <= 23 && row >= 2 && row <= 16;
}

// 家具占格：床(4-7, 3-8)、储物箱(19-21, 3-5)、桌子(8-10, 2-3)。
// 床放在左半屏，避免被右上角的订单板 HUD 挡住。
inline bool inCabinFurniture(int col, int row) {
    if (col >= 4 && col <= 7 && row >= 3 && row <= 8) {
        return true;
    }
    if (col >= 19 && col <= 21 && row >= 3 && row <= 5) {
        return true;
    }
    if (col >= 8 && col <= 10 && row >= 2 && row <= 3) {
        return true;
    }
    return false;
}

inline bool cabinWalkableCell(int col, int row) {
    return inCabinFloor(col, row) && !inCabinFurniture(col, row);
}

// 床沿交互点：床右侧那格，站上去点床睡觉。
constexpr int kCabinBedSideCol = 8;
constexpr int kCabinBedSideRow = 5;

// 订单需求：3 白菜 + 3 番茄 + 2 胡萝卜 + 3 鱼，集齐即胜利。
constexpr int kOrderCabbage = 3;
constexpr int kOrderTomato = 3;
constexpr int kOrderCarrot = 2;
constexpr int kOrderFish = 3;

// HUD：3 个种子槽 + 左上角木牌 + 右上角订单板。渲染和鼠标命中测试共用。
constexpr int kHudSlot = 62;
constexpr int kHudGap = 14;

struct RectI {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;

    bool contains(IPoint p) const {
        return p.x >= x && p.x < x + w && p.y >= y && p.y < y + h;
    }

    RectI inflated(int margin) const {
        return {x - margin, y - margin, w + 2 * margin, h + 2 * margin};
    }
};

inline IPoint cellCenter(int col, int row) {
    return {col * kCell + kCell / 2, row * kCell + kCell / 2};
}

inline FPoint cellCenterF(int col, int row) {
    return {static_cast<float>(col * kCell + kCell / 2),
            static_cast<float>(row * kCell + kCell / 2)};
}

inline IPoint cellOf(FPoint p) {
    return {static_cast<int>(std::floor(p.x / static_cast<float>(kCell))),
            static_cast<int>(std::floor(p.y / static_cast<float>(kCell)))};
}

inline bool inWalkBounds(int col, int row) {
    return col >= kWalkMinCol && col <= kWalkMaxCol && row >= kWalkMinRow &&
           row <= kWalkMaxRow;
}

inline bool inDockCell(int col, int row) {
    return col >= kDockCol && col < kDockCol + kDockCols && row >= kDockRow &&
           row < kDockRow + kDockRows;
}

inline bool inPond(float x, float y, float margin = 0.0f) {
    const float nx = (x - kPondCx) / (kPondRx + margin);
    const float ny = (y - kPondCy) / (kPondRy + margin);
    return nx * nx + ny * ny <= 1.0f;
}

inline bool inDock(float x, float y) {
    return x >= static_cast<float>(kDockX) && x < static_cast<float>(kDockX + kDockW) &&
           y >= static_cast<float>(kDockY) && y < static_cast<float>(kDockY + kDockH);
}

// 小屋占地：屋里是实体，玩家不能穿屋而过（空气墙）；门前那一格在屋面
// y = kCabinY + kCabinH 之下，不在占地内，仍是进门交互点。
inline bool inCabinFootprint(float x, float y) {
    return x >= kCabinX && x < kCabinX + kCabinW && y >= kCabinY &&
           y < kCabinY + kCabinH;
}

inline bool inFarmApron(float x, float y) {
    return x >= kFarmApronX && x < kFarmApronX + kFarmApronW && y >= kFarmApronY &&
           y < kFarmApronY + kFarmApronH;
}

inline bool inFarmBakeRect(float x, float y) {
    return x >= kFarmBakeX && x < kFarmBakeX + kFarmBakeW && y >= kFarmBakeY &&
           y < kFarmBakeY + kFarmBakeH;
}

// 重烘矩形再外扩一圈（装饰抖动幅度 + 草叶高度），草丛/花/石头只要中心
// 落在这一圈里就不生成，避免装饰跨界被农田层章盖掉半截。
inline bool nearFarmBakeRect(float x, float y) {
    constexpr float kMargin = 24.0f;
    return x >= static_cast<float>(kFarmBakeX) - kMargin &&
           x < static_cast<float>(kFarmBakeX + kFarmBakeW) + kMargin &&
           y >= static_cast<float>(kFarmBakeY) - kMargin &&
           y < static_cast<float>(kFarmBakeY + kFarmBakeH) + kMargin;
}

inline bool inPathArea(float x, float y) {
    return x >= kPathX0 - 8 && x <= kPathX1 + 8 && y >= kPathY0 - 8 && y <= kPathY1 + 8;
}

// 栈道格子优先于水面判定：码头是玩家在水上唯一能站的地方。
inline bool isWalkableCell(int col, int row) {
    if (!inWalkBounds(col, row)) {
        return false;
    }
    if (inDockCell(col, row)) {
        return true;
    }
    const IPoint c = cellCenter(col, row);
    if (inCabinFootprint(static_cast<float>(c.x), static_cast<float>(c.y))) {
        return false; // 小屋是实体：不能穿墙，只能从门前那格进出
    }
    return !inPond(static_cast<float>(c.x), static_cast<float>(c.y), 10.0f);
}

inline int plotIndexAtCell(int col, int row) {
    if (col < kFarmCol || row < kFarmRow) {
        return -1;
    }
    const int lc = col - kFarmCol;
    const int lr = row - kFarmRow;
    if (lc >= kPlotCols * kPlotPitchCells || lr >= kPlotRows * kPlotPitchCells) {
        return -1;
    }
    if (lc % kPlotPitchCells >= kPlotCells || lr % kPlotPitchCells >= kPlotCells) {
        return -1;
    }
    return (lr / kPlotPitchCells) * kPlotCols + (lc / kPlotPitchCells);
}

inline int plotIndexAtPoint(FPoint p) {
    const IPoint c = cellOf(p);
    return plotIndexAtCell(c.x, c.y);
}

inline RectI plotRect(int index) {
    const int c = index % kPlotCols;
    const int r = index / kPlotCols;
    return {kFarmX + c * kPlotPitch, kFarmY + r * kPlotPitch, kPlotCells * kCell,
            kPlotCells * kCell};
}

inline IPoint plotCenterCell(int index) {
    const int c = index % kPlotCols;
    const int r = index / kPlotCols;
    return {kFarmCol + c * kPlotPitchCells + 1, kFarmRow + r * kPlotPitchCells + 1};
}

inline FPoint plotCenter(int index) {
    const IPoint c = plotCenterCell(index);
    return cellCenterF(c.x, c.y);
}

inline RectI hotbarSlotRect(int slot, int viewW, int viewH) {
    const int total = 3 * kHudSlot + 2 * kHudGap;
    const int x0 = (viewW - total) / 2;
    const int y0 = viewH - kHudSlot - 18;
    return {x0 + slot * (kHudSlot + kHudGap), y0, kHudSlot, kHudSlot};
}

inline RectI titlePlaqueRect() {
    return {18, 16, 154, 48};
}

// 右上角订单板：四行物品进度 + 完成状态行 + 一行用时。
// 订单集齐不再弹结算面板，只在任务清单下面标一行“全部任务已完成”。
inline RectI orderBoardRect(int viewW) {
    return {viewW - 214, 14, 198, 198};
}

// 右下角常驻重开按钮：任何时候都能重开一局，不依赖键盘。
constexpr int kRestartW = 88;
constexpr int kRestartH = 34;

inline RectI restartButtonRect(int viewW, int viewH) {
    return {viewW - kRestartW - 18, viewH - kRestartH - 18, kRestartW, kRestartH};
}

// 顶部中央时间轴：时刻文字 + 24h 轨道 + 可拖拽旋钮。
// 面板与轨道分开给，渲染和命中测试共用一份几何。
constexpr int kTimeBarW = 308;
constexpr int kTimeBarH = 66;

inline RectI timeBarRect(int viewW) {
    return {viewW / 2 - kTimeBarW / 2, 10, kTimeBarW, kTimeBarH};
}

// 轨道本体（不含左右留白），拖动与旋钮位置都按这个矩形换算。
inline RectI timeTrackRect(int viewW) {
    return {viewW / 2 - 132, 46, 264, 12};
}

// 轨道上的横坐标 → 0-24 时刻；反过来时刻 → 旋钮圆心 x。
inline float timeFromTrackX(float x, int viewW) {
    const RectI t = timeTrackRect(viewW);
    const float u = (x - static_cast<float>(t.x)) / static_cast<float>(t.w);
    return std::clamp(u, 0.0f, 0.9999f) * 24.0f;
}

inline float trackXFromTime(float hour, int viewW) {
    const RectI t = timeTrackRect(viewW);
    return static_cast<float>(t.x) +
           std::clamp(hour / 24.0f, 0.0f, 1.0f) * static_cast<float>(t.w);
}

inline bool inHud(IPoint p, int viewW, int viewH) {
    if (titlePlaqueRect().contains(p)) {
        return true;
    }
    if (orderBoardRect(viewW).contains(p)) {
        return true;
    }
    if (timeBarRect(viewW).contains(p)) {
        return true;
    }
    if (restartButtonRect(viewW, viewH).inflated(2).contains(p)) {
        return true;
    }
    for (int i = 0; i < 3; ++i) {
        if (hotbarSlotRect(i, viewW, viewH).inflated(4).contains(p)) {
            return true;
        }
    }
    return false;
}

} // namespace cg::game
