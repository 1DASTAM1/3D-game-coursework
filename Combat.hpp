#pragma once
#include "DoorMap.hpp"
#include <SFML/System/Vector2.hpp>

enum class EnemyState { Idle, Attack, Dead };
struct Enemy {
    float x = 0, y = 0, angle = 0, speed = 0.7f, sightRange = 7.f, cooldown = 0;
    int hp = 100;
    int typeId = 1, maxHp = 100;
    int damage = 10;
    float fireInterval = 1.f, reactionTime = 0.7f, visionCos = 0.5f;
    EnemyState state = EnemyState::Idle;
};

struct EntityDefinition {
    std::string effect;
    int amount = 0;
    float width = 0.3f, height = 0.3f;
    std::filesystem::path texture;
};

struct Entity {
    int id;
    float x, y;
    bool collected = false;
};

struct EnemyAction {
    float windup = 0, attack = 0, walkTime = 0, targetX = 0, targetY = 0;
    bool moving = false;
};

struct Dynamite {
    float x, y, targetX, targetY, flight = 0.7f, fuse = 1.9f, burst = 0;
    int damage;
};

class Combat {
public:
    static constexpr int maxAmmo = 150;
    int ammo = 20;
    static constexpr int maxShells = 20;
    int shells = 0;
    bool hasShotgun = false;
    int weapon = 1;
    bool selectWeapon(int id) {
        if (hp <= 0 || (id != 1 && id != 2) || (id == 2 && !hasShotgun)) {
            return false;
        }
        if (weapon != id) {
            weapon = id;
            flash = 0;
        }
        return true;
    }
    std::map<int, EntityDefinition> definitions;
    std::vector<Entity> items;
    std::vector<EnemyAction> actions;
    std::vector<Dynamite> dynamites;
    std::map<std::string, std::filesystem::path> effectTextures;

    int frame(std::size_t i) const {
        if (enemies[i].hp <= 0) {
            return 1;
        }
        const auto& a = actions[i];
        if (a.attack > 0) {
            return 3;
        }
        if (a.windup > 0) {
            return 2;
        }
        if (a.moving) {
            return 4 + (int(a.walkTime / 0.18f) % 2);
        }
        return enemies[i].state == EnemyState::Attack ? 2 : 0;
    }
    int hp = 100;
    float cooldown = 0, flash = 0, hurt = 0, hit = 0;
    std::vector<Enemy> enemies;
    std::map<int, std::filesystem::path> enemyTextures;
    static constexpr float enemyRadius = 0.20f;
    static bool segmentClear(float x, float y, float nx, float ny, float cx, float cy, float radius) {
        float dx = nx - x, dy = ny - y, length2 = dx * dx + dy * dy;
        float t = length2 > 0 ? std::clamp(((cx - x) * dx + (cy - y) * dy) / length2, 0.f, 1.f) : 0.f;
        float ox = x + t * dx - cx, oy = y + t * dy - cy;
        return ox * ox + oy * oy >= radius * radius;
    }
    bool canTravel(float x, float y, float nx, float ny, float radius, const Enemy* ignore = nullptr) const {
        for (const auto& enemy : enemies) {
            if (&enemy != ignore && enemy.hp > 0 &&
                !segmentClear(x, y, nx, ny, enemy.x, enemy.y, radius + enemyRadius)) {
                return false;
            }
        }
        return true;
    }
    void movePlayer(const DoorMap& map, float& x, float& y, float dx, float dy, float radius) const {
        if (map.canMoveTo(x + dx, y, radius) && canTravel(x, y, x + dx, y, radius)) {
            x += dx;
        }
        if (map.canMoveTo(x, y + dy, radius) && canTravel(x, y, x, y + dy, radius)) {
            y += dy;
        }
    }

private:
    bool loadActors(const std::filesystem::path& path, const DoorMap& map, std::string& error) {
        try {
            std::ifstream file(path);
            nlohmann::json data;
            file >> data;
            Combat next;
            next.hp = data.at("player").value("hp", 100);
            if (next.hp < 0 || next.hp > 100) {
                throw std::runtime_error("player.hp must be 0..100");
            }
            if (data.contains("enemies")) {
                if (!data["enemies"].is_array() || data["enemies"].size() != std::size_t(map.getHeight())) {
                    throw std::runtime_error("enemies must contain one grid row per geometry row");
                }
                const auto catalogPath =
                    path.parent_path() / data.value("enemyCatalog", std::string("../enemies.json"));
                std::ifstream catalogFile(catalogPath);
                if (!catalogFile) {
                    throw std::runtime_error("Cannot open enemy catalog: " + catalogPath.string());
                }
                nlohmann::json catalog;
                catalogFile >> catalog;
                const auto& definitions = catalog.at("enemies");
                if (!definitions.is_object()) {
                    throw std::runtime_error("enemies.json: enemies must be an object keyed by positive IDs");
                }
                std::map<int, Enemy> prototypes;
                for (auto it = definitions.begin(); it != definitions.end(); ++it) {
                    int id = 0;
                    const auto& key = it.key();
                    auto parsed = std::from_chars(key.data(), key.data() + key.size(), id);
                    if (parsed.ec != std::errc{} || parsed.ptr != key.data() + key.size() || id <= 0 || id > 5 || prototypes.count(id)) {
                        throw std::runtime_error("Invalid or duplicate enemy ID: " + key);
                    }
                    const auto& config = it.value();
                    Enemy prototype;
                    prototype.hp = config.at("hp").get<int>();
                    prototype.speed = config.at("speed").get<float>();
                    prototype.sightRange = config.at("sightRange").get<float>();
                    prototype.angle = config.at("angle").get<float>();
                    prototype.damage = config.at("damage").get<int>();
                    prototype.fireInterval = config.at("fireInterval").get<float>();
                    prototype.reactionTime = config.at("reactionTime").get<float>();
                    const float fov = config.at("fieldOfView").get<float>();

                    auto bounded = [](float v, float low, float high) {
                        return std::isfinite(v) && v >= low && v <= high;
                    };
                    if (prototype.hp < 1 || prototype.hp > 1000 || prototype.damage < 1 ||
                        prototype.damage > 100 || !bounded(prototype.speed, 0.01f, 3.f) ||
                        !bounded(prototype.sightRange, 0.1f, 50.f) || !std::isfinite(prototype.angle) ||
                        !bounded(prototype.fireInterval, 0.05f, 60.f) || !bounded(prototype.reactionTime, 0.f, 60.f) || 
                        !bounded(fov, 1.f, 360.f)) {
                        throw std::runtime_error("Invalid settings in enemies.json");
                    }
                    prototype.visionCos = std::cos(fov * 3.14159265f / 360.f);
                    const auto texture = config.at("texture").get<std::string>();
                    if (texture.empty()) {
                        throw std::runtime_error("Enemy texture path is empty");
                    }
                    next.enemyTextures[id] = std::filesystem::weakly_canonical(catalogPath.parent_path() / texture);
                    if (!std::filesystem::is_regular_file(next.enemyTextures.at(id))) {
                        throw std::runtime_error("Enemy texture not found: " + next.enemyTextures.at(id).string());
                    }
                    prototype.typeId = id;
                    prototype.maxHp = prototype.hp;
                    prototypes[id] = prototype;
                }
                auto validPoint = [&](float x, float y) {
                    return std::isfinite(x) && std::isfinite(y) && x >= 0 && y >= 0 && x < map.getWidth() && y < map.getHeight() && map.canMoveTo(x, y, 0.2f);
                };
                for (int row = 0; row < map.getHeight(); ++row) {
                    if (!data["enemies"][row].is_string()) {
                        throw std::runtime_error("Enemy grid rows must be strings of enemy IDs separated by spaces");
                    }
                    std::istringstream cells(data["enemies"][row].get<std::string>());
                    for (int column = 0; column < map.getWidth(); ++column) {
                        std::string cell;
                        if (!(cells >> cell)) {
                            throw std::runtime_error("Enemy grid row is too short");
                        }
                        int typeId = 0;
                        auto parsed = std::from_chars(cell.data(), cell.data() + cell.size(), typeId);
                        if (parsed.ec != std::errc{} || parsed.ptr != cell.data() + cell.size() || typeId < 0) {
                            throw std::runtime_error("Invalid enemy grid ID: " + cell);
                        }
                        if (typeId == 0) {
                            continue;
                        }
                        if (next.enemies.size() >= 16) {
                            throw std::runtime_error("At most 16 enemies are supported");
                        }
                        if (!prototypes.count(typeId)) {
                            throw std::runtime_error("Unknown enemy ID: " + cell);
                        }
                        Enemy e = prototypes.at(typeId);
                        e.x = column + 0.5f;
                        e.y = row + 0.5f;
                        if (!validPoint(e.x, e.y)) {
                            throw std::runtime_error("Enemy spawn overlaps wall or door");
                        }
                        next.enemies.push_back(e);
                    }
                    std::string extra;
                    if (cells >> extra) {
                        throw std::runtime_error("Enemy grid row is wider than geometry");
                    }
                }
            }
            *this = std::move(next);
            error.clear();
            return true;
        } catch (const std::exception& e) {
            error = e.what();
            return false;
        }
    }

public:
    static float obstruction(const DoorMap& map, float x, float y, float dx, float dy, float range) {
        int cx = int(std::floor(x)), cy = int(std::floor(y));
        int sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1;
        float tx = std::abs(dx) < 1e-7f ? 1e30f : std::abs(1 / dx);
        float ty = std::abs(dy) < 1e-7f ? 1e30f : std::abs(1 / dy);
        float ex = (dx < 0 ? x - cx : cx + 1 - x) * tx, ey = (dy < 0 ? y - cy : cy + 1 - y) * ty;
        float t = 0, best = range;
        while (t < best) {
            if (cx < 0 || cy < 0 || cx >= map.getWidth() || cy >= map.getHeight() || map.getCell(cx, cy).isWall) {
                best = t;
                break;
            }
            if (ex < ey) {
                t = ex;
                ex += tx;
                cx += sx;
            } else {
                t = ey;
                ey += ty;
                cy += sy;
            }
        }
        for (const auto& d : map.doors) {
            float a, b;
            int side;
            if (d.open <= 0.5f && DoorMap::interval(d, x, y, dx, dy, a, b, side)) {
                best = std::min(best, a);
            }
        }
        return best;
    }
    bool load(const std::filesystem::path& path, const DoorMap& map, std::string& error) {
        try {
            Combat next;
            if (!next.loadActors(path, map, error)) {
                return false;
            }
            std::ifstream input(path);
            nlohmann::json data;
            input >> data;
            const auto catalogPath = path.parent_path() / data.value("entityCatalog", std::string("../entities.json"));
            std::ifstream catalogFile(catalogPath);
            nlohmann::json catalog;
            catalogFile >> catalog;
            const auto& defs = catalog.at("entities");
            if (!defs.is_object()) {
                throw std::runtime_error("entities catalog must be an object");
            }
            for (auto it = defs.begin(); it != defs.end(); ++it) {
                int id = 0;
                const auto& key = it.key();
                auto parsed = std::from_chars(key.data(), key.data() + key.size(), id);
                if (parsed.ec != std::errc{} || parsed.ptr != key.data() + key.size() || id <= 0 || next.definitions.count(id)) {
                    throw std::runtime_error("Invalid entity ID: " + key);
                }
                const auto& v = it.value();
                EntityDefinition def;
                def.effect = v.at("effect").get<std::string>();
                def.amount = v.at("amount").get<int>();
                def.width = v.at("width").get<float>();
                def.height = v.at("height").get<float>();
                def.texture = std::filesystem::weakly_canonical(catalogPath.parent_path() / v.at("texture").get<std::string>());
                if ((def.effect != "health" && def.effect != "ammo" && def.effect != "shotgun" && def.effect != "shells") ||
                    def.amount <= 0 || def.amount > 1000 || !std::isfinite(def.width) ||
                    !std::isfinite(def.height) || def.width <= 0 || def.width > 1 || def.height <= 0 || def.height > 1 ||
                    !std::filesystem::is_regular_file(def.texture)) {
                    throw std::runtime_error("Invalid entity definition: " + key);
                }
                next.definitions[id] = def;
            }
            if (data.contains("entities")) {
                const auto& grid = data.at("entities");
                if (!grid.is_array() || grid.size() != std::size_t(map.getHeight())) {
                    throw std::runtime_error("Invalid entities grid height");
                }
                for (int y = 0; y < map.getHeight(); ++y) {
                    std::istringstream row(grid.at(y).get<std::string>());
                    for (int x = 0; x < map.getWidth(); ++x) {
                        std::string token;
                        int id = 0;
                        if (!(row >> token)) {
                            throw std::runtime_error("Entities row too short");
                        }
                        auto parsed = std::from_chars(token.data(), token.data() + token.size(), id);
                        if (parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size() || id < 0) {
                            throw std::runtime_error("Invalid entity cell");
                        }
                        if (!id) {
                            continue;
                        }
                        if (!next.definitions.count(id)) {
                            throw std::runtime_error("Unknown entity ID: " + token);
                        }
                        if (next.items.size() >= 32) {
                            throw std::runtime_error("At most 32 pickups supported");
                        }
                        if (!map.canMoveTo(x + 0.5f, y + 0.5f, 0.2f)) {
                            throw std::runtime_error("Entity overlaps wall or door");
                        }
                        next.items.push_back({id, x + 0.5f, y + 0.5f, false});
                    }
                    std::string extra;
                    if (row >> extra) {
                        throw std::runtime_error("Entities row too wide");
                    }
                }
            }
            for (const auto& name : {"dynamite", "explosion"}) {
                auto p = std::filesystem::weakly_canonical(catalogPath.parent_path() / catalog.at("effects").at(name).get<std::string>());
                if (!std::filesystem::is_regular_file(p)) {
                    throw std::runtime_error("Missing effect: " + p.string());
                }
                next.effectTextures[name] = p;
            }
            next.actions.resize(next.enemies.size());
            *this = std::move(next);
            error.clear();
            return true;
        } catch (const std::exception& e) {
            error = e.what();
            return false;
        }
    }
    static bool clear(const DoorMap& map, float x, float y, float tx, float ty) {
        float d = std::hypot(tx - x, ty - y);
        return d < 0.001f || obstruction(map, x, y, (tx - x) / d, (ty - y) / d, d) >= d - 0.001f;
    }
    static bool seesPlayer(const DoorMap& map, const Enemy& e, float x, float y) {
        float dx = x - e.x, dy = y - e.y, d = std::hypot(dx, dy);
        if (e.hp <= 0 || d >= e.sightRange) {
            return false;
        }
        if (d < 0.001f) {
            return true;
        }
        if (obstruction(map, e.x, e.y, dx / d, dy / d, d) < d - 0.001f) {
            return false;
        }

        return d <= 1.5f || e.state == EnemyState::Attack || (dx * std::cos(e.angle) + dy * std::sin(e.angle)) / d >= e.visionCos;
    }
    void damageEnemy(std::size_t i, int damage) {
        auto& e = enemies[i];
        if (e.hp <= 0) {
            return;
        }
        e.hp = std::max(0, e.hp - damage);
        hit = 0.15f;
        if (e.state != EnemyState::Attack) {
            e.cooldown = e.reactionTime;
        }
        e.state = e.hp ? EnemyState::Attack : EnemyState::Dead;
        if (!e.hp) {
            actions[i] = {};
        }
    }
    bool shoot(const DoorMap& map, float x, float y, float angle) {
        if (hp <= 0 || cooldown > 0 || (weapon == 1 ? ammo <= 0 : (!hasShotgun || shells <= 0))) {
            return false;
        }
        if (weapon == 1) {
            --ammo;
        } else {
            --shells;
        }
        cooldown = weapon == 1 ? 0.3f : 0.85f;
        flash = 0.14f;
        std::vector<int> damage(enemies.size());
        for (int pellet = weapon == 1 ? 0 : -3; pellet <= (weapon == 1 ? 0 : 3); ++pellet) {
            float a = angle + pellet * 0.018f, dx = std::cos(a), dy = std::sin(a);
            float best = obstruction(map, x, y, dx, dy, weapon == 1 ? 20.f : 10.f);
            int target = -1;
            for (std::size_t i = 0; i < enemies.size(); ++i) {
                const auto& e = enemies[i];
                if (e.hp <= 0) {
                    continue;
                }
                float ox = e.x - x, oy = e.y - y, along = ox * dx + oy * dy;
                if (along > 0.05f && along < best && std::abs(-ox * dy + oy * dx) < 0.134f) {
                    best = along;
                    target = int(i);
                }
            }
            if (target >= 0) {
                damage[target] += weapon == 1 ? 25 : 20;
            }
        }
        for (std::size_t i = 0; i < damage.size(); ++i) {
            if (damage[i]) {
                damageEnemy(i, damage[i]);
            }
        }
        return true;
    }
    void moveEnemy(const DoorMap& map, Enemy& e, float tx, float ty, float px, float py, float dt) {
        float dx = tx - e.x, dy = ty - e.y, d = std::hypot(dx, dy);
        if (d < 0.01f) {
            return;
        }
        float step = std::min(d, e.speed * dt);
        dx = dx / d * step;
        dy = dy / d * step;
        auto move = [&](float nx, float ny) {
            if (map.canMoveTo(nx, ny, enemyRadius) && canTravel(e.x, e.y, nx, ny, enemyRadius, &e) && segmentClear(e.x, e.y, nx, ny, px, py, enemyRadius + 0.15f)) {
                e.x = nx; 
                e.y = ny;
            }
        };
        move(e.x + dx, e.y);
        move(e.x, e.y + dy);
    }
    void explode(const DoorMap& map, Dynamite& g, float x, float y) {
        auto amount = [&](float tx, float ty) {
            float d = std::hypot(tx - g.x, ty - g.y);
            return d < 2.4f && clear(map, g.x, g.y, tx, ty) ? int(std::ceil(g.damage * (1 - d / 2.4f))) : 0;
        };
        int damage = amount(x, y);
        if (damage) {
            hp = std::max(0, hp - damage);
            hurt = 0.3f;
        }
        for (std::size_t i = 0; i < enemies.size(); ++i) {
            damage = amount(enemies[i].x, enemies[i].y);
            if (damage) {
                damageEnemy(i, damage);
            }
        }
        g.burst = 0.3f;
    }
    void collect(const DoorMap& map, float x, float y) {
        if (hp <= 0) {
            return;
        }
        for (auto& item : items) {
            if (item.collected) {
                continue;
            }
            const float dx = item.x - x, dy = item.y - y, dist = std::hypot(dx, dy);
            if (dist > 0.45f) {
                continue;
            }
            if (dist > 0.001f &&
                obstruction(map, x, y, dx / dist, dy / dist, dist) < dist - 0.001f) {
                continue;
            }
            const auto& def = definitions.at(item.id);
            if (def.effect == "health") {
                if (hp >= 100) {
                    continue;
                }
                hp = std::min(100, hp + def.amount);
            } else if (def.effect == "shotgun") {
                if (hasShotgun && shells >= maxShells) {
                    continue;
                }
                const bool first = !hasShotgun;
                hasShotgun = true;
                shells = std::min(maxShells, shells + def.amount);
                if (first) {
                    selectWeapon(2);
                }
            } else if (def.effect == "shells") {
                if (shells >= maxShells) {
                    continue;
                }
                shells = std::min(maxShells, shells + def.amount);
            } else {
                if (ammo >= maxAmmo) {
                    continue;
                }
                ammo = std::min(maxAmmo, ammo + def.amount);
            }
            item.collected = true;
        }
    }
    void step(const DoorMap& map, float x, float y, float dt) {
        for (float* timer : {&cooldown, &flash, &hurt, &hit}) {
            *timer = std::max(0.f, *timer - dt);
        }
        for (auto& g : dynamites) {
            if (g.fuse <= 0) {
                g.burst = std::max(0.f, g.burst - dt);
                continue;
            }
            if (g.flight > 0) {
                float f = std::min(1.f, dt / g.flight);
                float nx = g.x + (g.targetX - g.x) * f, ny = g.y + (g.targetY - g.y) * f;
                if (clear(map, g.x, g.y, nx, ny)) {
                    g.x = nx;
                    g.y = ny;
                } else {
                    g.flight = 0;
                }
                g.flight = std::max(0.f, g.flight - dt);
            }
            g.fuse -= dt;
            if (g.fuse <= 0) {
                explode(map, g, x, y);
            }
        }
        dynamites.erase(std::remove_if(dynamites.begin(), dynamites.end(),
            [](const auto& g) {
                return g.fuse <= 0 && g.burst <= 0;
            }), dynamites.end());
        for (std::size_t i = 0; i < enemies.size() && hp > 0; ++i) {
            auto& e = enemies[i];
            auto& a = actions[i];
            a.moving = false;
            if (e.hp <= 0) {
                continue;
            }
            e.cooldown = std::max(0.f, e.cooldown - dt);
            a.attack = std::max(0.f, a.attack - dt);
            float dx = x - e.x, dy = y - e.y, d = std::hypot(dx, dy);
            bool visible = seesPlayer(map, e, x, y);
            if (a.windup > 0) {
                a.windup = std::max(0.f, a.windup - dt);
                if (a.windup == 0) {
                    bool hitPlayer = visible && clear(map, e.x, e.y, x, y);
                    if (e.typeId == 2) {
                        hitPlayer = hitPlayer && d < 0.8f;
                    }
                    if (e.typeId == 4) {
                        hitPlayer = hitPlayer && std::hypot(x - a.targetX, y - a.targetY) < 0.45f;
                    }
                    if (e.typeId == 5) {
                        if (dynamites.size() < 16) {
                            dynamites.push_back({e.x, e.y, a.targetX, a.targetY, 0.7f, 1.9f, 0, e.damage});
                        }
                        hitPlayer = false;
                    }
                    if (hitPlayer) {
                        hp = std::max(0, hp - e.damage);
                        hurt = 0.2f;
                    }
                    a.attack = 0.18f;
                    e.cooldown = e.fireInterval;
                }
                continue;
            }
            if (a.attack > 0) {
                continue;
            }
            if (!visible) {
                e.state = EnemyState::Idle;
                a.walkTime = 0;
                continue;
            }
            if (e.state != EnemyState::Attack) {
                e.cooldown = e.reactionTime;
            }
            e.state = EnemyState::Attack;
            e.angle = std::atan2(dy, dx);
            const float stop = e.typeId == 2 ? 0.65f : e.typeId == 4 ? 7.f : e.typeId == 5 ? 5.f : 3.5f;
            if (d > stop) {
                float oldX = e.x, oldY = e.y;
                moveEnemy(map, e, x, y, x, y, dt);
                a.moving = std::hypot(e.x - oldX, e.y - oldY) > 0.00001f;
                if (a.moving) {
                    a.walkTime += dt;
                }
            }
            if (e.cooldown <= 0 && (e.typeId != 2 || d < 0.8f)) {
                a.targetX = x;
                a.targetY = y;
                a.windup = e.typeId == 4 ? 1.4f : e.typeId == 5 ? 0.7f : e.typeId == 2 ? 0.4f : 0.25f;
                a.moving = false;
            }
        }
    }
    void update(const DoorMap& map, float x, float y, float dt) {
        if (hp <= 0 || !std::isfinite(dt) || dt <= 0) {
            return;
        }
        collect(map, x, y);
        while (dt > 0 && hp > 0) {
            float slice = std::min(dt, 0.02f);
            step(map, x, y, slice);
            dt -= slice;
        }
    }
};