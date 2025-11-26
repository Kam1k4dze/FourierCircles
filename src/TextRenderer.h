#pragma once

#include <SDL3/SDL.h>
#include <string>
#include <string_view>
#include <array>
#include <vector>
#include <span>
#include <cstdint>

#include "Vec2.h"

class TextRenderer
{
public:
    struct GlyphInfo
    {
        SDL_FRect texCoords;
        SDL_FRect bounds;
        float advance;
    };

    TextRenderer() = default;
    bool init(SDL_Renderer* renderer, const std::string& fontPath, float fontSize);
    bool init(SDL_Renderer* renderer, std::span<const uint8_t> fontData, float fontSize);

    ~TextRenderer();

    TextRenderer(const TextRenderer&) = delete;
    TextRenderer& operator=(const TextRenderer&) = delete;
    TextRenderer(TextRenderer&&) noexcept = default;
    TextRenderer& operator=(TextRenderer&&) noexcept = default;

    bool rebuildAtlas(float newFontSize);

    void renderText(float x, float y, std::string_view text) const;

    [[nodiscard]] geometry::Vec2f measureText(std::string_view text) const;

    [[nodiscard]] float getFontSize() const { return m_fontSize; }

    SDL_Texture* m_atlasTexture = nullptr;

private:
    bool buildAtlas();
    void cleanup();

    SDL_Renderer* m_renderer = nullptr;

    std::vector<uint8_t> m_fontData;
    float m_fontSize = 0.0f;

    static constexpr int FIRST_CHAR = 32;
    static constexpr int LAST_CHAR = 126;
    static constexpr int CHAR_COUNT = LAST_CHAR - FIRST_CHAR + 1;

    std::array<GlyphInfo, CHAR_COUNT> m_glyphs{};

    int m_atlasWidth = 0;
    int m_atlasHeight = 0;
};
