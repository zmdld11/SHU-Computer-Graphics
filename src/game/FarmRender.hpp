// FarmRender.hpp
// 胖龙农场的矢量渲染：整张地图烘进背景缓存（2× 超采样降采样），每帧只拷相机窗口、
// 再画胖龙/悬停提示/HUD。所有图形都由 FrameBuffer 与手写光栅算法生成，无外部素材。
#pragma once
#include "core/FrameBuffer.hpp"
#include "game/FarmWorld.hpp"

namespace cg::game {

class FarmRender {
public:
    FarmRender();

    // 按当前世界状态烘焙背景（含农田作物）；只在启动/重开时调用一次。
    void build(const FarmWorld& world);

    // 每帧：世界缓存 → 视口，再叠加悬停格、胖龙和 HUD。
    void frame(FrameBuffer& fb, const FarmWorld& world, bool hud);

    // 美术评审/报告插图：输出整张 1536×1152 世界图（2× 超采样后降采样）。
    FrameBuffer captureWorld(const FarmWorld& world) const;

    // 汇报/报告用的纯背景：空田、无角色、无 HUD。
    FrameBuffer captureBackground() const;

private:
    FrameBuffer background_;
};

} // namespace cg::game
