// FarmWorld.cpp
// 实现格点移动：优先“先横后竖”的 L 形路径，被池塘挡住时回退到 BFS；
// 镜头始终尽量把角色放在屏幕中央，到世界边缘才让角色偏离中心。
#include "game/FarmWorld.hpp"

#include <algorithm>
#include <cmath>
#include <queue>

namespace cg::game {
namespace {

float distanceSq(FPoint a, FPoint b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return dx * dx + dy * dy;
}

} // namespace

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
    selectedSeed_ = Seed::Tea;
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

    if (moving_ && !waypoints_.empty()) {
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

    updateCamera(dt, false);
}

void FarmWorld::onHover(FPoint worldPos) {
    hover_ = Hover{};
    if (worldPos.x < 0.0f || worldPos.y < 0.0f || worldPos.x >= kWorldW ||
        worldPos.y >= kWorldH) {
        return;
    }

    const int plot = plotIndexAtPoint(worldPos);
    if (plot >= 0) {
        const IPoint c = plotCenterCell(plot);
        if (isWalkableCell(c.x, c.y)) {
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
    if (isWalkableCell(c.x, c.y)) {
        hover_.kind = HoverKind::Cell;
        hover_.cell = c;
    }
}

void FarmWorld::clearHover() {
    hover_ = Hover{};
}

void FarmWorld::onClick(FPoint worldPos) {
    if (worldPos.x < 0.0f || worldPos.y < 0.0f || worldPos.x >= kWorldW ||
        worldPos.y >= kWorldH) {
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
        } else if (isWalkableCell(target.x, target.y)) {
            hover_.kind = HoverKind::Cell;
            hover_.plot = -1;
            hover_.cell = target;
        } else {
            hover_ = Hover{};
            return;
        }
    }

    if (!isWalkableCell(target.x, target.y)) {
        hover_ = Hover{};
        return;
    }

    const IPoint start = cellOf(playerPos_);
    buildPath(start, target);
}

void FarmWorld::cancelMove() {
    waypoints_.clear();
    waypointIndex_ = 0;
    moving_ = false;
}

void FarmWorld::selectSeed(int index) {
    if (index >= 0 && index < 3) {
        selectedSeed_ = static_cast<Seed>(index);
    }
}

bool FarmWorld::cellsClearH(int row, int fromCol, int toCol) const {
    const int step = toCol >= fromCol ? 1 : -1;
    for (int c = fromCol;; c += step) {
        if (!isWalkableCell(c, row)) {
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
        if (!isWalkableCell(col, r)) {
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
            if (!isWalkableCell(next.x, next.y)) {
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
        const int p = prev[static_cast<std::size_t>(indexOf(cur))];
        cur = {p % kCols, p / kCols};
    }
    cells.push_back(from);
    std::reverse(cells.begin(), cells.end());

    // 只保留拐点，避免角色每格都“走一步停一下”。
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

    if (!isWalkableCell(goalCell.x, goalCell.y)) {
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
    const float maxX = std::max(0.0f, static_cast<float>(kWorldW) - viewW_);
    const float maxY = std::max(0.0f, static_cast<float>(kWorldH) - viewH_);
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
}

void FarmWorld::debugSetPlayerCell(int col, int row) {
    playerPos_ = cellCenterF(col, row);
    waypoints_.clear();
    waypointIndex_ = 0;
    moving_ = false;
    updateCamera(0.0f, true);
}

} // namespace cg::game
