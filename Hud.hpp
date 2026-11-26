#pragma once
#include <SFML/Graphics.hpp>
#include "Combat.hpp"
#include "HealthStyle.hpp"

inline void drawHud(sf::RenderTarget& target, const sf::Font& font, const Combat& combat, const sf::Texture& pistol, const sf::Texture& shotgun) {
    const float w = float(target.getSize().x), h = float(target.getSize().y);
    auto rect = [&](float x, float y, float width, float height, sf::Color color) {
        sf::RectangleShape r({width, height});
        r.setPosition({x, y});
        r.setFillColor(color);
        target.draw(r);
    };
    auto label = [&](float x, float y, const sf::String& s, unsigned size) {
        sf::Text t(font, s, size);
        t.setPosition({x, y});
        target.draw(t);
    };
    const auto& weaponTexture = combat.weapon == 2 ? shotgun : pistol;

    const float gunHeight = h * (combat.weapon == 2 ? 0.495f : 0.405f);
    const float recoil = combat.flash > 0 ? h * 0.018f : 0.f;
    const int cell = int(weaponTexture.getSize().y);
    sf::Sprite gun(weaponTexture, sf::IntRect({combat.flash > 0 ? cell : 0, 0}, {cell, cell}));
    float scale = gunHeight / cell;
    gun.setScale({scale, scale});
    gun.setPosition({w / 2 - cell * scale / 2, h - gunHeight + recoil});
    target.draw(gun);
    sf::Color cross = combat.hit > 0 ? sf::Color::Red : sf::Color::White;
    rect(w / 2 - 10, h / 2 - 1, 7, 2, cross);
    rect(w / 2 + 3, h / 2 - 1, 7, 2, cross);
    rect(w / 2 - 1, h / 2 - 10, 2, 7, cross);
    rect(w / 2 - 1, h / 2 + 3, 2, 7, cross);
    if (combat.hurt > 0) {
        rect(0, 0, w, h, sf::Color(210, 0, 0, 55));
    }
    rect(16, h - 88, 260, 72, HealthStyle::panel);
    label(28, h - 84, "HP: " + std::to_string(combat.hp) + " / 100", 24);
    rect(28, h - 43, 232, 15, HealthStyle::empty);
    rect(28, h - 43, 232 * combat.hp / 100.f, 15, HealthStyle::fill(combat.hp / 100.f));

    rect(w - 350, h - 118, 334, 102, sf::Color(12, 18, 27, 240));
    auto ammoLabel = [&](float y, const sf::String& name, int amount, int maximum, bool selected, bool owned) {
        sf::Text t(font, name + std::to_string(amount) + " / " + std::to_string(maximum), 28);
        t.setStyle(sf::Text::Bold);
        t.setFillColor(!owned ? sf::Color(255, 145, 125) : sf::Color::White);
        t.setOutlineColor(sf::Color(0, 0, 0, 230));
        t.setOutlineThickness(1.f);
        t.setPosition({w - 332, y});
        target.draw(t);
        if (selected) {
            rect(w - 350, y + 5, 4, 32, sf::Color(255, 205, 85));
        }
    };
    ammoLabel(h - 108, sf::String(L"Револьвер: "), combat.ammo, Combat::maxAmmo, combat.weapon == 1, true);
    ammoLabel(h - 64, sf::String(L"Дробовик: "), combat.shells, Combat::maxShells, combat.weapon == 2, combat.hasShotgun);
}