#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <SFML/Graphics.hpp>
#include <SFML/OpenGL.hpp>
#include <vector>
#include <stdexcept>

class GameWindow : public sf::RenderWindow {
    sf::RenderTexture frame;
    bool checked = false, compatible = false;
    unsigned fps = 0;
    sf::Clock presentationClock;
    std::vector<std::uint8_t> bgra;
    void prepare() {
        if (!checked) {
            if (!setActive(true)) {
                throw std::runtime_error("Cannot activate game window");
            }
            sf::RenderWindow::clear(sf::Color(37, 113, 211));
            unsigned char pixel[4]{};
            glReadPixels(GLint(getSize().x / 2), GLint(getSize().y / 2), 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
            compatible = pixel[0] != 37 || pixel[1] != 113 || pixel[2] != 211;
            checked = true;
        }
        if (compatible) {
            if (frame.getSize() != getSize() && !frame.resize(getSize())) {
                throw std::runtime_error("Cannot allocate game framebuffer");
            }
            frame.setView(sf::RenderWindow::getView());
        }
    }

public:
    using sf::RenderWindow::RenderWindow;
    template <class... Args> void create(Args&&... args) {
        frame = sf::RenderTexture{};
        sf::RenderWindow::create(std::forward<Args>(args)...);
        compatible = false;
        checked = false;
        presentationClock.restart();
    }
    void setFramerateLimit(unsigned value) {
        fps = value;
        sf::RenderWindow::setFramerateLimit(value);
    }
    sf::RenderTarget& renderTarget() {
        prepare();
        return compatible ? static_cast<sf::RenderTarget&>(frame) : static_cast<sf::RenderTarget&>(*this);
    }
    bool usesCompatibilityPresentation() {
        prepare();
        return compatible;
    }
    void clear(sf::Color color = sf::Color::Black) {
        renderTarget().clear(color);
    }
    void draw(const sf::Drawable& item, const sf::RenderStates& states = sf::RenderStates::Default) {
        renderTarget().draw(item, states);
    }
    void display() {
        prepare();
        if (!compatible) {
            sf::RenderWindow::display();
            return;
        }
        frame.display();
        const auto image = frame.getTexture().copyToImage();
        const auto size = image.getSize();
        bgra.resize(std::size_t(size.x) * size.y * 4);
        const auto* rgba = image.getPixelsPtr();
        for (std::size_t i = 0; i < bgra.size(); i += 4) {
            bgra[i] = rgba[i + 2];
            bgra[i + 1] = rgba[i + 1];
            bgra[i + 2] = rgba[i];
            bgra[i + 3] = 255;
        }
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = LONG(size.x);
        info.bmiHeader.biHeight = -LONG(size.y);
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        HWND handle = getNativeHandle();
        HDC dc = GetDC(handle);
        if (!dc) {
            throw std::runtime_error("Cannot acquire window DC");
        }
        int result = StretchDIBits(dc, 0, 0, int(size.x), int(size.y), 0, 0, int(size.x), int(size.y), bgra.data(), &info, DIB_RGB_COLORS, SRCCOPY);
        ReleaseDC(handle, dc);
        if (result == 0 || result == int(GDI_ERROR)) {
            throw std::runtime_error("Cannot present game frame");
        }
        if (fps) {
            auto remaining = sf::seconds(1.f / fps) - presentationClock.getElapsedTime();
            if (remaining > sf::Time::Zero) {
                sf::sleep(remaining);
            }
        }
        presentationClock.restart();
    }
};