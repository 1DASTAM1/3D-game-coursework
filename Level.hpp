#pragma once
#include "GpuRenderer.hpp"
#include <memory>

class LevelMap : public DoorMap {
public:
    int switchX = -1, switchY = -1;
    bool victoryExit = false;
    std::filesystem::path source, nextLevel;
    bool loadFromFile(const std::filesystem::path& path, std::string& error) {
        try {
            LevelMap next;
            if (!next.DoorMap::loadFromFile(path, error, true)) {
                return false;
            }
            next.source = std::filesystem::absolute(path).lexically_normal();
            std::ifstream input(path);
            nlohmann::json data;
            input >> data;
            int count = 0;
            for (int y = 0; y < next.getHeight(); ++y) {
                const auto row = data.at("geometry").at(y).get<std::string>();
                for (int x = 0; x < next.getWidth(); ++x) {
                    if (row[x] == 'S') {
                        ++count;
                        next.switchX = x;
                        next.switchY = y;
                    }
                }
            }
            if (count > 1) {
                throw std::runtime_error("Only one S exit switch is allowed per level");
            }
            if (count == 1) {
                const auto& exit = data.at("exit");
                next.victoryExit = exit.value("victory", false);
                if (next.victoryExit) {
                    if (exit.contains("nextLevel")) {
                        throw std::runtime_error("Victory exit cannot also specify nextLevel");
                    }
                } else {
                    const auto target = exit.at("nextLevel").get<std::string>();
                    if (target.empty()) {
                        throw std::runtime_error("exit.nextLevel must not be empty");
                    }
                    next.nextLevel = (next.source.parent_path() / target).lexically_normal();
                    if (!std::filesystem::is_regular_file(next.nextLevel)) {
                        throw std::runtime_error("Exit destination does not exist: " + next.nextLevel.string());
                    }
                }
                bool accessible = false;
                for (auto [dx, dy] : {std::pair<int, int>{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) {
                    int x = next.switchX + dx, y = next.switchY + dy;
                    if (next.contains(x, y) && !next.getCell(x, y).isWall &&
                        !next.getCell(x, y).isDoor) {
                        accessible = true;
                    }
                }
                if (!accessible) {
                    throw std::runtime_error("Exit switch must have an accessible face");
                }
            } else if (data.contains("exit")) {
                throw std::runtime_error("exit requires one S cell in geometry");
            }
            *this = std::move(next);
            error.clear();
            return true;
        } catch (const std::exception& e) {
            error = e.what();
            return false;
        }
    }
    bool canUseExit(float x, float y, float angle) const {
        if (switchX < 0) {
            return false;
        }
        float dx = std::cos(angle), dy = std::sin(angle), entry, leave;
        int side;
        if (!DoorMap::intervalBox(x, y, dx, dy, float(switchX), float(switchX + 1), float(switchY), float(switchY + 1), entry, leave, side) ||
            entry <= 0 || entry >= 1.6f) {
            return false;
        }

        return Combat::obstruction(*this, x, y, dx, dy, entry + 0.01f) >= entry - 0.001f;
    }
};

class Level {
public:
    LevelMap map, checkpointMap;
    Combat combat, checkpointCombat;
    TextureManager textures{false};
    std::unique_ptr<GpuRenderer> renderer;
    bool won = false;

    bool load(const std::filesystem::path& path, bool carryInventory, std::string& error, bool graphics = true) {
        LevelMap nextMap;
        Combat nextCombat;
        TextureManager nextTextures(false);
        if (!nextMap.loadFromFile(path, error) || !nextCombat.load(path, nextMap, error) ||
            !nextTextures.loadFromFile(nextMap.getTextureCatalog(), error) ||
            !nextTextures.validateMap(nextMap, error)) {
            return false;
        }
        if (carryInventory) {
            nextCombat.hp = combat.hp;
            nextCombat.ammo = combat.ammo;
            nextCombat.shells = combat.shells;
            nextCombat.hasShotgun = combat.hasShotgun;
            nextCombat.weapon = combat.weapon;
        }
        auto nextRenderer = std::make_unique<GpuRenderer>();
        if (graphics && !nextRenderer->load(nextMap, nextTextures, error, nextCombat)) {
            return false;
        }
        map = std::move(nextMap);
        combat = std::move(nextCombat);
        textures = std::move(nextTextures);
        renderer = std::move(nextRenderer);
        checkpointMap = map;
        checkpointCombat = combat;
        won = false;
        error.clear();
        return true;
    }
    bool useExit(float x, float y, float angle, std::string& error, bool graphics = true) {
        error.clear();
        if (won || combat.hp <= 0 || !map.canUseExit(x, y, angle)) {
            return false;
        }
        if (map.victoryExit) {
            won = true;
            return true;
        }
        return load(map.nextLevel, true, error, graphics);
    }
    void restart() {
        map = checkpointMap;
        combat = checkpointCombat;
        won = false;
    }
};