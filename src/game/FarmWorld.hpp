// FarmWorld.hpp
// 胖龙农场的纯玩法层：格点移动、镜头、农田状态、钓鱼状态机、订单与库存、粒子池。
// 不 include raster/ui，可无窗口自测。
#pragma once
#include "game/FarmLayout.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace cg::game {

enum class Seed : int { Cabbage = 0, Tomato = 1, Carrot = 2 };
enum class CropStage : int { Empty = 0, Seed = 1, Sprout = 2, Mature = 3 };

struct Crop {
    Seed kind = Seed::Cabbage;
    CropStage stage = CropStage::Empty;
    float age = 0.0f;
    bool watered = false;
};

enum class HoverKind { None, Cell, Plot, Pond, Door, Bed };

struct Hover {
    HoverKind kind = HoverKind::None;
    int plot = -1;
    IPoint cell{};
};

// 场景：农场 ⇄ 小屋室内。切换有 0.5s 渐黑 + 0.5s 渐亮的转场。
enum class GameScene { Farm, Cabin };

// 转场类型：进屋/出屋/睡觉（睡觉在中点把时间跳到次日早晨）。
enum class SceneTransition { None, ToCabin, ToFarm, Sleep };

// 走到目标后要执行的动作。点击只记"想去哪/想做什么"，到位再落地，
// 这样寻路与交互解耦，路径被水挡住时也只会走到能走到的格。
enum class PendingKind { None, Plot, Pond, Enter, Sleep, Exit };

struct Pending {
    PendingKind kind = PendingKind::None;
    int plot = -1;
};

// 钓鱼状态机（设计稿 §7.2）。
enum class FishState { Idle, Casting, Waiting, Biting, Caught, Missed };

// 库存：四种物品的计数器。种子无限，不进库存。
struct Inventory {
    int cabbage = 0;
    int tomato = 0;
    int carrot = 0;
    int fish = 0;

    int& slot(Seed kind) {
        switch (kind) {
        case Seed::Tomato: return tomato;
        case Seed::Carrot: return carrot;
        default: return cabbage;
        }
    }
    const int& slot(Seed kind) const {
        switch (kind) {
        case Seed::Tomato: return tomato;
        case Seed::Carrot: return carrot;
        default: return cabbage;
        }
    }
    bool meetsOrder() const {
        return cabbage >= kOrderCabbage && tomato >= kOrderTomato &&
               carrot >= kOrderCarrot && fish >= kOrderFish;
    }
};

// 粒子类型：逻辑层只推进位置/寿命，画法由渲染层按类型决定。
enum class ParticleType { Droplet, Ring, Sparkle };

struct Particle {
    FPoint pos{};
    FPoint vel{};
    float life = 0.0f;
    float maxLife = 1.0f;
    ParticleType type = ParticleType::Droplet;
};

// ---- 昼夜模型（纯函数，渲染与 --game-smoke 共用）----
// 太阳 6:00-18:00 在天上：方位自东向西扫，高度角正弦；路灯 17:30 点到次日 6:30
//（两端各 1 小时淡入淡出），晨昏时段两个光源同时生效。

struct SkyState {
    bool sunUp = false;
    float sunElevation = 0.0f; // 0=地平线 1=正午
    float sunPhase = 0.0f;     // 0=日出(东) 0.5=正午 1=日落(西)
    float lampIntensity = 0.0f;
};

SkyState skyAt(float hour);

// 太阳影子方向：物体每 1px 高度对应的地面偏移。晨昏被拉长、正午最短，
// 方向一天内自西 → 南 → 东扫过去。
FPoint sunShadowVec(float hour);

// 环境色调：夜蓝 + 晨昏暖橙两层。alpha=0 表示该层不叠。
// 只用字节字段，保持本头文件不依赖图形模块。
struct AmbientTint {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t alpha = 0;
};

void ambientTintAt(float hour, AmbientTint& blue, AmbientTint& warm);

// 一局游戏的逻辑状态。移动以 32px 格为最小单位，角色最终一定停在格心。
class FarmWorld {
public:
    static constexpr float kPlayerSpeed = 220.0f;
    static constexpr float kCastTime = 0.5f;
    static constexpr float kBiteWindow = 0.8f;
    static constexpr float kFishTailTime = 1.0f;
    static constexpr int kMaxParticles = 256;
    static constexpr float kDayLength = 240.0f; // 一个游戏日 = 现实 240 秒

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

    // 玩法状态：农田层版本号（渲染层据此重烘农田）、库存、订单、用时。
    std::uint64_t farmVersion() const { return farmVersion_; }
    const Inventory& inventory() const { return inventory_; }
    bool won() const { return won_; }
    float playSeconds() const { return playSeconds_; }

    // 昼夜：游戏内时刻（0-24 浮点小时）与天数。7:00 起始。
    float hour() const { return dayTime_ * 24.0f / kDayLength; }
    int day() const { return day_; }

    // 场景与转场：fadeAlpha 供渲染层画渐黑渐亮（0 全显 → 1 全黑）。
    GameScene scene() const { return scene_; }
    float fadeAlpha() const;

    // 钓鱼：状态、鱼漂位置与抛竿控制点（渲染要画线/漂）、计划好的等待时长（测试用）。
    FishState fishState() const { return fishState_; }
    FPoint bobberPos() const { return bobberPos_; }
    FPoint bobberCtrl() const { return bobberCtrl_; }
    float fishWaitSeconds() const { return fishWait_; }

    // 粒子池（渲染层按类型绘制）。
    const std::vector<Particle>& particles() const { return particles_; }

    // 只读访问器：给 --game-smoke 验证路径与目标，玩法本身不用。
    const std::vector<FPoint>& waypoints() const { return waypoints_; }
    std::size_t waypointIndex() const { return waypointIndex_; }
    FPoint goal() const { return waypoints_.empty() ? playerPos_ : waypoints_.back(); }
    const Pending& pending() const { return pending_; }

    // 预览图/测试用：直接摆一块田/把角色放到指定格，不影响正常流程。
    void debugPlant(int plotIndex, Seed kind, CropStage stage, bool watered = false);
    void debugSetPlayerCell(int col, int row);
    // kind：0/1/2 = 三种作物，3 = 鱼。测试订单胜利用。
    void debugAddInventory(int kind, int count);
    // 直接把时刻拨到指定小时（测试/出图用），不影响天数。
    void debugSetTime(float hour);
    // 直接把场景拨到室内（出图用）；正式流程走门交互的转场。
    void debugEnterCabin();

private:
    // 场景感知的可行走判定：农场走格子规则，室内走房间规则。
    bool walkable(int col, int row) const;
    float worldWidth() const;
    float worldHeight() const;
    void startTransition(SceneTransition kind);
    void applyTransitionSwitch();
    void skipToMorning();
    bool cellsClearH(int row, int fromCol, int toCol) const;
    bool cellsClearV(int col, int fromRow, int toRow) const;
    bool tryLPath(IPoint from, IPoint to, bool horizontalFirst);
    bool tryBfsPath(IPoint from, IPoint to);
    void buildPath(IPoint start, IPoint goalCell);
    void updateCamera(float dt, bool snap);
    void updateCrops(float dt);
    void updateFishing(float dt);
    void updateParticles(float dt);
    void onArrived();
    void interactPlot(int plotIndex);
    void startCasting();
    void addParticles(ParticleType type, FPoint pos, int count);
    std::uint32_t nextRand();
    void markFarmChanged() { ++farmVersion_; }

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
    Seed selectedSeed_ = Seed::Cabbage;
    Hover hover_{};
    Pending pending_{};
    std::uint64_t farmVersion_ = 0;

    Inventory inventory_{};
    bool won_ = false;
    float playSeconds_ = 0.0f;

    float dayTime_ = 70.0f; // 7:00 起床干活
    int day_ = 1;

    GameScene scene_ = GameScene::Farm;
    SceneTransition transition_ = SceneTransition::None;
    float transitionT_ = 0.0f;
    bool transitionSwitched_ = false;

    FishState fishState_ = FishState::Idle;
    float fishTimer_ = 0.0f;
    float fishWait_ = 0.0f;
    FPoint bobberStart_{};
    FPoint bobberCtrl_{};
    FPoint bobberTarget_{};
    FPoint bobberPos_{};
    float rippleTimer_ = 0.0f;
    std::uint32_t fishRng_ = 0x9E3779B9u;

    std::vector<Particle> particles_;
};

} // namespace cg::game
