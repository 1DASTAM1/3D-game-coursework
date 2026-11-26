#pragma once
#include "vendor/json.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <charconv>
#include <stdexcept>
#include <string>
#include <vector>

struct Cell {
    bool isWall = false;
    bool isDoor = false;
    int wallTexture = 0;
    int floorTexture = 0;
    int ceilingTexture = 0;
};

struct PlayerStart {
    float x = 1.5f;
    float y = 1.5f;
    float angle = 0.f;
};

class Map {
public:
    bool loadFromFile(const std::filesystem::path& filename, std::string& error, bool allowDoors = false, bool allowSwitch = false) {
        try {
            std::ifstream input(filename);
            if (!input) {
                throw std::runtime_error("Cannot open map: " + filename.string());
            }
            nlohmann::json data;
            input >> data;
            Map next;
            const auto directory = std::filesystem::absolute(filename).parent_path();
            const auto catalog = data.at("textureCatalog").get<std::string>();
            if (catalog.empty()) {
                throw std::runtime_error("textureCatalog must not be empty");
            }
            next.textureCatalog = (directory / catalog).lexically_normal();
            const auto& rows = data.at("geometry");
            if (!rows.is_array() || rows.size() < 3 || rows.size() > 1024 || !rows.at(0).is_string()) {
                throw std::runtime_error("geometry must contain 3 to 1024 string rows");
            }
            const auto width = rows.at(0).get_ref<const std::string&>().size();
            if (width < 3 || width > 1024) {
                throw std::runtime_error("geometry width must be between 3 and 1024");
            }
            for (std::size_t y = 0; y < rows.size(); ++y) {
                if (!rows[y].is_string() || rows[y].get_ref<const std::string&>().size() != width) {
                    throw std::runtime_error("geometry row " + std::to_string(y) + ": incorrect width");
                }
                std::vector<Cell> cellsRow;
                for (char symbol : rows[y].get_ref<const std::string&>()) {
                    if (symbol != '#' && symbol != '.' && !(allowSwitch && symbol == 'S') &&
                        !(allowDoors && symbol == 'd')) {
                        throw std::runtime_error(
                            "geometry row " + std::to_string(y) +
                            (allowDoors ? ": use only #, ., d and S" : ": use only # and ."));
                    }
                    Cell cell;
                    cell.isWall = symbol == '#' || (allowSwitch && symbol == 'S');
                    cell.isDoor = symbol == 'd';
                    cellsRow.push_back(cell);
                }
                next.cells.push_back(std::move(cellsRow));
            }

            auto readLayer = [&](const char* name, int Cell::* field) {
                if (!data.contains(name)) {
                    throw std::runtime_error(std::string("Missing texture layer: ") + name);
                }
                const auto& layer = data.at(name);
                if (!layer.is_array() || layer.size() != rows.size()) {
                    throw std::runtime_error(std::string(name) + ": height must match geometry");
                }
                for (std::size_t y = 0; y < layer.size(); ++y) {
                    const std::string location = std::string(name) + " row " + std::to_string(y);
                    if (!layer[y].is_string()) {
                        throw std::runtime_error(location + ": expected a string of IDs");
                    }
                    std::istringstream stream(layer[y].get<std::string>());
                    std::string token;
                    std::size_t x = 0;
                    while (stream >> token) {
                        if (x >= width) {
                            throw std::runtime_error(location + ": too many IDs");
                        }
                        int id = 0;
                        const auto parsed = std::from_chars(token.data(), token.data() + token.size(), id);
                        if (parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size() || id <= 0) {
                            throw std::runtime_error(location + " column " + std::to_string(x) + ": invalid texture ID " + token);
                        }
                        next.cells[y][x++].*field = id;
                    }
                    if (x != width) {
                        throw std::runtime_error(location + ": too few IDs");
                    }
                }
            };
            readLayer("floor", &Cell::floorTexture);
            readLayer("ceiling", &Cell::ceilingTexture);
            readLayer("walls", &Cell::wallTexture);
            for (int y = 0; y < next.getHeight(); ++y) {
                for (int x = 0; x < next.getWidth(); ++x) {
                    if ((x == 0 || y == 0 || x == next.getWidth() - 1 ||
                         y == next.getHeight() - 1) &&
                        !next.getCell(x, y).isWall) {
                        throw std::runtime_error("Map boundary must be closed by walls");
                    }
                }
            }
            const auto& player = data.at("player");
            next.start = {player.at("x").get<float>(), player.at("y").get<float>(), player.value("angle", 0.f)};
            if (!std::isfinite(next.start.x) || !std::isfinite(next.start.y) ||
                !std::isfinite(next.start.angle) || !next.canMoveTo(next.start.x, next.start.y, 0.15f)) {
                throw std::runtime_error("Player start overlaps a wall or is outside the map");
            }
            *this = std::move(next);
            error.clear();
            return true;
        } catch (const std::exception& e) {
            error = e.what();
            return false;
        }
    }

    int getWidth() const {
        return cells.empty() ? 0 : static_cast<int>(cells[0].size());
    }

    int getHeight() const {
        return static_cast<int>(cells.size());
    }

    bool contains(int x, int y) const {
        return x >= 0 && y >= 0 && x < getWidth() && y < getHeight();
    }

    const Cell& getCell(int x, int y) const {
        return cells.at(y).at(x);
    }

    const Cell& getClampedCell(int x, int y) const {
        return getCell(std::clamp(x, 0, getWidth() - 1), std::clamp(y, 0, getHeight() - 1));
    }

    const auto& getTextureCatalog() const {
        return textureCatalog;
    }

    const PlayerStart& getPlayerStart() const {
        return start;
    }

    bool canMoveTo(float x, float y, float radius) const {
        if (!std::isfinite(x) || !std::isfinite(y) || x - radius < 0 || y - radius < 0 ||
            x + radius >= getWidth() || y + radius >= getHeight()) {
            return false;
        }
        for (int cy = static_cast<int>(std::floor(y - radius));
             cy <= static_cast<int>(std::floor(y + radius)); ++cy) {
            for (int cx = static_cast<int>(std::floor(x - radius)); cx <= static_cast<int>(std::floor(x + radius)); ++cx) {
                if (!getCell(cx, cy).isWall) {
                    continue;
                }
                float dx = x - std::clamp(x, float(cx), float(cx + 1));
                float dy = y - std::clamp(y, float(cy), float(cy + 1));
                if (dx * dx + dy * dy < radius * radius) {
                    return false;
                }
            }
        }
        return true;
    }

private:
    std::vector<std::vector<Cell>> cells;
    std::filesystem::path textureCatalog;
    PlayerStart start;
};