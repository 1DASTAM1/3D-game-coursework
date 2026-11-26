#include <SFML/Graphics.hpp>
#include <SFML/System/Err.hpp>
#include <streambuf>
#include "GameWindow.hpp"
#include <cmath>
#include <algorithm>
#include <vector>
#include <cstdint>
#include "Level.hpp"
#include "TextureManager.hpp"
#include "GpuRenderer.hpp"
#include "Settings.hpp"
#include "Bindings.hpp"
#include "Hud.hpp"

class SilentErrorOutput : public std::streambuf {
protected:
    int_type overflow(int_type character) override {
        return traits_type::not_eof(character);
    }

    std::streamsize xsputn(const char*, std::streamsize count) override {
        return count;
    }
};

int runGame(int argc, char* argv[]) {
    const auto executableDirectory = std::filesystem::absolute(argv[0]).parent_path();
    const std::filesystem::path mapPath = argc > 1 ? std::filesystem::path(argv[1]) : executableDirectory / "maps" / "level1.json";
    Level level;
    auto& map = level.map;
    auto& combat = level.combat;
    auto& textures = level.textures;
    auto& renderer = level.renderer;
    std::string error;
    if (!level.load(mapPath, false, error, false)) {
        return 1;
    }

    const bool checkOnly = argc > 2 && std::string(argv[2]) == "--check";
    sf::Texture pistol;
    if (!loadWeaponSheet(pistol, executableDirectory / "assets" / "textures" / "revolver_hands.png", error)) {
        return 1;
    }
    sf::Texture shotgun;
    if (!loadWeaponSheet(shotgun, executableDirectory / "assets" / "textures" / "shotgun_hands.png", error)) {
        return 1;
    }
    if (checkOnly) {
        if (!renderer->load(map, textures, error, combat)) {
            return 1;
        }
        return 0;
    }

    const bool victoryCheck = argc > 2 && std::string(argv[2]) == "--victory-check";
    const bool renderCheck = victoryCheck || (argc > 2 && std::string(argv[2]) == "--render-check");
    const auto settingsPath = executableDirectory / "settings.json";
    Settings savedSettings;
    savedSettings.load(settingsPath);
    const auto desktopMode = sf::VideoMode::getDesktopMode();
    std::vector<sf::Vector2u> windowSizes;
    for (const auto size : {sf::Vector2u{800, 600},
                            {1024, 768},
                            {1200, 800},
                            {1280, 720},
                            {1600, 900},
                            {1920, 1080},
                            {2560, 1440}}) {
        if (size.x <= desktopMode.size.x && size.y + 80 <= desktopMode.size.y) {
            windowSizes.push_back(size);
        }
    }
    if (windowSizes.empty()) {
        windowSizes.push_back({800, 600});
    }
    std::vector<sf::VideoMode> fullscreenModes;
    for (const auto& mode : sf::VideoMode::getFullscreenModes()) {
        if (mode.size.x < 800 || mode.size.y < 600 || mode.size.y % 2 != 0) {
            continue;
        }
        if (std::none_of(fullscreenModes.begin(), fullscreenModes.end(), [&](const auto& existing) {
                return existing.size == mode.size;
            })) {
            fullscreenModes.push_back(mode);
        }
    }

    std::sort(fullscreenModes.begin(), fullscreenModes.end(), [](const auto& a, const auto& b) {
        const auto pixelsA = std::uint64_t(a.size.x) * a.size.y;
        const auto pixelsB = std::uint64_t(b.size.x) * b.size.y;
        if (pixelsA != pixelsB) {
            return pixelsA < pixelsB;
        }
        if (a.size.x != b.size.x) {
            return a.size.x < b.size.x;
        }
        return a.size.y < b.size.y;
    });
    std::size_t windowSizeIndex = 0;
    for (std::size_t i = 0; i < windowSizes.size(); ++i) {
        if (windowSizes[i] == sf::Vector2u{1200, 800}) {
            windowSizeIndex = i;
        }
    }
    std::size_t fullscreenIndex = 0;
    for (std::size_t i = 0; i < fullscreenModes.size(); ++i) {
        if (fullscreenModes[i].size == desktopMode.size) {
            fullscreenIndex = i;
        }
    }
    const sf::Vector2u savedSize{savedSettings.width, savedSettings.height};
    for (std::size_t i = 0; i < windowSizes.size(); ++i) {
        if (windowSizes[i] == savedSize) {
            windowSizeIndex = i;
        }
    }
    for (std::size_t i = 0; i < fullscreenModes.size(); ++i) {
        if (fullscreenModes[i].size == savedSize) {
            fullscreenIndex = i;
        }
    }
    bool fullscreen = savedSettings.fullscreen && !fullscreenModes.empty();
    const auto initialMode = fullscreen ? fullscreenModes[fullscreenIndex] : sf::VideoMode(windowSizes[windowSizeIndex]);
    unsigned int SCREEN_WIDTH = initialMode.size.x;
    unsigned int SCREEN_HEIGHT = initialMode.size.y;
    const auto windowStyle = sf::Style::Titlebar | sf::Style::Close;
    GameWindow window(initialMode, "Game", windowStyle, fullscreen ? sf::State::Fullscreen : sf::State::Windowed);
    if (!window.isOpen()) {
        return 1;
    }
    if (renderCheck) {
        window.setVisible(false);
    }
    if (!renderer->load(map, textures, error, combat)) {
        return 1;
    }

    window.setFramerateLimit(60);
    window.setMouseCursorVisible(true);

    float playerX = map.getPlayerStart().x;
    float playerY = map.getPlayerStart().y;
    float playerAngle = map.getPlayerStart().angle;

    const float PI = 3.14159265f;
    const float TWO_PI = 2.0f * PI;
    const float moveSpeed = 0.03f;
    const float playerRadius = 0.15f;
    const float baseMouseSensitivity = 0.0005f;
    int sensitivityPercent = savedSettings.sensitivityPercent;
    Bindings bindings;
    const auto bindingsPath = executableDirectory / "bindings.json";
    bindings.load(bindingsPath);
    int bindingCapture = -1;
    bool settingsDirty = true;
    const auto saveSettings = [&] {
        savedSettings.width = SCREEN_WIDTH;
        savedSettings.height = SCREEN_HEIGHT;
        savedSettings.fullscreen = fullscreen;
        savedSettings.sensitivityPercent = sensitivityPercent;
        const bool settingsSaved = savedSettings.save(settingsPath);
        const bool bindingsSaved = bindings.save(bindingsPath);
        if (!settingsSaved || !bindingsSaved) {
            throw std::runtime_error("Cannot save settings");
        }
        settingsDirty = false;
    };

    sf::Vector2i mouseCenter(SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2);

    sf::Font font;

    if (!font.openFromFile(executableDirectory / "arial.ttf")) {
        return 1;
    }

    sf::Text text(font);
    text.setCharacterSize(15);
    text.setFillColor(sf::Color::Black);
    text.setPosition({10.f, 10.f});

    sf::Clock frameClock;
    bool mainMenu = true;
    bool paused = true;
    bool settingsOpen = false;
    enum class SettingsTab { Home, Screen, Controls };
    SettingsTab settingsTab = SettingsTab::Home;
    sf::RectangleShape pauseShade({float(SCREEN_WIDTH), float(SCREEN_HEIGHT)});
    pauseShade.setFillColor(sf::Color(0, 0, 0, 175));
    sf::RectangleShape resumeButton({320.f, 64.f});
    resumeButton.setPosition({440.f, 350.f});
    sf::RectangleShape exitButton({320.f, 64.f});
    exitButton.setPosition({440.f, 438.f});
    sf::Text pauseTitle(font, sf::String(L"Пауза"), 42);
    sf::Text resumeLabel(font, sf::String(L"Продолжить"), 28);
    sf::Text exitLabel(font, sf::String(L"Выход"), 28);
    const auto centerText = [](sf::Text& label, sf::Vector2f center) {
        const auto bounds = label.getLocalBounds();
        label.setOrigin(bounds.position + bounds.size / 2.f);
        label.setPosition(center);
    };
    sf::RectangleShape resolutionButton({460.f, 60.f});
    sf::RectangleShape fullscreenButton({460.f, 60.f});
    sf::Text resolutionLabel(font, "", 24);
    sf::Text fullscreenLabel(font, "", 24);
    sf::RectangleShape settingsButton({460.f, 60.f});
    sf::RectangleShape backButton({460.f, 60.f});
    sf::RectangleShape screenTabButton({460.f, 60.f});
    sf::RectangleShape controlsTabButton({460.f, 60.f});
    bool draggingSensitivity = false;
    sf::RectangleShape sensitivityTrack({400.f, 8.f});
    sensitivityTrack.setFillColor(sf::Color(65, 75, 90));
    sf::RectangleShape sensitivityFill({0.f, 8.f});
    sensitivityFill.setFillColor(sf::Color(85, 155, 235));
    sf::CircleShape sensitivityHandle(12.f);
    sensitivityHandle.setOrigin({12.f, 12.f});
    sf::FloatRect sensitivityHitArea;
    sf::Text settingsLabel(font, sf::String(L"Настройки"), 28);
    sf::Text settingsTitle(font, sf::String(L"Настройки"), 42);
    sf::Text backLabel(font, sf::String(L"Назад"), 28);
    sf::Text screenTabLabel(font, sf::String(L"Экран"), 28);
    sf::Text controlsTabLabel(font, sf::String(L"Управление"), 28);
    sf::Text sensitivityTitle(font, sf::String(L"Чувствительность мыши"), 26);
    sf::Text sensitivityLabel(font, "", 28);
    sf::Text sensitivityHint(font, sf::String(L"10% — 500%   |   По умолчанию: 100%"), 20);
    const std::array<sf::String, 8> actionNames{L"Вперёд",   L"Назад",        L"Влево",
                                                L"Вправо",   L"Использовать", L"Револьвер",
                                                L"Дробовик", L"Огонь"};
    std::array<sf::FloatRect, 8> bindingAreas;
    sensitivityHint.setFillColor(sf::Color(190, 195, 205));
    const auto layoutMenu = [&] {
        pauseTitle.setString(!mainMenu && combat.hp == 0 ? sf::String(L"ВЫ ПОГИБЛИ") : level.won ? sf::String(L"ПОБЕДА")  : mainMenu ? sf::String(L"GAME") : sf::String(L"Пауза"));
        pauseTitle.setFillColor(sf::Color::White);
        resumeLabel.setString(!mainMenu && combat.hp == 0 ? sf::String(L"Начать сначала") : level.won ? sf::String(L"Начать сначала") : mainMenu ? sf::String(L"Начать игру") : sf::String(L"Продолжить"));
        exitLabel.setString(mainMenu ? sf::String(L"Выход") : sf::String(L"В главное меню"));
        const float cx = SCREEN_WIDTH / 2.f;
        const float cy = SCREEN_HEIGHT / 2.f;
        pauseShade.setSize({float(SCREEN_WIDTH), float(SCREEN_HEIGHT)});
        resumeButton.setSize({460.f, 60.f});
        exitButton.setSize({460.f, 60.f});
        resumeButton.setPosition({cx - 230.f, cy - 125.f});
        resolutionButton.setPosition({cx - 230.f, cy - 185.f});
        fullscreenButton.setPosition({cx - 230.f, cy - 105.f});
        exitButton.setPosition({cx - 230.f, cy + 35.f});
        settingsButton.setPosition({cx - 230.f, cy - 45.f});
        const float backY = settingsTab == SettingsTab::Home ? cy - 25.f : settingsTab == SettingsTab::Screen ? cy - 25.f : cy + 205.f;
        backButton.setPosition({cx - 230.f, backY});
        screenTabButton.setPosition({cx - 230.f, cy - 185.f});
        controlsTabButton.setPosition({cx - 230.f, cy - 105.f});
        sensitivityTrack.setPosition({cx - 200.f, cy - 89.f});
        sensitivityFill.setPosition(sensitivityTrack.getPosition());
        const float sliderOffset = (sensitivityPercent - 10) / 490.f * 400.f;
        sensitivityFill.setSize({sliderOffset, 8.f});
        sensitivityHandle.setPosition({cx - 200.f + sliderOffset, cy - 85.f});
        sensitivityHitArea = sf::FloatRect({cx - 215.f, cy - 105.f}, {430.f, 40.f});
        sensitivityLabel.setString(std::to_string(sensitivityPercent) + "%");
        resolutionLabel.setString(sf::String(L"Разрешение: ") + std::to_string(SCREEN_WIDTH) + " x " + std::to_string(SCREEN_HEIGHT));
        fullscreenLabel.setString(fullscreen ? sf::String(L"Режим: полный экран") : sf::String(L"Режим: оконный"));
        if (fullscreenModes.empty()) {
            fullscreenLabel.setString(sf::String(L"Полный экран недоступен"));
        }
        centerText(pauseTitle, {cx, cy - 185.f});
        centerText(resumeLabel, {cx, cy - 95.f});
        centerText(resolutionLabel, {cx, cy - 155.f});
        centerText(fullscreenLabel, {cx, cy - 75.f});
        centerText(exitLabel, {cx, cy + 65.f});
        centerText(settingsLabel, {cx, cy - 15.f});
        centerText(settingsTitle, {cx, cy - 245.f});
        centerText(backLabel, {cx, backY + 30.f});
        centerText(screenTabLabel, {cx, cy - 155.f});
        centerText(controlsTabLabel, {cx, cy - 75.f});
        centerText(sensitivityTitle, {cx, cy - 180.f});
        centerText(sensitivityLabel, {cx, cy - 135.f});
        centerText(sensitivityHint, {cx, cy - 50.f});
        for (int i = 0; i < 8; ++i) {
            bindingAreas[i] = sf::FloatRect({cx - 230.f + (i % 2) * 240.f, cy - 15.f + (i / 2) * 34.f}, {220.f, 30.f});
        }
        if (!mainMenu && (level.won || combat.hp == 0)) {
            resumeButton.setPosition({cx - 230.f, cy - 125.f});
            exitButton.setPosition({cx - 230.f, cy - 45.f});
            centerText(resumeLabel, {cx, cy - 95.f});
            centerText(exitLabel, {cx, cy - 15.f});
        }
    };
    layoutMenu();
    const auto updateSensitivity = [&](float mouseX) {
        const float ratio = std::clamp((mouseX - sensitivityTrack.getPosition().x) / 400.f, 0.f, 1.f);
        const int value = 10 + int(std::lround(ratio * 490.f));
        if (value != sensitivityPercent) {
            sensitivityPercent = value;
            settingsDirty = true;
            layoutMenu();
        }
    };
    const auto applyDisplayMode = [&]() -> bool {
        const auto mode = fullscreen ? fullscreenModes[fullscreenIndex] : sf::VideoMode(windowSizes[windowSizeIndex]);
        window.create(mode, "Game", windowStyle, fullscreen ? sf::State::Fullscreen : sf::State::Windowed);
        if (!window.isOpen()) {
            return false;
        }
        settingsDirty = true;
        SCREEN_WIDTH = window.getSize().x;
        SCREEN_HEIGHT = window.getSize().y;
        window.setView(sf::View(sf::FloatRect({0.f, 0.f}, {float(SCREEN_WIDTH), float(SCREEN_HEIGHT)})));
        window.setFramerateLimit(60);
        window.setKeyRepeatEnabled(false);
        window.setMouseCursorVisible(paused);
        mouseCenter = {int(SCREEN_WIDTH / 2), int(SCREEN_HEIGHT / 2)};
        layoutMenu();
        frameClock.restart();
        return true;
    };
    const auto setPaused = [&](bool value) {
        draggingSensitivity = false;
        if (!value) {
            settingsOpen = false;
        }
        paused = mainMenu || level.won || combat.hp == 0 || value;
        layoutMenu();
        window.setMouseCursorVisible(paused);
        if (!paused) {
            const auto size = window.getSize();
            mouseCenter = {int(size.x / 2), int(size.y / 2)};
            sf::Mouse::setPosition(mouseCenter, window);
        }
        frameClock.restart();
    };
    window.setKeyRepeatEnabled(false);

    const auto enterLevel = [&](const std::filesystem::path& path, bool carry) {
        if (!level.load(path, carry, error)) {
            throw std::runtime_error(error);
        }
        playerX = map.getPlayerStart().x;
        playerY = map.getPlayerStart().y;
        playerAngle = map.getPlayerStart().angle;
        frameClock.restart();
        sf::Mouse::setPosition(mouseCenter, window);
    };
    if (victoryCheck) {
        mainMenu = false;
        level.won = true;
        setPaused(true);
    }
    int checkFrames = 0;
    while (window.isOpen()) {
        bool fireRequested = false;
        float dt = std::min(frameClock.restart().asSeconds(), 0.05f);
        while (auto event = window.pollEvent()) {
            layoutMenu();
            if (event->is<sf::Event::Closed>()) {
                window.close();
            }

            if (event->is<sf::Event::FocusLost>()) {
                setPaused(true);
                dt = 0.f;
            }
            if (const auto* moved = event->getIf<sf::Event::MouseMoved>(); moved && draggingSensitivity) {
                updateSensitivity(window.mapPixelToCoords(moved->position).x);
            }
            if (const auto* released = event->getIf<sf::Event::MouseButtonReleased>(); released && released->button == sf::Mouse::Button::Left && draggingSensitivity) {
                updateSensitivity(window.mapPixelToCoords(released->position).x);
                draggingSensitivity = false;
            }
            if (bindingCapture >= 0) {
                const int code = Bindings::code(*event);
                if (code == int(sf::Keyboard::Scancode::Escape)) {
                    bindingCapture = -1;
                    continue;
                }
                if (code >= -int(sf::Mouse::ButtonCount) && code < int(sf::Keyboard::ScancodeCount)) {
                    for (int i = 0; i < 8; ++i) {
                        if (i != bindingCapture && bindings.keys[i] == code) {
                            bindings.keys[i] = bindings.keys[bindingCapture];
                        }
                    }
                    bindings.keys[bindingCapture] = code;
                    bindingCapture = -1;
                    settingsDirty = true;
                    continue;
                }
            }
            if (!paused && window.hasFocus() && combat.hp > 0) {
                if (Bindings::code(*event) == bindings.keys[5]) {
                    combat.selectWeapon(1);
                }
                if (Bindings::code(*event) == bindings.keys[6]) {
                    combat.selectWeapon(2);
                }
            }
            if (!paused && combat.hp > 0 && Bindings::code(*event) == bindings.keys[4]) {
                if (map.canUseExit(playerX, playerY, playerAngle)) {
                    if (map.victoryExit) {
                        if (level.useExit(playerX, playerY, playerAngle, error)) {
                            settingsOpen = false;
                            setPaused(true);
                            dt = 0;
                            fireRequested = false;
                            break;
                        }
                    } else {
                        enterLevel(map.nextLevel, true);
                        dt = 0;
                        fireRequested = false;
                        break;
                    }
                } else {
                    map.use(playerX, playerY, playerAngle);
                }
            }

            if (!paused && window.hasFocus() && combat.hp > 0 && Bindings::code(*event) == bindings.keys[7]) {
                fireRequested = true;
            }
            if (auto* keyPressed = event->getIf<sf::Event::KeyPressed>(); keyPressed && window.hasFocus()) {
                if (keyPressed->scancode == sf::Keyboard::Scancode::Escape) {
                    draggingSensitivity = false;
                    if (level.won || (!mainMenu && combat.hp == 0)) {
                        level.won = false;
                        mainMenu = true;
                        setPaused(true);
                    } else if (paused && settingsOpen) {
                        if (settingsTab == SettingsTab::Home) {
                            settingsOpen = false;
                        } else {
                            settingsTab = SettingsTab::Home;
                        }
                    } else if (!mainMenu) {
                        setPaused(!paused);
                    }
                    dt = 0.f;
                }
            }
            if (auto* click = event->getIf<sf::Event::MouseButtonPressed>(); click && paused && window.hasFocus() && click->button == sf::Mouse::Button::Left) {
                draggingSensitivity = false;
                const auto position = window.mapPixelToCoords(click->position);
                if (!settingsOpen) {
                    if (resumeButton.getGlobalBounds().contains(position)) {
                        if (mainMenu || level.won || combat.hp == 0) {
                            enterLevel(mapPath, false);
                            playerX = map.getPlayerStart().x;
                            playerY = map.getPlayerStart().y;
                            playerAngle = map.getPlayerStart().angle;
                        }
                        mainMenu = false;
                        fireRequested = false;
                        setPaused(false);
                        dt = 0.f;
                    } else if ((mainMenu || (!level.won && combat.hp > 0)) && settingsButton.getGlobalBounds().contains(position)) {
                        settingsOpen = true;
                        settingsTab = SettingsTab::Home;
                    } else if (exitButton.getGlobalBounds().contains(position)) {
                        if (mainMenu) {
                            window.close();
                        } else {
                            level.won = false;
                            mainMenu = true;
                            fireRequested = false;
                            setPaused(true);
                            dt = 0.f;
                        }
                    }
                } else if (backButton.getGlobalBounds().contains(position)) {
                    if (settingsTab == SettingsTab::Home) {
                        settingsOpen = false;
                    } else {
                        settingsTab = SettingsTab::Home;
                    }
                } else if (settingsTab == SettingsTab::Home && screenTabButton.getGlobalBounds().contains(position)) {
                    settingsTab = SettingsTab::Screen;
                } else if (settingsTab == SettingsTab::Home && controlsTabButton.getGlobalBounds().contains(position)) {
                    settingsTab = SettingsTab::Controls;
                } else if (settingsTab == SettingsTab::Screen) {
                    if (resolutionButton.getGlobalBounds().contains(position)) {
                        if (fullscreen) {
                            fullscreenIndex = (fullscreenIndex + 1) % fullscreenModes.size();
                        } else {
                            windowSizeIndex = (windowSizeIndex + 1) % windowSizes.size();
                        }
                        if (!applyDisplayMode()) {
                            return 1;
                        }
                        dt = 0.f;
                        break;
                    } else if (fullscreenButton.getGlobalBounds().contains(position) && !fullscreenModes.empty()) {
                        fullscreen = !fullscreen;
                        if (!applyDisplayMode()) {
                            return 1;
                        }
                        dt = 0.f;
                        break;
                    }
                } else if (settingsTab == SettingsTab::Controls && std::any_of(bindingAreas.begin(), bindingAreas.end(),
                    [&](const auto& area) {
                        return area.contains(position);
                    })) {
                    for (int i = 0; i < 8; ++i) {
                        if (bindingAreas[i].contains(position)) {
                            bindingCapture = i;
                        }
                    }
                } else if (settingsTab == SettingsTab::Controls && sensitivityHitArea.contains(position)) {
                    draggingSensitivity = true;
                    updateSensitivity(position.x);
                }
            }
        }
        if (draggingSensitivity) {
            if (window.hasFocus() && paused && settingsOpen && settingsTab == SettingsTab::Controls) {
                updateSensitivity(window.mapPixelToCoords(sf::Mouse::getPosition(window)).x);
                if (!sf::Mouse::isButtonPressed(sf::Mouse::Button::Left)) {
                    draggingSensitivity = false;
                }
            } else {
                draggingSensitivity = false;
            }
        }
        if (settingsDirty && (!draggingSensitivity || !window.isOpen())) {
            saveSettings();
        }
        if (!window.isOpen()) {
            break;
        }

        if (!paused && window.hasFocus() && combat.hp > 0) {
            map.update(dt, playerX, playerY, playerRadius);
            const auto size = window.getSize();
            mouseCenter = {int(size.x / 2), int(size.y / 2)};
            sf::Vector2i mousePosition = sf::Mouse::getPosition(window);

            float mouseDeltaX = mousePosition.x - mouseCenter.x;

            playerAngle += mouseDeltaX * baseMouseSensitivity * (sensitivityPercent / 100.f);
            playerAngle = std::fmod(playerAngle, TWO_PI);

            if (playerAngle < 0.0f) {
                playerAngle += TWO_PI;
            }

            sf::Mouse::setPosition(mouseCenter, window);

            float moveX = std::cos(playerAngle) * moveSpeed * dt * 60.f;
            float moveY = std::sin(playerAngle) * moveSpeed * dt * 60.f;
            float strafeX = std::sin(playerAngle) * moveSpeed * dt * 60.f;
            float strafeY = -std::cos(playerAngle) * moveSpeed * dt * 60.f;

            if (bindings.down(0)) {
                combat.movePlayer(map, playerX, playerY, moveX, moveY, playerRadius);
            }

            if (bindings.down(1)) {
                combat.movePlayer(map, playerX, playerY, -moveX, -moveY, playerRadius);
            }

            if (bindings.down(2)) {
                combat.movePlayer(map, playerX, playerY, strafeX, strafeY, playerRadius);
            }

            if (bindings.down(3)) {
                combat.movePlayer(map, playerX, playerY, -strafeX, -strafeY, playerRadius);
            }
        }

        if (!paused && window.hasFocus() && combat.hp > 0) {
            if (fireRequested) {
                combat.shoot(map, playerX, playerY, playerAngle);
            }
            combat.update(map, playerX, playerY, dt);
            if (combat.hp == 0) {
                settingsOpen = false;
                setPaused(true);
            }
        }

        window.clear((mainMenu || level.won) ? sf::Color(18, 26, 38) : sf::Color(30, 30, 30));

        if (!mainMenu && !level.won && combat.hp > 0) {
            renderer->draw(window.renderTarget(), map, playerX, playerY, playerAngle, combat);
        }

        if (!mainMenu && !level.won && combat.hp > 0) {
            drawHud(window.renderTarget(), font, combat, pistol, shotgun);
        }

        if (paused) {
            layoutMenu();
            const auto cursor = window.mapPixelToCoords(sf::Mouse::getPosition(window));
            const auto drawButton = [&](sf::RectangleShape& button, const sf::Text& label, bool selected = false, bool enabled = true) {
                const bool hover = enabled && button.getGlobalBounds().contains(cursor);
                button.setFillColor(!enabled   ? sf::Color(45, 45, 50) : selected ? sf::Color(65, 115, 180) : hover ? sf::Color(75, 125, 185)  : sf::Color(45, 70, 105));
                window.draw(button);
                window.draw(label);
            };
            if (!mainMenu && !level.won && combat.hp > 0) {
                window.draw(pauseShade);
            }
            if (!settingsOpen) {
                window.draw(pauseTitle);
                drawButton(resumeButton, resumeLabel);
                if (!level.won && (mainMenu || combat.hp > 0)) {
                    drawButton(settingsButton, settingsLabel);
                }
                drawButton(exitButton, exitLabel);
            } else {
                settingsTitle.setString(settingsTab == SettingsTab::Home ? sf::String(L"Настройки") : settingsTab == SettingsTab::Screen ? sf::String(L"Экран") : sf::String(L"Управление"));
                centerText(settingsTitle, {SCREEN_WIDTH / 2.f, SCREEN_HEIGHT / 2.f - 245.f});
                window.draw(settingsTitle);
                if (settingsTab == SettingsTab::Home) {
                    drawButton(screenTabButton, screenTabLabel);
                    drawButton(controlsTabButton, controlsTabLabel);
                } else if (settingsTab == SettingsTab::Screen) {
                    drawButton(resolutionButton, resolutionLabel);
                    drawButton(fullscreenButton, fullscreenLabel, false, !fullscreenModes.empty());
                } else {
                    window.draw(sensitivityTitle);
                    window.draw(sensitivityLabel);
                    window.draw(sensitivityHint);
                    for (int i = 0; i < 8; ++i) {
                        sf::RectangleShape row(bindingAreas[i].size);
                        row.setPosition(bindingAreas[i].position);
                        row.setFillColor(bindingCapture == i ? sf::Color(65, 115, 180) : sf::Color(45, 70, 105));
                        window.draw(row);
                        sf::Text label(font, actionNames[i] + L": " + (bindingCapture == i ? sf::String(L"Нажмите…") : bindings.name(i)), 17);
                        label.setPosition(bindingAreas[i].position + sf::Vector2f{8.f, 4.f});
                        window.draw(label);
                    }
                    sensitivityHandle.setFillColor(draggingSensitivity || sensitivityHitArea.contains(cursor) ? sf::Color(180, 215, 255) : sf::Color(110, 175, 250));
                    window.draw(sensitivityTrack);
                    window.draw(sensitivityFill);
                    window.draw(sensitivityHandle);
                }
                drawButton(backButton, backLabel);
            }
        }
        if (renderCheck && checkFrames == 2) {

            auto* target = dynamic_cast<sf::RenderTexture*>(&window.renderTarget());
            if (target) {
                target->display();
                if (!target->getTexture().copyToImage().saveToFile(executableDirectory / "tests" / (victoryCheck ? "game-victory.png" : "game-menu.png"))) {
                    return 2;
                }
            } else {
                sf::Texture capture;
                if (!capture.resize(window.getSize())) {
                    return 2;
                }
                capture.update(window);
                if (!capture.copyToImage().saveToFile(executableDirectory / "tests" / (victoryCheck ? "game-victory.png" : "game-menu.png"))) {
                    return 2;
                }
            }
        }
        window.display();
        if (renderCheck && ++checkFrames == 3) {
            window.close();
            break;
        }
    }
    return 0;
}

int main(int argc, char* argv[]) {
    SilentErrorOutput silentErrors;
    auto* previousErrors = sf::err().rdbuf(&silentErrors);
    int result = 1;

    try {
        result = runGame(argc, argv);
    } catch (...) {
        result = 1;
    }

    sf::err().rdbuf(previousErrors);
    return result;
}