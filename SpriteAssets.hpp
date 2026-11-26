#pragma once
#include <SFML/Graphics.hpp>
#include <filesystem>
#include <string>
#include <vector>
#include <array>

inline void cleanCellEdges(sf::Image& image) {
    auto size = image.getSize();
    std::vector<bool> seen(size.x * size.y);
    std::vector<std::vector<sf::Vector2u>> components;
    std::vector<bool> touchesEdge;
    std::size_t largest = 0;
    for (unsigned y = 0; y < size.y; ++y) {
        for (unsigned x = 0; x < size.x; ++x) {
            if (seen[y * size.x + x] || image.getPixel({x, y}).a < 128) {
                continue;
            }
            std::vector<sf::Vector2u> points{{x, y}};
            seen[y * size.x + x] = true;
            bool edge = false;
            for (std::size_t i = 0; i < points.size(); ++i) {
                auto p = points[i];
                edge |= p.x == 0 || p.y == 0 || p.x + 1 == size.x || p.y + 1 == size.y;
                for (auto [dx, dy] : {std::pair<int, int>{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) {
                    int nx = int(p.x) + dx, ny = int(p.y) + dy;
                    if (nx < 0 || ny < 0 || nx >= int(size.x) || ny >= int(size.y)) {
                        continue;
                    }
                    auto at = unsigned(ny) * size.x + unsigned(nx);
                    if (!seen[at] && image.getPixel({unsigned(nx), unsigned(ny)}).a >= 128) {
                        seen[at] = true;
                        points.push_back({unsigned(nx), unsigned(ny)});
                    }
                }
            }
            largest = std::max(largest, points.size());
            components.push_back(std::move(points));
            touchesEdge.push_back(edge);
        }
    }
    for (std::size_t i = 0; i < components.size(); ++i) {
        if (touchesEdge[i] && components[i].size() * 10 < largest) {
            for (auto p : components[i]) {
                image.setPixel(p, sf::Color::Transparent);
            }
        }
    }
}

inline bool loadEnemyFrames(const std::filesystem::path& path, std::array<sf::Image, 6>& frames, std::string& error) {
    sf::Image sheet;
    if (!sheet.loadFromFile(path)) {
        error = "Cannot load enemy sheet: " + path.string();
        return false;
    }
    auto size = sheet.getSize();
    if (size.x % 3 || size.y % 2 || size.x / 3 != size.y / 2) {
        error = "Expected square cells in a 3x2 sheet: " + path.string();
        return false;
    }
    unsigned cell = size.x / 3;
    for (unsigned i = 0; i < 6; ++i) {
        frames[i] = sf::Image({cell, cell}, sf::Color::Transparent);
        if (!frames[i].copy(sheet, {0, 0}, sf::IntRect({int(i % 3 * cell), int(i / 3 * cell)}, {int(cell), int(cell)}))) {
            return false;
        }
        cleanCellEdges(frames[i]);
        bool opaque = false;
        for (unsigned y = 0; y < cell && !opaque; ++y) {
            for (unsigned x = 0; x < cell; ++x) {
                if (frames[i].getPixel({x, y}).a >= 128) {
                    opaque = true;
                    break;
                }
            }
        }
        if (!opaque) {
            error = "Empty enemy animation cell: " + path.string();
            return false;
        }
    }
    return true;
}

inline sf::Image cropSprite(const sf::Image& image) {
    auto size = image.getSize();
    unsigned left = size.x, top = size.y, right = 0, bottom = 0;
    for (unsigned y = 0; y < size.y; ++y) {
        for (unsigned x = 0; x < size.x; ++x) {
            if (image.getPixel({x, y}).a >= 128) {
                left = std::min(left, x);
                top = std::min(top, y);
                right = std::max(right, x);
                bottom = std::max(bottom, y);
            }
        }
    }
    if (left > right || top > bottom) {
        return image;
    }
    sf::Image result({right - left + 1, bottom - top + 1}, sf::Color::Transparent); 
        (void)result.copy(image, {0, 0}, sf::IntRect({int(left), int(top)}, {int(right - left + 1), int(bottom - top + 1)}));
    return result;
}

inline bool loadWeaponSheet(sf::Texture& texture, const std::filesystem::path& path, std::string& error) {
    sf::Image image;
    if (!image.loadFromFile(path)) {
        error = "Cannot load weapon sheet: " + path.string();
        return false;
    }
    const auto size = image.getSize();
    if (!size.y || size.x != size.y * 2) {
        error = "Expected two square weapon frames: " + path.string();
        return false;
    }
    for (unsigned frame = 0; frame < 2; ++frame) {
        bool opaque = false;
        for (unsigned y = 0; y < size.y && !opaque; ++y) {
            for (unsigned x = 0; x < size.y; ++x) {
                if (image.getPixel({frame * size.y + x, y}).a >= 128) {
                    opaque = true;
                    break;
                }
            }
        }
        if (!opaque) {
            error = "Empty weapon frame: " + path.string();
            return false;
        }
    }
    if (!texture.loadFromImage(image)) {
        error = "Cannot upload weapon sheet: " + path.string();
        return false;
    }
    texture.setSmooth(false);
    return true;
}

inline bool loadSprite(sf::Texture& texture, const std::filesystem::path& path, std::string& error) {
    sf::Image image;
    if (!image.loadFromFile(path)) {
        error = "Cannot load sprite: " + path.string();
        return false;
    }

    auto size = image.getSize();
    unsigned top = 0;
    for (; top < size.y; ++top) {
        bool opaque = false;
        for (unsigned x = 0; x < size.x; ++x) {
            if (image.getPixel({x, top}).a >= 128) {
                opaque = true;
                break;
            }
        }
        if (opaque) {
            break;
        }
    }
    if (top == size.y) {
        error = "Empty sprite: " + path.string();
        return false;
    }
    sf::Image cropped({size.x, size.y - top}, sf::Color::Transparent);
        (void)cropped.copy(image, {0, 0}, sf::IntRect({0, int(top)}, {int(size.x), int(size.y - top)}));
    if (!texture.loadFromImage(cropped)) {
        error = "Cannot upload sprite: " + path.string();
        return false;
    }
    texture.setSmooth(false);
    return true;
}

inline sf::Image atlasSprite(const sf::Image& image) {
    auto size = image.getSize();
    float scale = std::min(1.f, 512.f / std::max(size.x, size.y));
    if (scale == 1) {
        return image;
    }
    sf::Vector2u target{std::max(1u, unsigned(size.x * scale)), std::max(1u, unsigned(size.y * scale))};
    sf::Image result(target, sf::Color::Transparent);
    for (unsigned y = 0; y < target.y; ++y) {
        for (unsigned x = 0; x < target.x; ++x) {
            result.setPixel({x, y}, image.getPixel({unsigned(std::uint64_t(x) * size.x / target.x), unsigned(std::uint64_t(y) * size.y / target.y)}));
        }
    }
    return result;
}