// FarmWorld.cpp
// 玩法逻辑：格点移动（L 形优先、BFS 兜底）、镜头、农田生长、钓鱼状态机、订单结算。
// 不做任何像素绘制，全部状态可无窗口自测。
#include "game/FarmWorld.hpp"

#include <algorithm>
#include <cmath>
#include <queue>

namespace cg::game {
namespace {

constexpr float kPi = 3.14159265358979f;

float lerp(float a, float b, float t) {
    return a + (b - a) * std::clamp(t, 0.0f, 1.0f);
}

float distanceSq(FPoint a, FPoint b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return dx * dx + dy * dy;
}

// 二次 Bezier：抛竿轨迹从竿尖到落点。
FPoint bezierPoint(FPoint p0, FPoint p1, FPoint p2, float t) {
    const float u = 1.0f - t;
    return {u * u * p0.x + 2.0f * u * t * p1.x + t * t * p2.x,
            u * u * p0.y + 2.0f * u * t * p1.y + t * t * p2.y};
}

} // namespace

SkyState skyAt(float hour) {
    SkyState s;
    hour = std::fmod(hour + 24.0f, 24.0f);
    if (hour >= 6.0f && hour <= 18.0f) {
        s.sunUp = true;
        s.sunPhase = (hour - 6.0f) / 12.0f;
        s.sunElevation = std::sin(kPi * s.sunPhase);
    }
    // 路灯 17:30 亮到次日 6:30，两端各 1 小时渐亮/渐灭。
    if (hour >= 17.5f || hour < 6.5f) {
        if (hour >= 17.5f && hour < 18.5f) {
            s.lampIntensity = hour - 17.5f;
        } else if (hour >= 18.5f || hour < 5.5f) {
            s.lampIntensity = 1.0f;
        } else {
            s.lampIntensity = 1.0f - (hour - 5.5f);
        }
    }
    return s;
}

FPoint sunShadowVec(float hour) {
    const SkyState s = skyAt(hour);
    if (!s.sunUp) {
        return {0.0f, 0.0f};
    }
    // 高度角越低影子拉得越长；方位反着太阳：早晨朝西，傍晚朝东，全天略向南偏。
    const float stretch = lerp(1.9f, 0.45f, s.sunElevation);
    const float dx = -std::cos(kPi * s.sunPhase);
    const float dy = 0.42f;
    const float len = std::sqrt(dx * dx + dy * dy);
    return {dx / len * stretch, dy / len * stretch};
}

void ambientTintAt(float hour, AmbientTint& blue, AmbientTint& warm) {
    hour = std::fmod(hour + 24.0f, 24.0f);
    float b;
    if (hour < 4.5f) {
        b = 1.0f;
    } else if (hour < 7.5f) {
        b = 1.0f - (hour - 4.5f) / 3.0f;
    } else if (hour < 16.5f) {
        b = 0.0f;
    } else if (hour < 19.5f) {
        b = (hour - 16.5f) / 3.0f;
    } else {
        b = 1.0f;
    }
    blue.r = 16;
    blue.g = 24;
    blue.b = 60;
    blue.alpha = static_cast<std::uint8_t>(b * 108.0f);

    float w = 0.0f;
    if (hour > 5.0f && hour < 6.5f) {
        w = 1.0f - std::fabs(hour - 5.75f) / 0.75f;
    } else if (hour > 17.5f && hour < 19.0f) {
        w = 1.0f - std::fabs(hour - 18.25f) / 0.75f;
    }
    warm.r = 255;
    warm.g = 132;
    warm.b = 48;
    warm.alpha = static_cast<std::uint8_t>(std::clamp(w, 0.0f, 1.0f) * 46.0f);
}

FarmWorld::FarmWorld() {
    reset();
}

void FarmWorld::reset() {
    playerPos_ = cellCenterF(kSpawnCol, kSpawnRow);
    waypoints_.clear();
    waypointIndex_ = 0;
    moving_ = false;
    facing_ = 1;
    walkPhase_ = 0.0f;
    animTime_ = 0.0f;
    plots_.fill(Crop{});
    selectedSeed_ = Seed::Cabbage;
    hover_ = Hover{};
    pending_ = Pending{};
    inventory_ = Inventory{};
    won_ = false;
    playSeconds_ = 0.0f;
    dayTime_ = 70.0f;
    day_ = 1;
    scene_ = GameScene::Farm;
    transition_ = SceneTransition::None;
    transitionT_ = 0.0f;
    transitionSwitched_ = false;
    fishState_ = FishState::Idle;
    fishTimer_ = 0.0f;
    fishWait_ = 0.0f;
    rippleTimer_ = 0.0f;
    fishRng_ = 0x9E3779B9u;
    particles_.clear();
    farmVersion_++;
    cameraReady_ = false;
    updateCamera(0.0f, true);
}

// ---- 场景与昼夜的共用小工具 ----

bool FarmWorld::walkable(int col, int row) const {
    return scene_ == GameScene::Farm ? isWalkableCell(col, row)
                                     : cabinWalkableCell(col, row);
}

float FarmWorld::worldWidth() const {
    return scene_ == GameScene::Farm ? static_cast<float>(kWorldW)
                                     : static_cast<float>(kRoomW);
}

float FarmWorld::worldHeight() const {
    return scene_ == GameScene::Farm ? static_cast<float>(kWorldH)
                                     : static_cast<float>(kRoomH);
}

float FarmWorld::fadeAlpha() const {
    if (transition_ == SceneTransition::None) {
        return 0.0f;
    }
    const float t = std::clamp(transitionT_, 0.0f, 1.0f);
    return t < 0.5f ? t * 2.0f : (1.0f - t) * 2.0f;
}

void FarmWorld::startTransition(SceneTransition kind) {
    if (transition_ != SceneTransition::None) {
        return;
    }
    transition_ = kind;
    transitionT_ = 0.0f;
    transitionSwitched_ = false;
    waypoints_.clear();
    waypointIndex_ = 0;
    moving_ = false;
    pending_ = Pending{};
    hover_ = Hover{};
}

void FarmWorld::skipToMorning() {
    // 从当前时刻跳到次日 7:00：作物按跳过的秒数一次性生长（过夜不再保水）。
    float skippedHours = 31.0f - hour(); // 24 - h + 7
    if (skippedHours < 0.0f) {
        skippedHours += 24.0f;
    }
    const float skipped = skippedHours * kDayLength / 24.0f;
    for (std::size_t i = 0; i < plots_.size(); ++i) {
        Crop& c = plots_[i];
        if (c.stage != CropStage::Seed && c.stage != CropStage::Sprout) {
            continue;
        }
        c.age += skipped;
        if (c.stage == CropStage::Seed && c.age >= 6.0f) {
            c.stage = CropStage::Sprout;
        }
        if (c.stage == CropStage::Sprout && c.age >= 14.0f) {
            c.stage = CropStage::Mature;
        }
        c.watered = false;
    }
    ++day_;
    dayTime_ = 7.0f * kDayLength / 24.0f;
    markFarmChanged();
}

void FarmWorld::applyTransitionSwitch() {
    switch (transition_) {
    case SceneTransition::ToCabin:
        scene_ = GameScene::Cabin;
        playerPos_ = cellCenterF(kCabinDoorCol, kCabinDoorRow);
        break;
    case SceneTransition::ToFarm:
        scene_ = GameScene::Farm;
        playerPos_ = cellCenterF(kCabinFrontCol, kCabinFrontRow);
        break;
    case SceneTransition::Sleep:
        skipToMorning();
        break;
    case SceneTransition::None:
        return;
    }
    waypoints_.clear();
    waypointIndex_ = 0;
    moving_ = false;
    hover_ = Hover{};
    cameraReady_ = false;
    updateCamera(0.0f, true);
}

void FarmWorld::setViewport(int width, int height) {
    viewW_ = static_cast<float>(std::max(1, width));
    viewH_ = static_cast<float>(std::max(1, height));
    updateCamera(0.0f, true);
}

void FarmWorld::update(float dt) {
    if (dt < 0.0f) {
        dt = 0.0f;
    }
    animTime_ += dt;
    updateParticles(dt);

    // 胜利后冻结世界，只保留粒子收尾，等待 R 重开。
    if (won_) {
        return;
    }
    playSeconds_ += dt;
    dayTime_ += dt;
    if (dayTime_ >= kDayLength) {
        dayTime_ -= kDayLength;
        ++day_;
    }
    updateCrops(dt);
    updateFishing(dt);

    // 转场：前半程渐黑，中点切场景/跳时间，后半程渐亮；期间角色冻结。
    if (transition_ != SceneTransition::None) {
        transitionT_ += dt;
        if (!transitionSwitched_ && transitionT_ >= 0.5f) {
            transitionSwitched_ = true;
            applyTransitionSwitch();
        }
        if (transitionT_ >= 1.0f) {
            transition_ = SceneTransition::None;
        }
        updateCamera(dt, false);
        return;
    }

    // 钓鱼进行中锁定移动，防止"边跑边钓"。
    const bool fishing = fishState_ != FishState::Idle;
    if (moving_ && !waypoints_.empty() && !fishing) {
        float remain = kPlayerSpeed * dt;
        while (remain > 0.0f && waypointIndex_ < waypoints_.size()) {
            const FPoint target = waypoints_[waypointIndex_];
            const float dx = target.x - playerPos_.x;
            const float dy = target.y - playerPos_.y;
            const float dist = std::sqrt(dx * dx + dy * dy);
            if (dist <= 0.001f) {
                playerPos_ = target;
                ++waypointIndex_;
                continue;
            }
            if (std::fabs(dx) > 0.5f) {
                facing_ = dx > 0.0f ? 1 : -1;
            }
            if (dist <= remain) {
                playerPos_ = target;
                remain -= dist;
                ++waypointIndex_;
            } else {
                const float step = remain / dist;
                playerPos_.x += dx * step;
                playerPos_.y += dy * step;
                remain = 0.0f;
            }
        }
        walkPhase_ += dt * 1.9f;
        if (waypointIndex_ >= waypoints_.size()) {
            // 最后一段落到格心，保证停住的位置永远是格心。
            playerPos_ = waypoints_.back();
            waypoints_.clear();
            waypointIndex_ = 0;
            moving_ = false;
        }
    }

    // 走到位：执行点击时记下的动作（播种/浇水/收获/抛竿）。
    if (!moving_ && pending_.kind != PendingKind::None) {
        onArrived();
    }

    updateCamera(dt, false);
}

void FarmWorld::updateCrops(float dt) {
    for (std::size_t i = 0; i < plots_.size(); ++i) {
        Crop& c = plots_[i];
        if (c.stage != CropStage::Seed && c.stage != CropStage::Sprout) {
            continue;
        }
        c.age += dt * (c.watered ? 1.6f : 1.0f);
        if (c.stage == CropStage::Seed && c.age >= 6.0f) {
            c.stage = CropStage::Sprout;
            c.watered = false;
            markFarmChanged();
            addParticles(ParticleType::Sparkle, plotCenter(static_cast<int>(i)), 3);
        } else if (c.stage == CropStage::Sprout && c.age >= 14.0f) {
            c.stage = CropStage::Mature;
            c.watered = false;
            markFarmChanged();
            addParticles(ParticleType::Sparkle, plotCenter(static_cast<int>(i)), 4);
        }
    }
}

void FarmWorld::updateFishing(float dt) {
    switch (fishState_) {
    case FishState::Idle:
        return;
    case FishState::Casting:
        fishTimer_ += dt;
        if (fishTimer_ >= kCastTime) {
            fishState_ = FishState::Waiting;
            fishTimer_ = 0.0f;
            rippleTimer_ = 0.0f;
            bobberPos_ = bobberTarget_; // 落水：漂就位（单帧大 dt 时也不能停在竿尖）
            addParticles(ParticleType::Droplet, bobberTarget_, 8);
            addParticles(ParticleType::Ring, bobberTarget_, 1);
        } else {
            // 抛竿：二次 Bezier 从竿尖推到落点，中段抬高。
            const float t = fishTimer_ / kCastTime;
            const FPoint ctrl{(bobberStart_.x + bobberTarget_.x) * 0.5f,
                              std::min(bobberStart_.y, bobberTarget_.y) - 70.0f};
            bobberPos_ = bezierPoint(bobberStart_, ctrl, bobberTarget_, t);
        }
        return;
    case FishState::Waiting:
        fishTimer_ += dt;
        bobberPos_ = {bobberTarget_.x,
                      bobberTarget_.y + std::sin(animTime_ * 3.0f) * 2.0f};
        rippleTimer_ += dt;
        if (rippleTimer_ >= 0.9f) {
            rippleTimer_ = 0.0f;
            addParticles(ParticleType::Ring, bobberTarget_, 1);
        }
        if (fishTimer_ >= fishWait_) {
            fishState_ = FishState::Biting;
            fishTimer_ = 0.0f;
            addParticles(ParticleType::Ring, bobberTarget_, 2);
        }
        return;
    case FishState::Biting:
        fishTimer_ += dt;
        bobberPos_ = {bobberTarget_.x, bobberTarget_.y + 5.0f};
        if (fishTimer_ >= kBiteWindow) {
            fishState_ = FishState::Missed;
            fishTimer_ = 0.0f;
        }
        return;
    case FishState::Caught:
    case FishState::Missed:
        fishTimer_ += dt;
        if (fishTimer_ >= kFishTailTime) {
            fishState_ = FishState::Idle;
        }
        return;
    }
}

void FarmWorld::updateParticles(float dt) {
    std::size_t write = 0;
    for (std::size_t i = 0; i < particles_.size(); ++i) {
        Particle& pt = particles_[i];
        pt.life += dt;
        if (pt.life >= pt.maxLife) {
            continue;
        }
        if (pt.type == ParticleType::Droplet) {
            pt.vel.y += 520.0f * dt;
        }
        pt.pos.x += pt.vel.x * dt;
        pt.pos.y += pt.vel.y * dt;
        particles_[write++] = pt;
    }
    particles_.resize(write);
}

void FarmWorld::onHover(FPoint worldPos) {
    hover_ = Hover{};
    if (worldPos.x < 0.0f || worldPos.y < 0.0f || worldPos.x >= worldWidth() ||
        worldPos.y >= worldHeight()) {
        return;
    }

    // 室内：床/地垫/普通地面三种悬停。
    if (scene_ == GameScene::Cabin) {
        const IPoint c = cellOf(worldPos);
        if (c.x >= 4 && c.x <= 7 && c.y >= 3 && c.y <= 8) {
            hover_.kind = HoverKind::Bed;
            hover_.cell = {kCabinBedSideCol, kCabinBedSideRow};
            return;
        }
        if (c.y >= 15 && c.y <= 18 && c.x >= 11 && c.x <= 13) {
            hover_.kind = HoverKind::Door;
            hover_.cell = {kCabinDoorCol, kCabinDoorRow};
            return;
        }
        if (walkable(c.x, c.y)) {
            hover_.kind = HoverKind::Cell;
            hover_.cell = c;
        }
        return;
    }

    // 农场：悬停小屋 → 进屋提示。
    if (worldPos.x >= kCabinX && worldPos.x < kCabinX + kCabinW &&
        worldPos.y >= kCabinY && worldPos.y < kCabinY + kCabinH + 24.0f) {
        hover_.kind = HoverKind::Door;
        hover_.cell = {kCabinFrontCol, kCabinFrontRow};
        return;
    }

    const int plot = plotIndexAtPoint(worldPos);
    if (plot >= 0) {
        const IPoint c = plotCenterCell(plot);
        if (walkable(c.x, c.y)) {
            hover_.kind = HoverKind::Plot;
            hover_.plot = plot;
            hover_.cell = c;
        }
        return;
    }

    if (inPond(worldPos.x, worldPos.y, 0.0f) || inDock(worldPos.x, worldPos.y)) {
        hover_.kind = HoverKind::Pond;
        hover_.cell = {kDockTipCol, kDockTipRow};
        return;
    }

    const IPoint c = cellOf(worldPos);
    if (walkable(c.x, c.y)) {
        hover_.kind = HoverKind::Cell;
        hover_.cell = c;
    }
}

void FarmWorld::clearHover() {
    hover_ = Hover{};
}

void FarmWorld::onClick(FPoint worldPos) {
    if (worldPos.x < 0.0f || worldPos.y < 0.0f || worldPos.x >= worldWidth() ||
        worldPos.y >= worldHeight() || won_ ||
        transition_ != SceneTransition::None) {
        return;
    }

    // 室内：点床睡觉、点地垫出门、其余走格。
    if (scene_ == GameScene::Cabin) {
        const IPoint c = cellOf(worldPos);
        if (c.x >= 4 && c.x <= 7 && c.y >= 3 && c.y <= 8) {
            pending_ = Pending{PendingKind::Sleep, -1};
            buildPath(cellOf(playerPos_), {kCabinBedSideCol, kCabinBedSideRow});
            if (!moving_) {
                onArrived();
            }
            return;
        }
        if (c.y >= 15 && c.y <= 18 && c.x >= 11 && c.x <= 13) {
            pending_ = Pending{PendingKind::Exit, -1};
            buildPath(cellOf(playerPos_), {kCabinDoorCol, kCabinDoorRow});
            if (!moving_) {
                onArrived();
            }
            return;
        }
        if (walkable(c.x, c.y)) {
            pending_ = Pending{};
            const IPoint start = cellOf(playerPos_);
            if (start.x == c.x && start.y == c.y) {
                return;
            }
            buildPath(start, c);
        }
        return;
    }

    // 农场：点小屋 → 走到门前进屋。
    if (worldPos.x >= kCabinX && worldPos.x < kCabinX + kCabinW &&
        worldPos.y >= kCabinY && worldPos.y < kCabinY + kCabinH + 24.0f) {
        if (fishState_ != FishState::Idle) {
            return;
        }
        pending_ = Pending{PendingKind::Enter, -1};
        const IPoint start = cellOf(playerPos_);
        if (start.x == kCabinFrontCol && start.y == kCabinFrontRow) {
            onArrived();
            return;
        }
        buildPath(start, {kCabinFrontCol, kCabinFrontRow});
        if (!moving_ && pending_.kind != PendingKind::None) {
            onArrived();
        }
        return;
    }

    // 钓鱼期间点击只用于收竿：咬钩窗口内点哪儿都算提竿——只认水面太苛刻，
    // 手一抖点到岸边或鱼漂外就跑鱼；等待/抛竿期间的点击仍忽略。
    if (fishState_ == FishState::Biting) {
        fishState_ = FishState::Caught;
        fishTimer_ = 0.0f;
        inventory_.fish++;
        addParticles(ParticleType::Droplet, bobberTarget_, 14);
        addParticles(ParticleType::Ring, bobberTarget_, 2);
        if (inventory_.meetsOrder()) {
            won_ = true;
        }
        return;
    }
    if (fishState_ != FishState::Idle) {
        return;
    }

    IPoint target{};
    const int plot = plotIndexAtPoint(worldPos);
    if (plot >= 0) {
        target = plotCenterCell(plot);
        hover_.kind = HoverKind::Plot;
        hover_.plot = plot;
        hover_.cell = target;
    } else if (inPond(worldPos.x, worldPos.y, 0.0f)) {
        // 点水面自动走到栈道最里面，准备钓鱼。
        target = {kDockTipCol, kDockTipRow};
        hover_.kind = HoverKind::Pond;
        hover_.plot = -1;
        hover_.cell = target;
    } else {
        target = cellOf(worldPos);
        if (inDock(worldPos.x, worldPos.y) && inDockCell(target.x, target.y)) {
            target = {kDockTipCol, kDockTipRow};
            hover_.kind = HoverKind::Pond;
            hover_.cell = target;
        } else if (walkable(target.x, target.y)) {
            hover_.kind = HoverKind::Cell;
            hover_.plot = -1;
            hover_.cell = target;
        } else {
            hover_ = Hover{};
            return;
        }
    }

    if (!walkable(target.x, target.y)) {
        hover_ = Hover{};
        return;
    }

    const IPoint start = cellOf(playerPos_);
    // 已经站在目标格上：直接执行动作，不再绕一圈路径。
    if (start.x == target.x && start.y == target.y) {
        if (plot >= 0) {
            pending_ = Pending{PendingKind::Plot, plot};
        } else if (hover_.kind == HoverKind::Pond) {
            pending_ = Pending{PendingKind::Pond, -1};
        } else {
            return;
        }
        onArrived();
        return;
    }

    if (plot >= 0) {
        pending_ = Pending{PendingKind::Plot, plot};
    } else if (hover_.kind == HoverKind::Pond) {
        pending_ = Pending{PendingKind::Pond, -1};
    } else {
        pending_ = Pending{};
    }
    buildPath(start, target);
    if (!moving_ && pending_.kind != PendingKind::None) {
        onArrived();
    }
}

void FarmWorld::onArrived() {
    const Pending p = pending_;
    pending_ = Pending{};
    switch (p.kind) {
    case PendingKind::Plot:
        interactPlot(p.plot);
        break;
    case PendingKind::Pond:
        // 只有站上栈道尽头才抛竿。
        if (cellOf(playerPos_).x == kDockTipCol && cellOf(playerPos_).y == kDockTipRow) {
            startCasting();
        }
        break;
    case PendingKind::Enter:
        startTransition(SceneTransition::ToCabin);
        break;
    case PendingKind::Sleep:
        startTransition(SceneTransition::Sleep);
        break;
    case PendingKind::Exit:
        startTransition(SceneTransition::ToFarm);
        break;
    case PendingKind::None:
        break;
    }
}

void FarmWorld::interactPlot(int plotIndex) {
    if (plotIndex < 0 || plotIndex >= static_cast<int>(plots_.size())) {
        return;
    }
    Crop& c = plots_[static_cast<std::size_t>(plotIndex)];
    const FPoint center = plotCenter(plotIndex);
    switch (c.stage) {
    case CropStage::Empty:
        c.kind = selectedSeed_;
        c.stage = CropStage::Seed;
        c.age = 0.0f;
        c.watered = false;
        markFarmChanged();
        addParticles(ParticleType::Sparkle, center, 5);
        break;
    case CropStage::Seed:
    case CropStage::Sprout:
        if (!c.watered) {
            c.watered = true;
            markFarmChanged();
            addParticles(ParticleType::Droplet, center, 10);
            addParticles(ParticleType::Ring, center, 1);
        }
        break;
    case CropStage::Mature:
        inventory_.slot(c.kind)++;
        c.stage = CropStage::Empty;
        c.age = 0.0f;
        c.watered = false;
        markFarmChanged();
        addParticles(ParticleType::Sparkle, center, 12);
        if (inventory_.meetsOrder()) {
            won_ = true;
        }
        break;
    }
}

void FarmWorld::startCasting() {
    fishState_ = FishState::Casting;
    fishTimer_ = 0.0f;
    bobberStart_ = {playerPos_.x, playerPos_.y - 42.0f};
    // 落点在池塘内部随机取（避开岸边一圈），种子来自确定性 LCG，冒烟测试可复现。
    const std::uint32_t r = nextRand();
    const float a = static_cast<float>(r % 360u) * kPi / 180.0f;
    const float rr = 0.25f + static_cast<float>((r >> 9) % 1000u) / 1000.0f * 0.45f;
    bobberTarget_ = {kPondCx + kPondRx * 0.7f * rr * std::cos(a),
                     kPondCy + kPondRy * 0.7f * rr * std::sin(a)};
    bobberCtrl_ = {(bobberStart_.x + bobberTarget_.x) * 0.5f,
                   std::min(bobberStart_.y, bobberTarget_.y) - 70.0f};
    bobberPos_ = bobberStart_;
    const std::uint32_t w = nextRand();
    fishWait_ = 1.2f + static_cast<float>(w % 1000u) / 1000.0f * 1.6f;
    // 抛竿后到收竿前都不再走路。
    waypoints_.clear();
    waypointIndex_ = 0;
    moving_ = false;
}

void FarmWorld::addParticles(ParticleType type, FPoint pos, int count) {
    for (int i = 0; i < count; ++i) {
        if (particles_.size() >= kMaxParticles) {
            particles_.erase(particles_.begin());
        }
        Particle pt{};
        pt.pos = pos;
        pt.type = type;
        switch (type) {
        case ParticleType::Droplet: {
            const float a = static_cast<float>(nextRand() % 628u) / 100.0f;
            const float sp = 40.0f + static_cast<float>(nextRand() % 90u);
            pt.vel = {std::cos(a) * sp, -std::fabs(std::sin(a)) * sp - 60.0f};
            pt.maxLife = 0.45f + static_cast<float>(nextRand() % 20u) / 100.0f;
            break;
        }
        case ParticleType::Ring:
            pt.vel = {0.0f, 0.0f};
            pt.maxLife = 0.6f;
            break;
        case ParticleType::Sparkle: {
            const float a = static_cast<float>(nextRand() % 628u) / 100.0f;
            const float sp = 20.0f + static_cast<float>(nextRand() % 50u);
            pt.vel = {std::cos(a) * sp, std::sin(a) * sp - 40.0f};
            pt.maxLife = 0.5f + static_cast<float>(nextRand() % 25u) / 100.0f;
            break;
        }
        }
        pt.life = 0.0f;
        particles_.push_back(pt);
    }
}

std::uint32_t FarmWorld::nextRand() {
    fishRng_ = fishRng_ * 1664525u + 1013904223u;
    return fishRng_ >> 8;
}

void FarmWorld::cancelMove() {
    waypoints_.clear();
    waypointIndex_ = 0;
    moving_ = false;
    pending_ = Pending{};
}

void FarmWorld::selectSeed(int index) {
    if (index >= 0 && index < 3) {
        selectedSeed_ = static_cast<Seed>(index);
    }
}

bool FarmWorld::cellsClearH(int row, int fromCol, int toCol) const {
    const int step = toCol >= fromCol ? 1 : -1;
    for (int c = fromCol;; c += step) {
        if (!walkable(c, row)) {
            return false;
        }
        if (c == toCol) {
            break;
        }
    }
    return true;
}

bool FarmWorld::cellsClearV(int col, int fromRow, int toRow) const {
    const int step = toRow >= fromRow ? 1 : -1;
    for (int r = fromRow;; r += step) {
        if (!walkable(col, r)) {
            return false;
        }
        if (r == toRow) {
            break;
        }
    }
    return true;
}

bool FarmWorld::tryLPath(IPoint from, IPoint to, bool horizontalFirst) {
    if (horizontalFirst) {
        if (!cellsClearH(from.y, from.x, to.x)) {
            return false;
        }
        if (!cellsClearV(to.x, from.y, to.y)) {
            return false;
        }
        const IPoint corner{to.x, from.y};
        if (corner.x != from.x || corner.y != from.y) {
            waypoints_.push_back(cellCenterF(corner.x, corner.y));
        }
        waypoints_.push_back(cellCenterF(to.x, to.y));
        return true;
    }

    if (!cellsClearV(from.x, from.y, to.y)) {
        return false;
    }
    if (!cellsClearH(to.y, from.x, to.x)) {
        return false;
    }
    const IPoint corner{from.x, to.y};
    if (corner.x != from.x || corner.y != from.y) {
        waypoints_.push_back(cellCenterF(corner.x, corner.y));
    }
    waypoints_.push_back(cellCenterF(to.x, to.y));
    return true;
}

bool FarmWorld::tryBfsPath(IPoint from, IPoint to) {
    constexpr int kCells = kCols * kRows;
    std::array<int, kCells> prev{};
    prev.fill(-1);

    auto indexOf = [](IPoint c) { return c.y * kCols + c.x; };
    std::queue<IPoint> queue;
    const IPoint startIndex = from;
    prev[static_cast<std::size_t>(indexOf(startIndex))] = indexOf(startIndex);
    queue.push(startIndex);

    const int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    while (!queue.empty()) {
        const IPoint cur = queue.front();
        queue.pop();
        if (cur.x == to.x && cur.y == to.y) {
            break;
        }
        for (const auto& d : dirs) {
            const IPoint next{cur.x + d[0], cur.y + d[1]};
            if (!walkable(next.x, next.y)) {
                continue;
            }
            const int ni = indexOf(next);
            if (prev[static_cast<std::size_t>(ni)] != -1) {
                continue;
            }
            prev[static_cast<std::size_t>(ni)] = indexOf(cur);
            queue.push(next);
        }
    }

    if (prev[static_cast<std::size_t>(indexOf(to))] == -1) {
        return false;
    }

    std::vector<IPoint> cells;
    IPoint cur = to;
    while (cur.x != from.x || cur.y != from.y) {
        cells.push_back(cur);
        const int p = prev[static_cast<std::size_t>(cur.y * kCols + cur.x)];
        cur = {p % kCols, p / kCols};
    }
    cells.push_back(from);
    std::reverse(cells.begin(), cells.end());

    // 只保留拐点，避免角色每格都"走一步停一下"。
    std::vector<IPoint> corners;
    corners.push_back(cells.front());
    for (std::size_t i = 1; i + 1 < cells.size(); ++i) {
        const IPoint d0{cells[i].x - cells[i - 1].x, cells[i].y - cells[i - 1].y};
        const IPoint d1{cells[i + 1].x - cells[i].x, cells[i + 1].y - cells[i].y};
        if (d0.x != d1.x || d0.y != d1.y) {
            corners.push_back(cells[i]);
        }
    }
    corners.push_back(cells.back());
    for (std::size_t i = 1; i < corners.size(); ++i) {
        waypoints_.push_back(cellCenterF(corners[i].x, corners[i].y));
    }
    return true;
}

void FarmWorld::buildPath(IPoint start, IPoint goalCell) {
    waypoints_.clear();
    waypointIndex_ = 0;
    moving_ = false;

    if (!walkable(goalCell.x, goalCell.y)) {
        return;
    }

    const FPoint startCenter = cellCenterF(start.x, start.y);
    const bool needCenter = distanceSq(playerPos_, startCenter) > 0.25f;
    if (needCenter) {
        waypoints_.push_back(startCenter);
    }

    if (start.x == goalCell.x && start.y == goalCell.y) {
        moving_ = !waypoints_.empty();
        return;
    }

    if (!tryLPath(start, goalCell, true) && !tryLPath(start, goalCell, false)) {
        waypoints_.clear();
        if (needCenter) {
            waypoints_.push_back(startCenter);
        }
        if (!tryBfsPath(start, goalCell)) {
            waypoints_.clear();
            return;
        }
    }

    if (!waypoints_.empty()) {
        waypointIndex_ = 0;
        moving_ = true;
    }
}

void FarmWorld::updateCamera(float dt, bool snap) {
    const float maxX = std::max(0.0f, worldWidth() - viewW_);
    const float maxY = std::max(0.0f, worldHeight() - viewH_);
    const float targetX =
        std::clamp(playerPos_.x - viewW_ * 0.5f, 0.0f, maxX);
    const float targetY =
        std::clamp(playerPos_.y - viewH_ * 0.5f, 0.0f, maxY);

    if (snap || !cameraReady_) {
        camera_ = {targetX, targetY};
        cameraReady_ = true;
        return;
    }

    const float k = 1.0f - std::exp(-9.0f * dt);
    camera_.x += (targetX - camera_.x) * k;
    camera_.y += (targetY - camera_.y) * k;
    camera_.x = std::clamp(camera_.x, 0.0f, maxX);
    camera_.y = std::clamp(camera_.y, 0.0f, maxY);
}

void FarmWorld::debugPlant(int plotIndex, Seed kind, CropStage stage, bool watered) {
    if (plotIndex < 0 || plotIndex >= static_cast<int>(plots_.size())) {
        return;
    }
    Crop& c = plots_[static_cast<std::size_t>(plotIndex)];
    c.kind = kind;
    c.stage = stage;
    c.watered = watered;
    c.age = 0.0f;
    markFarmChanged();
}

void FarmWorld::debugSetPlayerCell(int col, int row) {
    playerPos_ = cellCenterF(col, row);
    waypoints_.clear();
    waypointIndex_ = 0;
    moving_ = false;
    updateCamera(0.0f, true);
}

void FarmWorld::debugSetTime(float hour) {
    hour = std::fmod(hour + 24.0f, 24.0f);
    dayTime_ = hour * kDayLength / 24.0f;
}

void FarmWorld::debugEnterCabin() {
    scene_ = GameScene::Cabin;
    transition_ = SceneTransition::None;
    playerPos_ = cellCenterF(kCabinDoorCol, kCabinDoorRow);
    waypoints_.clear();
    waypointIndex_ = 0;
    moving_ = false;
    pending_ = Pending{};
    hover_ = Hover{};
    cameraReady_ = false;
    updateCamera(0.0f, true);
}

void FarmWorld::debugAddInventory(int kind, int count) {
    if (kind == 3) {
        inventory_.fish += count;
    } else {
        inventory_.slot(static_cast<Seed>(kind)) += count;
    }
    if (inventory_.meetsOrder()) {
        won_ = true;
    }
}

} // namespace cg::game
