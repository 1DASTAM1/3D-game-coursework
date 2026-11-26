#pragma once
#include "vendor/json.hpp"
#include <filesystem>
#include <fstream>
#include <algorithm>

struct Settings {
    unsigned width = 1200, height = 800;
    int sensitivityPercent = 100;
    bool fullscreen = false;

    void load(const std::filesystem::path& path) {
        std::ifstream input(path);
        if (!input) {
            if (std::filesystem::exists(path)) {
                throw std::runtime_error("Cannot read settings");
            }
            return;
        }
        nlohmann::json data;
        input >> data;
        if (!data.is_object()) {
            throw std::runtime_error("Settings must be an object");
        }
        if (data.contains("resolution") && data["resolution"].is_object()) {
            const auto& resolution = data["resolution"];
            if (resolution.contains("width") && resolution["width"].is_number_integer() &&
                resolution.contains("height") && resolution["height"].is_number_integer()) {
                const auto w = resolution["width"].get<double>();
                const auto h = resolution["height"].get<double>();
                if (w >= 800 && w <= 16384 && h >= 600 && h <= 16384) {
                    width = unsigned(w);
                    height = unsigned(h);
                }
            }
        }
        if (data.contains("sensitivityPercent") && data["sensitivityPercent"].is_number_integer()) {
            sensitivityPercent = int(std::clamp(data["sensitivityPercent"].get<double>(), 10.0, 500.0));
        }
        if (data.contains("fullscreen") && data["fullscreen"].is_boolean()) {
            fullscreen = data["fullscreen"].get<bool>();
        }
    }
    bool save(const std::filesystem::path& path) const {
        const nlohmann::json data = {{"resolution", {{"width", width}, {"height", height}}},
                                     {"sensitivityPercent", sensitivityPercent},
                                     {"fullscreen", fullscreen}};
        std::ofstream output(path);
        if (!output) {
            return false;
        }
        output << data.dump(4) << '\n';
        output.close();
        return bool(output);
    }
};