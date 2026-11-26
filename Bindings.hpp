#pragma once
#include <SFML/Graphics.hpp>
#include "vendor/json.hpp"
#include <array>
#include <fstream>

struct Bindings {
    std::array<int, 8> keys{int(sf::Keyboard::Scancode::W),    int(sf::Keyboard::Scancode::S),
                            int(sf::Keyboard::Scancode::A),    int(sf::Keyboard::Scancode::D),
                            int(sf::Keyboard::Scancode::E),    int(sf::Keyboard::Scancode::Num1),
                            int(sf::Keyboard::Scancode::Num2), -1};
    static int code(const sf::Event& event) {
        if (auto k = event.getIf<sf::Event::KeyPressed>()) {
            return k->scancode == sf::Keyboard::Scancode::Unknown ? -999 : int(k->scancode);
        }
        if (auto m = event.getIf<sf::Event::MouseButtonPressed>()) {
            return -1 - int(m->button);
        }
        return -999;
    }
    bool down(int i) const {
        return keys[i] >= 0 ? sf::Keyboard::isKeyPressed(sf::Keyboard::Scancode(keys[i])) : sf::Mouse::isButtonPressed(sf::Mouse::Button(-1 - keys[i]));
    }
    sf::String name(int i) const {
        if (keys[i] >= 0) {
            return sf::Keyboard::getDescription(sf::Keyboard::Scancode(keys[i]));
        }
        switch (keys[i]) {
        case -1:
            return L"ЛКМ";
        case -2:
            return L"ПКМ";
        case -3:
            return L"СКМ";
        default:
            return "Mouse " + std::to_string(-keys[i]);
        }
    }
    inline static constexpr std::array<const char*, 8> actions{"forward", "backward", "left", "right", "use", "pistol", "shotgun", "fire"};
    static std::string token(int key) {
        static constexpr std::array<const char*, sf::Keyboard::ScancodeCount> keyboard{
            "A",
            "B",
            "C",
            "D",
            "E",
            "F",
            "G",
            "H",
            "I",
            "J",
            "K",
            "L",
            "M",
            "N",
            "O",
            "P",
            "Q",
            "R",
            "S",
            "T",
            "U",
            "V",
            "W",
            "X",
            "Y",
            "Z",
            "1",
            "2",
            "3",
            "4",
            "5",
            "6",
            "7",
            "8",
            "9",
            "0",
            "Enter",
            "Escape",
            "Backspace",
            "Tab",
            "Space",
            "Hyphen",
            "Equal",
            "LBracket",
            "RBracket",
            "Backslash",
            "Semicolon",
            "Apostrophe",
            "Grave",
            "Comma",
            "Period",
            "Slash",
            "F1",
            "F2",
            "F3",
            "F4",
            "F5",
            "F6",
            "F",
            "F8",
            "F9",
            "F10",
            "F11",
            "F",
            "F",
            "F14",
            "F15",
            "F16",
            "F1",
            "F18",
            "F19",
            "F20",
            "F21",
            "F22",
            "F23",
            "F24",
            "CapsLock",
            "PrintScreen",
            "ScrollLock",
            "Pause",
            "Insert",
            "Home",
            "PageUp",
            "Delete",
            "End",
            "PageDown",
            "Right",
            "Left",
            "Down",
            "Up",
            "NumLock",
            "NumpadDivide",
            "NumpadMultiply",
            "NumpadMinus",
            "NumpadPlus",
            "NumpadEqual",
            "NumpadEnter",
            "NumpadDecimal",
            "Numpad1",
            "Numpad2",
            "Numpad3",
            "Numpad4",
            "Numpad5",
            "Numpad6",
            "Numpad",
            "Numpad8",
            "Numpad9",
            "Numpad0",
            "NonUsBackslash",
            "Application",
            "Execute",
            "ModeChange",
            "Help",
            "Menu",
            "Select",
            "Redo",
            "Undo",
            "Cut",
            "Copy",
            "Paste",
            "VolumeMute",
            "VolumeUp",
            "VolumeDown",
            "MediaPlayPause",
            "MediaStop",
            "MediaNextTrack",
            "MediaPreviousTrack",
            "LControl",
            "LShift",
            "LAlt",
            "LSystem",
            "RControl",
            "RShift",
            "RAlt",
            "RSystem",
            "Back",
            "Forward",
            "Refresh",
            "Stop",
            "Search",
            "Favorites",
            "HomePage",
            "LaunchApplication1",
            "LaunchApplication2",
            "LaunchMail",
            "LaunchMediaSelect"};
        static constexpr std::array<const char*, 5> mouse{"MouseLeft", "MouseRight", "MouseMiddle", "MouseX1", "MouseX2"};
        return key >= 0 ? keyboard.at(key) : mouse.at(-1 - key);
    }
    static int parse(const std::string& name) {
        for (int k = -int(sf::Mouse::ButtonCount); k < int(sf::Keyboard::ScancodeCount); ++k) {
            if (token(k) == name) {
                return k;
            }
        }
        return -999;
    }
    void load(const std::filesystem::path& path) {
        std::ifstream f(path);
        if (!f) {
            if (std::filesystem::exists(path)) {
                throw std::runtime_error("Cannot read bindings");
            }
            return;
        }
        nlohmann::json j;
        f >> j;
        auto candidate = keys;
        if (j.is_array()) {
            candidate = j.get<std::array<int, 8>>();
        } else {
            for (unsigned i = 0; i < candidate.size(); ++i) {
                candidate[i] = parse(j.at("bindings").at(actions[i]).get<std::string>());
            }
        }
        for (unsigned i = 0; i < candidate.size(); ++i) {
            int k = candidate[i];
            if (k < -int(sf::Mouse::ButtonCount) || k >= int(sf::Keyboard::ScancodeCount) || k == int(sf::Keyboard::Scancode::Escape)) {
                throw std::runtime_error("Invalid key binding");
            }
            for (unsigned n = 0; n < i; ++n) {
                if (candidate[n] == k) {
                    throw std::runtime_error("Duplicate key binding");
                }
            }
        }
        keys = candidate;
    }
    bool save(const std::filesystem::path& path) const {
        nlohmann::ordered_json data;
        for (unsigned i = 0; i < keys.size(); ++i) {
            data["bindings"][actions[i]] = token(keys[i]);
        }
        std::ofstream f(path);
        f << data.dump(2) << '\n';
        f.close();
        return bool(f);
    }
};