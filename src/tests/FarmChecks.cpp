// FarmChecks.cpp
// 断言农场世界的格点布局、行走路径、镜头夹取和重置；不创建窗口，可直接在 CI/命令行跑。
#include "tests/FarmChecks.hpp"

#include "game/FarmLayout.hpp"
#include "game/FarmWorld.hpp"

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

    std::cout << (fails == 0 ? "farm checks passed" : "farm checks FAILED") << std::endl;
    return fails;
}

} // namespace cg::tests
