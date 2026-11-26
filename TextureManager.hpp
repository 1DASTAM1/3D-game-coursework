#pragma once
#include <SFML/Graphics.hpp>
#include "vendor/json.hpp"
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <charconv>
#include <stdexcept>

struct TextureResource {
    sf::Image image;
    sf::Texture texture;
};

class TextureManager {
public:
    explicit TextureManager(bool uploadToGpu = true) : uploadToGpu(uploadToGpu) {
    }

    bool loadFromFile(const std::filesystem::path& filename, std::string& error) {
        try {
            std::ifstream input(filename);
            if (!input) {
                throw std::runtime_error("Cannot open texture catalog: " + filename.string());
            }
            nlohmann::json data;
            input >> data;
            const auto& entries = data.at("textures");
            if (!entries.is_object()) {
                throw std::runtime_error("textures must be an object");
            }
            std::map<int, std::filesystem::path> paths;
            for (auto it = entries.begin(); it != entries.end(); ++it) {
                int id = 0;
                const auto& key = it.key();
                auto result = std::from_chars(key.data(), key.data() + key.size(), id);
                if (result.ec != std::errc{} || result.ptr != key.data() + key.size() || id <= 0 || paths.count(id)) {
                    throw std::runtime_error("Invalid or duplicate texture ID: " + key);
                }
                auto path = it.value().get<std::string>();
                if (path.empty()) {
                    throw std::runtime_error("Empty texture path");
                }
                paths[id] = std::filesystem::weakly_canonical(std::filesystem::absolute(filename).parent_path() / path);
            }
            std::map<int, std::shared_ptr<TextureResource>> next;
            for (const auto& [id, path] : paths) {
                auto found = cache.find(path);
                if (found != cache.end()) {
                    next[id] = found->second;
                    continue;
                }
                auto resource = std::make_shared<TextureResource>();
                if (!resource->image.loadFromFile(path) ||
                    (uploadToGpu && !resource->texture.loadFromImage(resource->image))) {
                    continue;
                }
                cache[path] = resource;
                next[id] = std::move(resource);
            }
            textures = std::move(next);
            error.clear();
            return true;
        } catch (const std::exception& e) {
            error = e.what();
            return false;
        }
    }

    bool has(int id) const {
        return textures.count(id) != 0;
    }

    template <class MapType> bool validateMap(const MapType& map, std::string& error) const {
        for (int y = 0; y < map.getHeight(); ++y) {
            for (int x = 0; x < map.getWidth(); ++x) {
                const auto& cell = map.getCell(x, y);
                for (int id : {cell.wallTexture, cell.floorTexture, cell.ceilingTexture}) {
                    if (!has(id)) {
                        error = "Missing texture ID " + std::to_string(id) + " at cell " + std::to_string(x) + "," + std::to_string(y);
                        return false;
                    }
                }
            }
        }
        error.clear();
        return true;
    }

    const TextureResource& get(int id) const {
        return *textures.at(id);
    }

    std::size_t cachedCount() const {
        return cache.size();
    }

private:
    bool uploadToGpu;
    std::map<std::filesystem::path, std::shared_ptr<TextureResource>> cache;
    std::map<int, std::shared_ptr<TextureResource>> textures;
};