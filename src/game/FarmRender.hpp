// FarmRender.hpp
// 胖龙农场的矢量渲染：静态地图烘进背景缓存（2× 超采样降采样），每帧只拷相机窗口；
// 农田（土壤湿度/作物形态）单独一层缓存，玩法改变时局部重烘，避免整图重烘卡顿。
// 所有图形都由 FrameBuffer 与手写光栅算法生成，无外部素材。
#pragma once
#include "core/FrameBuffer.hpp"
#include "game/FarmWorld.hpp"

#include <cstdint>
#include <vector>

namespace cg::game {

class FarmRender {
public:
    FarmRender();

    // 按当前世界状态烘焙背景（静态地图 + 农田层）；只在启动/重开时调用一次。
    void build(const FarmWorld& world);

    // 每帧：世界缓存 → 视口，再叠加悬停格、胖龙、鱼漂、粒子和 HUD。
    // 农田层版本号落后于世界时自动局部重烘。
    void frame(FrameBuffer& fb, const FarmWorld& world, bool hud);

    // 美术评审/报告插图：输出整张 1536×1152 世界图（2× 超采样后降采样）。
    FrameBuffer captureWorld(const FarmWorld& world) const;

    // 与 captureWorld 相同但不含角色：用于校验"静态+农田层"与整图烘培一致。
    FrameBuffer captureScene(const FarmWorld& world) const;

    // 汇报/报告用的纯背景：空田、无角色、无 HUD。
    FrameBuffer captureBackground() const;

    // 分层累计帧：草地、土路、农田、池塘与栈道、花草石头、树木，共 6 张。
    std::vector<FrameBuffer> captureLayers(const FarmWorld& world) const;

private:
    // 农田层：在农田土台附近的小矩形里重画 草地+土路+农田，再盖回大背景。
    void bakeFarmLayer(const FarmWorld& world);

    FrameBuffer background_;
    FrameBuffer cabinBackground_; // 小屋室内：一次性烘焙，每帧按相机拷贝
    FrameBuffer farmLayer_;
    RectI farmBakeRect_{};
    std::uint64_t bakedFarmVersion_ = 0;
};

} // namespace cg::game
