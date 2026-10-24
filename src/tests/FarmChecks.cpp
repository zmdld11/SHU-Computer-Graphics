// FarmChecks.cpp
// 断言农场世界的格点布局、行走路径、镜头夹取、玩法状态机与烘培一致性；
// 不创建窗口，可直接在 CI/命令行跑。
#include "tests/FarmChecks.hpp"

#include "core/FrameBuffer.hpp"
#include "core/ImageIO.hpp"
#include "game/FarmLayout.hpp"
#include "game/FarmRender.hpp"
#include "game/FarmWorld.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>

namespace cg::tests {
namespace {

bool near(float a, float b) {
    return std::fabs(a - b) < 0.01f;
}

bool nearPoint(FPoint a, FPoint b) {
    return near(a.x, b.x) && near(a.y, b.y);
}

} // namespace

int runFarmChecks() {
    using namespace cg::game;
    int fails = 0;
    auto expect = [&](bool ok, const char* name) {
        if (!ok) {
            ++fails;
            std::cout << "[FAIL] " << name << std::endl;
        }
    };

    FarmWorld world;
    world.setViewport(800, 600);

    // 布局：出生点、池塘、栈道、世界边界
    expect(nearPoint(world.playerPos(), cellCenterF(kSpawnCol, kSpawnRow)),
           "farm: 出生在格心");
    expect(isWalkableCell(kSpawnCol, kSpawnRow), "farm: 出生格可行走");
    expect(!isWalkableCell(35, 18), "farm: 池塘中心不可行走");
    expect(isWalkableCell(kDockTipCol, kDockTipRow), "farm: 栈道尽头可行走");
    expect(!isWalkableCell(2, 2), "farm: 世界边界外不可行走");
    expect(plotIndexAtCell(kFarmCol + 1, kFarmRow + 1) == 0, "farm: 左上田块索引");

    // 点地面：先横后竖的 L 形路径，终点是格心
    world.debugSetPlayerCell(kSpawnCol, kSpawnRow);
    world.onClick({24 * kCell + 16.0f, 23 * kCell + 16.0f});
    expect(!world.waypoints().empty(), "farm: 点击后生成路径");
    expect(world.waypoints().size() >= 2, "farm: L 形路径有拐点");
    expect(near(world.waypoints()[0].x, 24 * kCell + 16.0f) &&
               near(world.waypoints()[0].y, kSpawnRow * kCell + 16.0f),
           "farm: 先横移再竖移");
    expect(nearPoint(world.goal(), cellCenterF(24, 23)), "farm: 目标为格心");
    world.update(1.2f);
    expect(nearPoint(world.playerPos(), cellCenterF(24, 23)), "farm: 到点后停在格心");
    expect(!world.moving(), "farm: 到点后停止");

    // 点池塘：自动把目标换成栈道尽头
    world.debugSetPlayerCell(kSpawnCol, kSpawnRow);
    world.onClick({kPondCx, kPondCy + 80.0f});
    expect(nearPoint(world.goal(), cellCenterF(kDockTipCol, kDockTipRow)),
           "farm: 点水面目标为栈道尽头");
    expect(world.hover().kind == HoverKind::Pond, "farm: 水面悬停提示");

    // 从池塘上方点水：L 形被水挡住，必须 BFS 绕行，且每个拐点都可走
    world.debugSetPlayerCell(35, 10);
    world.onClick({kPondCx, kPondCy + 80.0f});
    expect(!world.waypoints().empty(), "farm: 被池塘挡住时仍能找到路");
    bool allWalkable = true;
    for (const FPoint& p : world.waypoints()) {
        const IPoint c = cellOf(p);
        if (!isWalkableCell(c.x, c.y)) {
            allWalkable = false;
        }
    }
    expect(allWalkable, "farm: BFS 路径不穿水/穿墙");

    // 点农田：目标为田块中心
    world.debugSetPlayerCell(kSpawnCol, kSpawnRow);
    world.onClick(plotCenter(5));
    expect(nearPoint(world.goal(), plotCenter(5)), "farm: 点农田走到田块中心");

    // 镜头：角色在中间跟随，到四角夹在世界边界
    world.debugSetPlayerCell(6, 6);
    expect(near(world.camera().x, 0.0f) && near(world.camera().y, 0.0f),
           "farm: 镜头左上夹取");
    world.debugSetPlayerCell(41, 29);
    expect(near(world.camera().x, static_cast<float>(kWorldW - 800)) &&
               near(world.camera().y, static_cast<float>(kWorldH - 600)),
           "farm: 镜头右下夹取");

    // 重开：农田、选种、角色全部复位
    world.debugPlant(0, Seed::Tomato, CropStage::Mature, true);
    world.selectSeed(2);
    world.reset();
    world.setViewport(800, 600);
    expect(world.crop(0).stage == CropStage::Empty, "farm: 重开清空农田");
    expect(world.selectedSeed() == Seed::Cabbage, "farm: 重开复位选种");
    expect(nearPoint(world.playerPos(), cellCenterF(kSpawnCol, kSpawnRow)),
           "farm: 重开回到出生点");

    // ---- 核心玩法（#70）----
    // 测试用小步推进：状态机一次 update 只跨一个状态，游戏主循环 dt≤0.05 无此问题。
    auto runFor = [&world](float seconds) {
        for (float t = 0.0f; t < seconds; t += 0.05f) {
            world.update(std::min(0.05f, seconds - t));
        }
    };

    // 站在田上点田：空地播种 → 浇水 → 生长 → 收获入库
    const IPoint plot0 = plotCenterCell(0);
    world.debugSetPlayerCell(plot0.x, plot0.y);
    world.onClick(plotCenter(0));
    expect(world.crop(0).stage == CropStage::Seed, "game: 点空田播种");
    world.onClick(plotCenter(0));
    expect(world.crop(0).watered, "game: 点幼苗浇水");
    world.onClick(plotCenter(0));
    expect(world.crop(0).watered, "game: 同阶段重复浇水不叠加");
    world.update(6.5f);
    expect(world.crop(0).stage == CropStage::Sprout, "game: 6 秒进幼苗期");
    expect(!world.crop(0).watered, "game: 进阶后重置浇水");
    expect(!world.particles().empty(), "game: 进阶冒出新芽闪光");
    world.update(13.0f);
    expect(world.crop(0).stage == CropStage::Mature, "game: 浇水 1.6x 提速到成熟");
    const int cabbageBefore = world.inventory().cabbage;
    world.onClick(plotCenter(0));
    expect(world.crop(0).stage == CropStage::Empty, "game: 成熟收获清空地块");
    expect(world.inventory().cabbage == cabbageBefore + 1, "game: 收获入库");

    // 不走到位不执行：站着点远处田块，只是移动，不播种
    world.debugSetPlayerCell(kSpawnCol, kSpawnRow);
    world.onClick(plotCenter(1));
    expect(world.crop(1).stage == CropStage::Empty && !world.waypoints().empty(),
           "game: 未到位不执行动作");

    // 钓鱼状态机：抛竿 → 等待 → 咬钩 → 超时脱钩
    world.debugSetPlayerCell(kDockTipCol, kDockTipRow);
    world.onClick({kPondCx, kPondCy + 80.0f});
    expect(world.fishState() == FishState::Casting, "game: 到栈道尽头即抛竿");
    runFor(0.6f);
    expect(world.fishState() == FishState::Waiting, "game: 0.5 秒抛竿后等待");
    expect(world.fishWaitSeconds() >= 1.2f && world.fishWaitSeconds() <= 2.8f,
           "game: 等待时长在 1.2~2.8 秒");
    runFor(world.fishWaitSeconds() + 0.1f);
    expect(world.fishState() == FishState::Biting, "game: 等待结束进入咬钩");
    runFor(1.0f);
    expect(world.fishState() == FishState::Missed, "game: 0.8 秒窗口超时判脱钩");
    runFor(1.1f);
    expect(world.fishState() == FishState::Idle, "game: 脱钩 1 秒后回空闲");

    // 咬钩窗口内点水面：钓到，鱼 +1
    world.onClick({kPondCx, kPondCy + 80.0f});
    runFor(0.6f + world.fishWaitSeconds() + 0.1f);
    expect(world.fishState() == FishState::Biting, "game: 再次进入咬钩");
    const int fishBefore = world.inventory().fish;
    world.onClick(world.bobberPos());
    expect(world.fishState() == FishState::Caught, "game: 窗口内点水面收竿");
    expect(world.inventory().fish == fishBefore + 1, "game: 钓到鱼 +1");
    runFor(1.1f);

    // 咬钩窗口内点远处（不在池塘）也判提竿：点偏不该跑鱼
    world.onClick({kPondCx, kPondCy + 80.0f});
    runFor(0.6f + world.fishWaitSeconds() + 0.1f);
    expect(world.fishState() == FishState::Biting, "game: 第三次进入咬钩");
    world.onClick(plotCenter(0));
    expect(world.fishState() == FishState::Caught, "game: 窗口内点远处也提竿");
    expect(world.inventory().fish == fishBefore + 2, "game: 远处提竿同样鱼 +1");
    runFor(1.1f);

    // 钓鱼期间锁移动
    world.onClick({kPondCx, kPondCy + 80.0f});
    expect(world.fishState() == FishState::Casting, "game: 收竿后可再抛");
    world.onClick(plotCenter(3));
    expect(world.waypoints().empty(), "game: 钓鱼期间点田不移动");
    runFor(6.0f);
    expect(world.fishState() == FishState::Idle, "game: 一轮结束回空闲");

    // 粒子：交互生成、寿命结束回收、数量有上限
    world.reset();
    world.setViewport(800, 600);
    world.debugSetPlayerCell(plotCenterCell(1).x, plotCenterCell(1).y);
    world.debugPlant(1, Seed::Cabbage, CropStage::Mature);
    for (int i = 0; i < 40; ++i) {
        world.debugPlant(1, Seed::Cabbage, CropStage::Mature);
        world.onClick(plotCenter(1));
    }
    expect(!world.particles().empty(), "game: 交互产生粒子");
    expect(world.particles().size() <= FarmWorld::kMaxParticles,
           "game: 粒子池不超上限");
    world.update(2.0f);
    expect(world.particles().empty(), "game: 粒子寿命结束回收");

    // 订单与胜利
    expect(!world.won(), "game: 开局未胜利");
    world.debugAddInventory(0, kOrderCabbage);
    expect(!world.won(), "game: 只齐白菜不算胜利");
    world.debugAddInventory(1, kOrderTomato);
    world.debugAddInventory(2, kOrderCarrot);
    expect(!world.won(), "game: 缺鱼不算胜利");
    world.debugAddInventory(3, kOrderFish);
    expect(world.won(), "game: 四项集齐判胜利");
    world.update(0.5f);
    expect(world.won(), "game: 胜利后世界冻结");

    // 分层烘培一致性（结构性校验）：静态层 + 农田层与整图 drawScene 的差异
    // 必须全部落在农田重烘矩形内且数量极少。AET 增量累加的交点 x 依赖画布
    // 绝对量级，边缘 ±1 子像素翻转（降采样后 ≤2/255）无法消除也不可见；
    // 但树冠/草丛跨界被章盖掉这类结构性错误会产生大量差异，必须在此拦住。
    {
        FarmWorld w2;
        w2.setViewport(800, 600);
        w2.debugPlant(0, Seed::Tomato, CropStage::Mature, true);
        w2.debugPlant(5, Seed::Cabbage, CropStage::Sprout, false);
        FarmRender render;
        render.build(w2);
        const FrameBuffer baked = render.captureBackground();
        const FrameBuffer full = render.captureScene(w2);
        int diff = 0;
        bool outsideBakeRect = false;
        for (int y = 0; y < baked.height(); ++y) {
            for (int x = 0; x < baked.width(); ++x) {
                if (baked.pixel(x, y) != full.pixel(x, y)) {
                    ++diff;
                    if (x < kFarmBakeX || x >= kFarmBakeX + kFarmBakeW ||
                        y < kFarmBakeY || y >= kFarmBakeY + kFarmBakeH) {
                        outsideBakeRect = true;
                    }
                }
            }
        }
        expect(!outsideBakeRect, "game: 烘培差异不越出农田重烘矩形");
        expect(diff <= 48, "game: 烘培差异仅限边缘子像素");
    }

    // ---- 昼夜系统（#71）----
    // 太阳：6:00-18:00 在天上，正午最高；影子方向自西经南扫到东，晨昏最长。
    // 双光源重叠窗口：清晨 6:00-6:30 与黄昏 17:30-18:00。
    expect(!skyAt(3.0f).sunUp && skyAt(3.0f).lampIntensity > 0.99f,
           "day: 凌晨只有路灯");
    expect(skyAt(6.2f).sunUp && skyAt(6.2f).lampIntensity > 0.0f,
           "day: 清晨太阳与路灯同时生效");
    expect(skyAt(12.5f).sunElevation > 0.98f && skyAt(12.5f).lampIntensity == 0.0f,
           "day: 正午只有太阳");
    expect(skyAt(17.8f).sunUp && skyAt(17.8f).lampIntensity > 0.0f,
           "day: 黄昏双光源重叠");
    expect(!skyAt(21.0f).sunUp && skyAt(21.0f).lampIntensity > 0.99f,
           "day: 夜里只有路灯");
    const FPoint morning = sunShadowVec(7.0f);
    const FPoint noon = sunShadowVec(12.0f);
    const FPoint evening = sunShadowVec(17.0f);
    expect(morning.x < -0.8f, "day: 清晨影子朝西");
    expect(std::fabs(noon.x) < 0.3f, "day: 正午影子朝南");
    expect(evening.x > 0.8f, "day: 傍晚影子朝东");
    const float lenMorning = std::sqrt(morning.x * morning.x + morning.y * morning.y);
    const float lenNoon = std::sqrt(noon.x * noon.x + noon.y * noon.y);
    expect(lenMorning > lenNoon * 2.0f, "day: 晨昏影子比正午长一倍以上");
    AmbientTint blue;
    AmbientTint warm;
    ambientTintAt(23.0f, blue, warm);
    expect(blue.alpha > 90 && warm.alpha == 0, "day: 深夜只剩夜蓝叠加");
    ambientTintAt(18.3f, blue, warm);
    expect(blue.alpha > 0 && warm.alpha > 0, "day: 黄昏蓝橙两层叠加");

    // 时间轴：随 update 推进，跨零点天数 +1；胜利后冻结。
    world.reset();
    world.setViewport(800, 600);
    world.debugSetTime(23.9f);
    const float h0 = world.hour();
    world.update(0.2f * FarmWorld::kDayLength / 24.0f); // 推进 0.2 小时
    expect(world.hour() > h0 || world.hour() < 1.0f, "day: 时刻随帧推进");
    world.debugSetTime(23.99f);
    const int dayBefore = world.day();
    for (int i = 0; i < 40; ++i) {
        world.update(0.05f);
    }
    expect(world.day() == dayBefore + 1, "day: 跨零点进入新的一天");
    world.debugAddInventory(0, kOrderCabbage);
    world.debugAddInventory(1, kOrderTomato);
    world.debugAddInventory(2, kOrderCarrot);
    world.debugAddInventory(3, kOrderFish);
    expect(world.won(), "day: 铺垫胜利");
    world.debugSetTime(12.0f);
    const float frozenHour = world.hour();
    world.update(1.0f);
    expect(world.hour() == frozenHour, "day: 胜利后时间冻结");
    world.reset();
    world.setViewport(800, 600);

    // ---- 场景切换（#72）----
    expect(cabinWalkableCell(kCabinDoorCol, kCabinDoorRow), "cabin: 门垫可行走");
    expect(!cabinWalkableCell(5, 5), "cabin: 床位不可穿行");
    expect(!cabinWalkableCell(0, 8), "cabin: 侧墙不可穿行");

    // 点小屋 → 走到门前格 → 转场进室内，落在门垫上
    world.debugSetPlayerCell(kSpawnCol, kSpawnRow);
    world.onClick({kCabinDoorX, kCabinDoorY + 10.0f});
    expect(world.pending().kind == PendingKind::Enter, "cabin: 点小屋记进门意图");
    expect(nearPoint(world.goal(), cellCenterF(kCabinFrontCol, kCabinFrontRow)),
           "cabin: 目标为门前格");
    runFor(5.0f);
    expect(world.scene() == GameScene::Cabin || world.fadeAlpha() > 0.0f,
           "cabin: 到位触发转场");
    runFor(1.2f);
    expect(world.scene() == GameScene::Cabin, "cabin: 转场后进入室内");
    expect(nearPoint(world.playerPos(), cellCenterF(kCabinDoorCol, kCabinDoorRow)),
           "cabin: 进屋落在门垫");

    // 室内路径不穿家具
    world.onClick({20 * kCell + 16.0f, 13 * kCell + 16.0f});
    bool cabinPathOk = !world.waypoints().empty();
    for (const FPoint& p : world.waypoints()) {
        const IPoint c = cellOf(p);
        if (!cabinWalkableCell(c.x, c.y)) {
            cabinPathOk = false;
        }
    }
    expect(cabinPathOk, "cabin: 室内路径不穿家具");
    runFor(3.0f);

    // 点床睡觉：转场后次日 7:00，作物按跳过的秒数长了一夜
    world.debugPlant(0, Seed::Tomato, CropStage::Seed, false);
    world.debugSetTime(22.0f);
    world.onClick({5 * kCell + 16.0f, 5 * kCell + 16.0f}); // 床 now 在左侧 4-7 列
    expect(world.pending().kind == PendingKind::Sleep, "cabin: 点床记睡觉意图");
    runFor(4.5f);
    expect(world.day() == 2, "cabin: 睡醒进入第二天");
    expect(world.hour() > 6.9f && world.hour() < 7.5f, "cabin: 醒来是早晨 7 点");
    expect(world.crop(0).stage == CropStage::Mature, "cabin: 作物长了一夜成熟");

    // 点地垫出门：回到农场门前格
    world.onClick({kCabinDoorCol * kCell + 16.0f, kCabinDoorRow * kCell + 16.0f});
    expect(world.pending().kind == PendingKind::Exit, "cabin: 点地垫记出门意图");
    runFor(5.0f);
    expect(world.scene() == GameScene::Farm, "cabin: 出门回到农场");
    expect(nearPoint(world.playerPos(), cellCenterF(kCabinFrontCol, kCabinFrontRow)),
           "cabin: 出门落在屋前格");
    world.reset();
    world.setViewport(800, 600);

    std::cout << (fails == 0 ? "farm checks passed" : "farm checks FAILED") << std::endl;
    return fails;
}

} // namespace cg::tests
