#pragma once
#include "Combat.hpp"
#include "TextureManager.hpp"
#include "SpriteAssets.hpp"
#include "HealthStyle.hpp"

class GpuRenderer {
    sf::Shader shader;
    sf::Texture atlas, metadata, cells, doorHeights, enemySprite;
    std::vector<std::uint8_t> heights;
    std::map<int, sf::Glsl::Vec4> enemyRects;
    std::map<int, float> spriteAspect;

public:
    bool load(const DoorMap& map, const TextureManager& textures, std::string& error, const Combat& combat) {
        if (!sf::Shader::isAvailable()) {
            error = "OpenGL fragment shaders are unavailable";
            return false;
        }
        std::map<int, unsigned> slots;
        unsigned tileW = 1, tileH = 1;
        for (int y = 0; y < map.getHeight(); ++y) {
            for (int x = 0; x < map.getWidth(); ++x) {
                const auto& c = map.getCell(x, y);
                for (int id : {c.wallTexture, c.floorTexture, c.ceilingTexture}) {
                    if (!slots.count(id)) {
                        const unsigned slot = static_cast<unsigned>(slots.size());
                        slots[id] = slot;
                        const auto size = textures.get(id).image.getSize();
                        tileW = std::max(tileW, size.x);
                        tileH = std::max(tileH, size.y);
                    }
                }
            }
        }
        if (slots.size() > 256) {
            error = "GPU renderer supports up to 256 distinct map textures";
            return false;
        }
        const unsigned columns = static_cast<unsigned>(std::ceil(std::sqrt(double(slots.size()))));
        const unsigned rows = (static_cast<unsigned>(slots.size()) + columns - 1) / columns;
        const unsigned limit = sf::Texture::getMaximumSize();
        if (tileW > 65535 || tileH > 65535 || columns * tileW > limit || rows * tileH > limit) {
            error = "Texture atlas exceeds the GPU texture size limit";
            return false;
        }
        sf::Image atlasImage({columns * tileW, rows * tileH}, sf::Color::Black);
        sf::Image sizes({256, 1}, sf::Color::Black);
        for (const auto& [id, slot] : slots) {
            const auto& img = textures.get(id).image;
            const auto size = img.getSize();
            if (!atlasImage.copy(img, {(slot % columns) * tileW, (slot / columns) * tileH})) {
                error = "Cannot pack texture atlas";
                return false;
            }
            sizes.setPixel({slot, 0}, sf::Color(size.x & 255, size.x >> 8, size.y & 255, size.y >> 8));
        }
        const sf::Vector2u mapSize{unsigned(map.getWidth()), unsigned(map.getHeight())};
        sf::Image cellImage(mapSize);
        for (unsigned y = 0; y < mapSize.y; ++y) {
            for (unsigned x = 0; x < mapSize.x; ++x) {
                const auto& c = map.getCell(int(x), int(y));
                cellImage.setPixel({x, y}, sf::Color(slots.at(c.wallTexture), slots.at(c.floorTexture), slots.at(c.ceilingTexture), c.isWall ? 1 : 0));
            }
        }
        for (const auto& d : map.doors) {
            auto color = cellImage.getPixel({unsigned(d.x), unsigned(d.y)});
            color.a = 2 + int(d.shape);
            cellImage.setPixel({unsigned(d.x), unsigned(d.y)}, color);
        }
        if (!atlas.loadFromImage(atlasImage) || !metadata.loadFromImage(sizes) || !cells.loadFromImage(cellImage) || !doorHeights.resize(mapSize)) {
            error = "Cannot allocate GPU scene textures";
            return false;
        }
        heights.resize(std::size_t(mapSize.x) * mapSize.y * 4);
        if (!shader.loadFromMemory(fragmentSource(), sf::Shader::Type::Fragment)) {
            error = "Cannot compile GPU ray-casting shader";
            return false;
        }
        std::map<int, sf::Image> spriteImages;
        for (const auto& [type, path] : combat.enemyTextures) {
            std::array<sf::Image, 6> frames;
            if (!loadEnemyFrames(path, frames, error)) {
                return false;
            }
            for (int frame = 0; frame < 6; ++frame) {
                spriteImages[type * 10 + frame] = frame == 1 ? cropSprite(frames[frame]) : frames[frame];
            }
        }
        std::map<int, std::filesystem::path> paths;
        for (const auto& [id, def] : combat.definitions) {
            paths[-id] = def.texture;
        }
        paths[-100] = combat.effectTextures.at("dynamite");
        paths[-101] = combat.effectTextures.at("explosion");
        for (const auto& [id, path] : paths) {
            sf::Image image;
            if (!image.loadFromFile(path)) {
                error = "Cannot load sprite: " + path.string();
                return false;
            }
            spriteImages[id] = cropSprite(image);
        }
        unsigned sw = 1, sh = 1;
        spriteAspect.clear();
        for (auto& [id, img] : spriteImages) {
            img = atlasSprite(img);
            auto size = img.getSize();
            spriteAspect[id] = float(size.x) / size.y;
            sw = std::max(sw, size.x);
            sh = std::max(sh, size.y);
        }
        unsigned cols = unsigned(std::ceil(std::sqrt(double(spriteImages.size()))));
        unsigned spriteRows = (unsigned(spriteImages.size()) + cols - 1) / cols;
        if (sw * cols > limit || sh * spriteRows > limit) {
            error = "Enemy atlas exceeds GPU limit";
            return false;
        }
        sf::Image sprites({sw * cols, sh * spriteRows}, sf::Color::Transparent);
        enemyRects.clear();
        unsigned index = 0;
        for (const auto& [id, img] : spriteImages) {
            unsigned x = (index % cols) * sw, y = (index / cols) * sh;
            if (!sprites.copy(img, {x, y})) {
                error = "Cannot pack enemy sprites";
                return false;
            }
            auto size = img.getSize();
            enemyRects[id] = sf::Glsl::Vec4((x + 0.5f) / (sw * cols), (y + 0.5f) / (sh * spriteRows), (size.x - 1.f) / (sw * cols), (size.y - 1.f) / (sh * spriteRows));
            ++index;
        }
        if (!enemySprite.loadFromImage(sprites)) {
            error = "Cannot upload enemy sprites";
            return false;
        }
        shader.setUniform("enemySprite", enemySprite);
        shader.setUniform("atlas", atlas);
        shader.setUniform("metadata", metadata);
        shader.setUniform("cells", cells);
        shader.setUniform("doorHeights", doorHeights);
        shader.setUniform("mapSize", sf::Glsl::Vec2(float(mapSize.x), float(mapSize.y)));
        shader.setUniform("tileSize", sf::Glsl::Vec2(float(tileW), float(tileH)));
        shader.setUniform("atlasSize", sf::Glsl::Vec2(float(columns * tileW), float(rows * tileH)));
        shader.setUniform("columns", float(columns));
        shader.setUniform("hpPanel", sf::Glsl::Vec4(HealthStyle::panel));
        shader.setUniform("hpEmpty", sf::Glsl::Vec4(HealthStyle::empty));
        shader.setUniform("hpHealthy", sf::Glsl::Vec4(HealthStyle::healthy));
        shader.setUniform("hpLow", sf::Glsl::Vec4(HealthStyle::low));

        return true;
    }
    void draw(sf::RenderTarget& target, const DoorMap& map, float x, float y, float angle, const Combat& combat) {
        for (const auto& d : map.doors) {
            const auto at = (std::size_t(d.y) * map.getWidth() + d.x) * 4;
            const unsigned value = unsigned(std::lround(std::clamp(d.open, 0.f, 1.f) * 65535.f));
            heights[at] = value & 255;
            heights[at + 1] = value >> 8;
        }
        doorHeights.update(heights.data());
        const auto size = target.getSize();
        shader.setUniform("screenSize", sf::Glsl::Vec2(float(size.x), float(size.y)));
        shader.setUniform("eye", sf::Glsl::Vec2(x, y));
        shader.setUniform("forward", sf::Glsl::Vec2(std::cos(angle), std::sin(angle)));
        shader.setUniform("enemyCount", int(combat.enemies.size()));
        sf::Glsl::Vec4 actors[16]{}, spriteRects[16]{}, effects[16]{}, effectRects[16]{}, effectSizes[16]{};
        float health[16]{};
        for (std::size_t i = 0; i < combat.enemies.size(); ++i) {
            const auto& e = combat.enemies[i];
            int key = e.typeId * 10 + combat.frame(i);
            spriteRects[i] = enemyRects.at(key);
            float width = 0.78f;
            actors[i] = sf::Glsl::Vec4(e.x, e.y, width, width / spriteAspect.at(key));
            health[i] = e.maxHp > 0 ? std::clamp(float(e.hp) / e.maxHp, 0.f, 1.f) : 0.f;
        }
        shader.setUniform("effectCount", int(combat.dynamites.size()));
        std::vector<const Dynamite*> sorted;
        for (const auto& g : combat.dynamites) {
            sorted.push_back(&g);
        }
        std::sort(sorted.begin(), sorted.end(), [&](const auto* a, const auto* b) {
            return (a->x - x) * std::cos(angle) + (a->y - y) * std::sin(angle) > (b->x - x) * std::cos(angle) + (b->y - y) * std::sin(angle);
        });
        for (std::size_t i = 0; i < sorted.size(); ++i) {
            const auto& g = *sorted[i];
            bool burst = g.fuse <= 0;
            int key = burst ? -101 : -100;
            float height = burst ? 0.85f : 0.22f;
            float center = burst ? 0.42f : 0.11f + 0.42f * std::sin(std::clamp(g.flight / 0.7f, 0.f, 1.f) * 3.14159265f);
            effects[i] = sf::Glsl::Vec4(g.x, g.y, center, burst ? std::min(1.f, g.burst / 0.1f) : 1.f);
            effectSizes[i] = sf::Glsl::Vec4(height * spriteAspect.at(key), height, 0, 0);
            effectRects[i] = enemyRects.at(key);
        }
        shader.setUniformArray("effects", effects, 16);
        shader.setUniformArray("effectSizes", effectSizes, 16);
        shader.setUniformArray("effectRects", effectRects, 16);
        shader.setUniformArray("enemies", actors, 16);
        shader.setUniformArray("enemyRects", spriteRects, 16);
        shader.setUniformArray("enemyHealth", health, 16);
        sf::Glsl::Vec4 pickups[32]{}, pickupRects[32]{};
        int count = 0;
        for (const auto& item : combat.items) {
            if (!item.collected) {
                const auto& def = combat.definitions.at(item.id);
                pickups[count] = sf::Glsl::Vec4(item.x, item.y, def.width, def.height);
                pickupRects[count] = enemyRects.at(-item.id);
                ++count;
            }
        }
        shader.setUniform("pickupCount", count);
        shader.setUniformArray("pickups", pickups, 32);
        shader.setUniformArray("pickupRects", pickupRects, 32);
        sf::RectangleShape screen({float(size.x), float(size.y)});
        sf::RenderStates states;
        states.shader = &shader;
        states.blendMode = sf::BlendNone;
        target.draw(screen, states);
    }

private:
    static const char* fragmentSource() {
        return R"GLSL(

#version 120
uniform sampler2D atlas, metadata, cells, doorHeights, enemySprite;
uniform vec2 mapSize, tileSize, atlasSize, screenSize, eye, forward;
uniform float columns;
uniform int enemyCount, pickupCount;

uniform vec4 pickups[32], pickupRects[32];
uniform vec4 enemies[16], effects[16], effectSizes[16], effectRects[16];
uniform int effectCount;
uniform vec4 enemyRects[16];
uniform float enemyHealth[16];
uniform vec4 hpPanel, hpEmpty, hpHealthy, hpLow;
vec4 cellAt(vec2 cell) {
    return floor(texture2D(cells, (clamp(cell, vec2(0.0), mapSize - 1.0) + 0.5) / mapSize) * 255.0 + 0.5);
}
vec4 sampleMaterial(float slot, vec2 uv) {
    vec4 encoded = floor(texture2D(metadata, vec2((slot + 0.5) / 256.0, 0.5)) * 255.0 + 0.5);
    vec2 size = vec2(encoded.r + encoded.g * 256.0, encoded.b + encoded.a * 256.0);
    vec2 pixel = floor(clamp(uv, 0.0, 0.999999) * size);

    float row = floor((slot + 0.5) / columns);
    vec2 origin = vec2(slot - row * columns, row) * tileSize;
    return texture2D(atlas, (origin + pixel + 0.5) / atlasSize);
}
void doorBounds(vec2 cell, float kind, out vec2 low, out vec2 high) {
    low = cell;
    high = cell + 1.0;
    if (kind == 2.0) {
        low.x += 0.47;
        high.x -= 0.47;
    }
    if (kind == 3.0) {
        low.y += 0.47;
        high.y -= 0.47;
    }
}
bool slab(float origin, float direction, float low, float high, float axis, inout float entry, inout float leave, inout float face) {
    if (abs(direction) < 0.0000001) {
        return origin >= low && origin <= high;
    }
    float a = (low - origin) / direction, b = (high - origin) / direction;
    float nearT = min(a, b), farT = max(a, b);
    if (nearT > entry) {
        entry = nearT;
        face = axis;
    }
    leave = min(leave, farT);
    return leave >= entry;
}
void main() {
    float cameraX = 2.0 * gl_FragCoord.x / screenSize.x - 1.0;
    vec2 ray = forward + vec2(-forward.y, forward.x) * cameraX * 0.577350269;
    float dz = (gl_FragCoord.y - screenSize.y * 0.5) / screenSize.y;
    float best = 1.0e20;
    vec4 color = vec4(0.0, 0.0, 0.0, 1.0);

    if (abs(dz) > 0.0000001) {
        best = 0.5 / abs(dz);
        vec2 point = eye + best * ray;
        vec2 cell = floor(point);
        vec4 data = cellAt(cell);
        float material = dz > 0.0 ? data.b : data.g;
        color = sampleMaterial(material, fract(point));
        if (dz > 0.0 && data.a >= 2.0) {
            vec2 low, high;
            doorBounds(cell, data.a, low, high);
            if (all(greaterThanEqual(point, low)) && all(lessThanEqual(point, high))) {
                vec4 panel = sampleMaterial(data.r, fract(point));
                if (panel.a >= 0.5) {
                    color = panel;
                }
            }
        }
    }
    vec2 cell = floor(eye);
    vec2 stepDir = vec2(ray.x < 0.0 ? -1.0 : 1.0, ray.y < 0.0 ? -1.0 : 1.0);
    vec2 delta = 1.0 / max(abs(ray), vec2(0.0000001));
    vec2 edge = vec2(ray.x < 0.0 ? eye.x - cell.x : cell.x + 1.0 - eye.x, ray.y < 0.0 ? eye.y - cell.y : cell.y + 1.0 - eye.y) * delta;
    float entry = 0.0, side = 0.0;

    for (int i = 0; i < 2048; ++i) {
        if (entry > best || any(lessThan(cell, vec2(0.0))) || any(greaterThanEqual(cell, mapSize))) {
            break;
        }
        vec4 data = cellAt(cell);
        if (data.a == 1.0) {
            vec2 point = eye + entry * ray;
            float u = fract(side == 0.0 ? point.y : point.x);
            if ((side == 0.0 && ray.x > 0.0) || (side == 1.0 && ray.y < 0.0)) {
                u = 1.0 - u;
            }
            color = sampleMaterial(data.r, vec2(u, 0.5 - entry * dz));
            best = entry;
            break;
        }
        if (data.a >= 2.0) {
            vec4 encoded = floor(texture2D(doorHeights, (cell + 0.5) / mapSize) * 255.0 + 0.5);
            float raised = (encoded.r + encoded.g * 256.0) / 65535.0;
            if (raised < 1.0) {
                vec2 low, high;
                doorBounds(cell, data.a, low, high);
                float nearT = 0.0, farT = 1.0e20, face = 0.0;
                bool hit = slab(eye.x, ray.x, low.x, high.x, 0.0, nearT, farT, face);
                if (hit) {
                    hit = slab(eye.y, ray.y, low.y, high.y, 1.0, nearT, farT, face);
                }
                if (hit) {
                    hit = slab(0.5, dz, raised, 1.0, 2.0, nearT, farT, face);
                }
                if (hit && nearT > 0.0 && nearT < best) {
                    vec2 point = eye + nearT * ray - cell;
                    vec2 uv = face == 2.0 ? point : vec2(face == 0.0 ? point.y : point.x, 0.5 - nearT * dz + raised);
                    vec4 panel = sampleMaterial(data.r, uv);

                    if (panel.a >= 0.5) {
                        color = panel;
                        best = nearT;
                    }
                }
            }
        }
        if (edge.x < edge.y) {
            entry = edge.x;
            edge.x += delta.x;
            cell.x += stepDir.x;
            side = 0.0;
        } else {
            entry = edge.y;
            edge.y += delta.y;
            cell.y += stepDir.y;
            side = 1.0;
        }
    }

    vec2 right = vec2(-forward.y, forward.x);
    for (int i = 0; i < 16; ++i) {
        if (i >= enemyCount) {
            break;
        }
        vec4 actor = enemies[i];
        float t = dot(actor.xy - eye, forward);
        if (t <= 0.05 || t >= best) {
            continue;
        }
        float u = dot(eye + t * ray - actor.xy, right), z = 0.5 + t * dz;
        vec2 uv = vec2(0.5 + u / actor.z, 1.0 - z / actor.w);
        if (uv.x >= 0.0 && uv.x <= 1.0 && uv.y >= 0.0 && uv.y <= 1.0) {
            vec4 texel = texture2D(enemySprite, enemyRects[i].xy + uv * enemyRects[i].zw);
            if (texel.a >= 0.5) {
                best = t;
                color = vec4(texel.rgb, 1.0);
            }
        }
        if (enemyHealth[i] > 0.0 && abs(u) < 0.23 && z > 0.81 && z < 0.875) {
            vec4 bar = hpPanel;
            if (abs(u) < 0.205 && z > 0.827 && z < 0.858) {
                bar = hpEmpty;
                if (u + 0.205 < 0.41 * enemyHealth[i]) {
                    bar = enemyHealth[i] > 0.3 ? hpHealthy : hpLow;
                }
            }
            color = vec4(mix(color.rgb, bar.rgb, bar.a), 1.0);
            best = t;
        }
    }
    for (int i = 0; i < 32; ++i) {
        if (i >= pickupCount) {
            break;
        }
        vec4 item = pickups[i];
        float t = dot(item.xy - eye, forward);
        if (t <= 0.05 || t >= best) {
            continue;
        }
        float u = dot(eye + t * ray - item.xy, right);
        float z = 0.5 + t * dz;
        vec2 uv = vec2(0.5 + u / item.z, 1.0 - z / item.w);
        if (uv.x >= 0.0 && uv.x <= 1.0 && uv.y >= 0.0 && uv.y <= 1.0) {
            vec4 texel = texture2D(enemySprite, pickupRects[i].xy + uv * pickupRects[i].zw);
            if (texel.a >= 0.5) {
                best = t;
                color = vec4(texel.rgb, 1.0);
            }
        }
    }

    for (int i = 0; i < 16; ++i) {
        if (i >= effectCount) {
            break;
        }
        vec4 item = effects[i];
        float t = dot(item.xy - eye, forward);
        if (t <= 0.05 || t >= best) {
            continue;
        }
        float u = dot(eye + t * ray - item.xy, right), z = 0.5 + t * dz;
        vec2 uv = vec2(0.5 + u / effectSizes[i].x, 0.5 - (z - item.z) / effectSizes[i].y);
        if (uv.x >= 0.0 && uv.x <= 1.0 && uv.y >= 0.0 && uv.y <= 1.0) {
            vec4 texel = texture2D(enemySprite, effectRects[i].xy + uv * effectRects[i].zw);
            color = vec4(mix(color.rgb, texel.rgb, texel.a * item.w), 1.0);
        }
    }
    gl_FragColor = color;
}
)GLSL";
    }
};