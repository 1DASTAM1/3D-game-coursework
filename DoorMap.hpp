#pragma once
#include "Map.hpp"

inline constexpr float CAMERA_HEIGHT = 0.5f;
inline constexpr float DOOR_SPEED = 0.8f;
inline constexpr float DOOR_OPEN_TIME = 3.f;

struct Door {
    int x, y;
    float open = 0.f, timer = 0.f;
    enum State { Closed, Opening, Waiting, Closing } state = Closed;
    enum Shape { ThinX, ThinY } shape = ThinX;
    float minX() const {
        return x + (shape == ThinX ? 0.47f : 0.f);
    }
    float maxX() const {
        return x + (shape == ThinX ? 0.53f : 1.f);
    }
    float minY() const {
        return y + (shape == ThinY ? 0.47f : 0.f);
    }
    float maxY() const {
        return y + (shape == ThinY ? 0.53f : 1.f);
    }
};

class DoorMap : public Map {
public:
    std::vector<Door> doors;

    int ceilingTextureAt(float x, float y) const {
        const auto& cell = getClampedCell(int(std::floor(x)), int(std::floor(y)));
        if (cell.isDoor) {
            for (const auto& d : doors) {
                if (x >= d.minX() && x <= d.maxX() && y >= d.minY() && y <= d.maxY()) {
                    return cell.wallTexture;
                }
            }
        }
        return cell.ceilingTexture;
    }

    bool loadFromFile(const std::filesystem::path& path, std::string& error, bool allowSwitch = false) {
        DoorMap next;
        if (!next.Map::loadFromFile(path, error, true, allowSwitch)) {
            return false;
        }
        try {
            std::ifstream input(path);
            nlohmann::json data;
            input >> data;
            if (data.contains("doors")) {
                throw std::runtime_error("Use d in geometry instead of the old doors array");
            }
            for (int y = 0; y < next.getHeight(); ++y) {
                for (int x = 0; x < next.getWidth(); ++x) {
                    if (next.getCell(x, y).isDoor) {
                        Door door{x, y};

                        bool horizontal = next.getCell(x - 1, y).isWall && next.getCell(x + 1, y).isWall;
                        door.shape = horizontal ? Door::ThinY : Door::ThinX;
                        next.doors.push_back(door);
                    }
                }
            }
            const auto& p = next.getPlayerStart();
            if (!next.canMoveTo(p.x, p.y, 0.15f)) {
                throw std::runtime_error("Player overlaps a door");
            }
            *this = std::move(next);
            error.clear();
            return true;
        } catch (const std::exception& e) {
            error = e.what();
            return false;
        }
    }

    static bool overlaps(const Door& d, float x, float y, float radius) {
        float nearestX = std::clamp(x, d.minX(), d.maxX());
        float nearestY = std::clamp(y, d.minY(), d.maxY());
        return (x - nearestX) * (x - nearestX) + (y - nearestY) * (y - nearestY) < radius * radius;
    }
    bool canMoveTo(float x, float y, float radius) const {
        if (!Map::canMoveTo(x, y, radius)) {
            return false;
        }
        for (const auto& d : doors) {
            if (d.open <= CAMERA_HEIGHT && overlaps(d, x, y, radius)) {
                return false;
            }
        }
        return true;
    }

    static bool interval(const Door& d, float x, float y, float dx, float dy, float& entry, float& exit, int& side) {
        return intervalBox(x, y, dx, dy, d.minX(), d.maxX(), d.minY(), d.maxY(), entry, exit, side);
    }
    static bool intervalBox(float x, float y, float dx, float dy, float minX, float maxX, float minY, float maxY, float& entry, float& exit, int& side) {
        entry = 0;
        exit = 1e30f;
        side = 0;
        auto slab = [&](float origin, float dir, float low, float high, int face) {
            if (std::abs(dir) < 1e-7f) {
                return origin >= low && origin <= high;
            }
            float a = (low - origin) / dir, b = (high - origin) / dir;
            if (a > b) {
                std::swap(a, b);
            }
            if (a > entry) {
                entry = a;
                side = face;
            }
            exit = std::min(exit, b);
            return exit >= entry;
        };
        return slab(x, dx, minX, maxX, 0) && slab(y, dy, minY, maxY, 1) && exit > 0;
    }

    static float surface(const Door& d, float entry, float exit, int side, float dz, int& face) {
        if (d.open >= 1.f) {
            return -1;
        }
        face = side;
        if (std::abs(dz) < 1e-7f) {
            return d.open <= 0.5f ? entry : -1;
        }
        float a = (d.open - 0.5f) / dz, b = 0.5f / dz;
        if (a > b) {
            std::swap(a, b);
        }
        if (a > entry) {
            entry = a;
            face = dz > 0 ? 2 : 3;
        }
        exit = std::min(exit, b);
        return entry > 0 && exit >= entry ? entry : -1;
    }
    void use(float x, float y, float angle) {
        float dx = std::cos(angle), dy = std::sin(angle), best = 1.6f;
        Door* target = nullptr;
        for (auto& d : doors) {
            float t, exit;
            int side;
            if (!interval(d, x, y, dx, dy, t, exit, side)) {
                continue;
            }
            if (t <= 0 || t >= best) {
                continue;
            }
            bool blocked = false;
            for (float s = 0; s < t; s += 0.02f) {
                if (getClampedCell(int(x + s * dx), int(y + s * dy)).isWall) {
                    blocked = true;
                    break;
                }
            }
            if (!blocked) {
                target = &d;
                best = t;
            }
        }
        if (target) {
            if (target->state == Door::Waiting) {
                target->timer = DOOR_OPEN_TIME;
            } else {
                target->state = Door::Opening;
            }
        }
    }
    void update(float dt, float x, float y, float radius) {
        for (auto& d : doors) {
            bool occupied = overlaps(d, x, y, radius + 0.05f);
            if (d.state == Door::Opening) {
                d.open = std::min(1.f, d.open + dt * DOOR_SPEED);
                if (d.open == 1.f) {
                    d.state = Door::Waiting;
                    d.timer = DOOR_OPEN_TIME;
                }
            } else if (d.state == Door::Waiting) {
                if (occupied) {
                    d.timer = DOOR_OPEN_TIME;
                } else if ((d.timer -= dt) <= 0) {
                    d.state = Door::Closing;
                }
            } else if (d.state == Door::Closing) {
                if (occupied) {
                    d.state = Door::Opening;
                } else {
                    d.open = std::max(0.f, d.open - dt * DOOR_SPEED);
                    if (d.open == 0.f) {
                        d.state = Door::Closed;
                    }
                }
            }
        }
    }
};