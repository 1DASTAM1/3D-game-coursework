#pragma once
#include <SFML/Graphics.hpp>

namespace HealthStyle {
    inline const sf::Color panel{15, 20, 28, 225};
    inline const sf::Color empty{80, 28, 28};
    inline const sf::Color healthy{50, 200, 95};
    inline const sf::Color low{235, 65, 45};
    inline sf::Color fill(float fraction) {
        return fraction > 0.3f ? healthy : low;
    }
}