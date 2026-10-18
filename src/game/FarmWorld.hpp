// FarmWorld.hpp
// 胖龙农场的纯玩法层：格点移动、镜头、农田状态。不 include raster/ui，可无窗口自测。
#pragma once
#include "game/FarmLayout.hpp"

#include <array>
#include <cstddef>
#include <vector>

namespace cg::game {

enum class Seed : int { Tea = 0, Pearl = 1, Coconut = 2 };
enum class CropStage : int { Empty = 0, Seed = 1, Sprout = 2, Mature = 3 };

struct Crop {
    Seed kind = Seed::Tea;
    CropStage stage = CropStage::Empty;
    float age = 0.0f;
    bool watered = false;
};

enum class HoverKind { None, Cell, Plot, Pond };

struct Hover {
    HoverKind kind = HoverKind::None;
    int plot = -1;
    IPoint cell{};
};

// 一局游戏的逻辑状态。移动以 32px 格为最小单位，角色最终一定停在格心。
class FarmWorld {
public:
    static constexpr float kPlayerSpeed = 220.0f;

    FarmWorld();

    void reset();
    void setViewport(int width, int height);
    void update(float dt);

    // worldPos 已由调用方加上相机偏移。
    void onHover(FPoint worldPos);
    void clearHover();
    void onClick(FPoint worldPos);
    void cancelMove();

    void selectSeed(int index);
    Seed selectedSeed() const { return selectedSeed_; }

    FPoint playerPos() const { return playerPos_; }
    FPoint camera() const { return camera_; }
    bool moving() const { return moving_; }
    float walkPhase() const { return walkPhase_; }
    float animTime() const { return animTime_; }
    int facing() const { return facing_; }
    const Hover& hover() const { return hover_; }
    const Crop& crop(int index) const { return plots_[static_cast<std::size_t>(index)]; }

    // 只读访问器：给 --game-smoke 验证路径与目标，玩法本身不用。
    const std::vector<FPoint>& waypoints() const { return waypoints_; }
    std::size_t waypointIndex() const { return waypointIndex_; }
    FPoint goal() const { return waypoints_.empty() ? playerPos_ : waypoints_.back(); }

    // 预览图用：直接摆一块田/把角色放到指定格，不影响正常流程。
    void debugPlant(int plotIndex, Seed kind, CropStage stage, bool watered = false);
    void debugSetPlayerCell(int col, int row);

private:
    bool cellsClearH(int row, int fromCol, int toCol) const;
    bool cellsClearV(int col, int fromRow, int toRow) const;
    bool tryLPath(IPoint from, IPoint to, bool horizontalFirst);
    bool tryBfsPath(IPoint from, IPoint to);
    void buildPath(IPoint start, IPoint goalCell);
    void updateCamera(float dt, bool snap);

    FPoint playerPos_{};
    FPoint camera_{};
    float viewW_ = 800.0f;
    float viewH_ = 600.0f;
    bool cameraReady_ = false;

    std::vector<FPoint> waypoints_;
    std::size_t waypointIndex_ = 0;
    bool moving_ = false;
    int facing_ = 1;
    float walkPhase_ = 0.0f;
    float animTime_ = 0.0f;

    std::array<Crop, kPlotCols * kPlotRows> plots_{};
    Seed selectedSeed_ = Seed::Tea;
    Hover hover_{};
};

} // namespace cg::game
