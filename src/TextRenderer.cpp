#include "TextRenderer.h"
#include <fstream>
#include <vector>
#include <algorithm>
#include <span>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#include "embedded_font.h"

bool TextRenderer::init(SDL_Renderer* renderer, const std::string& fontPath, const float fontSize)
{
    m_renderer = renderer;
    m_fontSize = fontSize;

    if (!fontPath.empty())
    {
        std::ifstream file(fontPath, std::ios::binary | std::ios::ate);
        if (file.is_open())
        {
            const std::streamsize fileSize = file.tellg();
            file.seekg(0, std::ios::beg);
            m_fontData.resize(fileSize);
            if (file.read(reinterpret_cast<char*>(m_fontData.data()), fileSize))
            {
                return buildAtlas();
            }
        }
        SDL_Log("Failed to read font file: %s, using embedded font", fontPath.c_str());
    }

    m_fontData.assign(embedded_font_data, embedded_font_data + embedded_font_data_len);
    return buildAtlas();
}

bool TextRenderer::init(SDL_Renderer* renderer, const std::span<const uint8_t> fontData, const float fontSize)
{
    m_renderer = renderer;
    m_fontSize = fontSize;
    m_fontData.assign(fontData.begin(), fontData.end());
    return buildAtlas();
}

TextRenderer::~TextRenderer()
{
    cleanup();
}

void TextRenderer::cleanup()
{
    if (m_atlasTexture)
    {
        SDL_DestroyTexture(m_atlasTexture);
        m_atlasTexture = nullptr;
    }
}

bool TextRenderer::rebuildAtlas(const float newFontSize)
{
    if (m_fontData.empty() || !m_renderer)
    {
        SDL_Log("Cannot rebuild atlas: not initialized");
        return false;
    }

    m_fontSize = newFontSize;
    cleanup();
    return buildAtlas();
}

bool TextRenderer::buildAtlas()
{
    stbtt_fontinfo font;
    if (!stbtt_InitFont(&font, m_fontData.data(), 0))
    {
        SDL_Log("Failed to initialize font");
        return false;
    }

    const float scale = stbtt_ScaleForPixelHeight(&font, m_fontSize);

    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(&font, &ascent, &descent, &lineGap);
    const float baseline = static_cast<float>(ascent) * scale;

    struct TempGlyphData
    {
        int width, height, xoff, yoff;
        float advance;
    };
    std::array<TempGlyphData, CHAR_COUNT> tempGlyphs{};

    int atlasWidth = 0;
    int atlasHeight = 0;

    for (int i = 0; i < CHAR_COUNT; ++i)
    {
        const int codepoint = FIRST_CHAR + i;

        int advance, leftSideBearing;
        stbtt_GetCodepointHMetrics(&font, codepoint, &advance, &leftSideBearing);

        int x0, y0, x1, y1;
        stbtt_GetCodepointBitmapBox(&font, codepoint, scale, scale, &x0, &y0, &x1, &y1);

        const int width = x1 - x0;
        const int height = y1 - y0;

        tempGlyphs[i] = {
            .width = width,
            .height = height,
            .xoff = x0,
            .yoff = y0,
            .advance = static_cast<float>(advance) * scale
        };

        atlasWidth += width + 1;
        atlasHeight = std::max(atlasHeight, height);
    }

    atlasWidth += 2;
    atlasHeight += 2;

    m_atlasWidth = atlasWidth;
    m_atlasHeight = atlasHeight;

    std::vector<uint8_t> atlasData(atlasWidth * atlasHeight, 0);

    int penX = 1;
    for (int i = 0; i < CHAR_COUNT; ++i)
    {
        const int codepoint = FIRST_CHAR + i;
        const auto& [width, height, xoff, yoff, advance] = tempGlyphs[i];

        if (width > 0 && height > 0)
        {
            stbtt_MakeCodepointBitmap(&font,
                                      atlasData.data() + penX + 1 * atlasWidth,
                                      width, height, atlasWidth,
                                      scale, scale,
                                      codepoint);
        }

        m_glyphs[i] = {
            .texCoords = {
                static_cast<float>(penX) / static_cast<float>(atlasWidth),
                1.0f / static_cast<float>(atlasHeight),
                static_cast<float>(width) / static_cast<float>(atlasWidth),
                static_cast<float>(height) / static_cast<float>(atlasHeight)
            },
            .bounds = {
                static_cast<float>(xoff),
                baseline + static_cast<float>(yoff),
                static_cast<float>(width),
                static_cast<float>(height)
            },
            .advance = advance
        };

        penX += width + 1;
    }

    std::vector<uint8_t> rgbaData(atlasWidth * atlasHeight * 4);
    for (size_t i = 0; i < atlasData.size(); ++i)
    {
        rgbaData[i * 4 + 0] = 255;
        rgbaData[i * 4 + 1] = 255;
        rgbaData[i * 4 + 2] = 255;
        rgbaData[i * 4 + 3] = atlasData[i];
    }

    m_atlasTexture = SDL_CreateTexture(m_renderer,
                                       SDL_PIXELFORMAT_RGBA32,
                                       SDL_TEXTUREACCESS_STATIC,
                                       atlasWidth, atlasHeight);

    if (!m_atlasTexture)
    {
        SDL_Log("Failed to create atlas texture: %s", SDL_GetError());
        return false;
    }

    SDL_SetTextureBlendMode(m_atlasTexture, SDL_BLENDMODE_BLEND);
    SDL_UpdateTexture(m_atlasTexture, nullptr, rgbaData.data(), atlasWidth * 4);

    return true;
}

void TextRenderer::renderText(const float x, const float y, const std::string_view text) const
{
    if (!m_atlasTexture || text.empty()) return;

    float penX = x;

    for (const char c : text)
    {
        if (c < FIRST_CHAR || c > LAST_CHAR) continue;

        const int glyphIndex = c - FIRST_CHAR;
        const auto& [texCoords, bounds, advance] = m_glyphs[glyphIndex];

        if (bounds.w > 0 && bounds.h > 0)
        {
            SDL_FRect srcRect = {
                texCoords.x * static_cast<float>(m_atlasWidth),
                texCoords.y * static_cast<float>(m_atlasHeight),
                texCoords.w * static_cast<float>(m_atlasWidth),
                texCoords.h * static_cast<float>(m_atlasHeight)
            };

            SDL_FRect dstRect = {
                penX + bounds.x,
                y + bounds.y,
                bounds.w,
                bounds.h
            };

            SDL_RenderTexture(m_renderer, m_atlasTexture, &srcRect, &dstRect);
        }

        penX += advance;
    }
}

geometry::Vec2f TextRenderer::measureText(const std::string_view text) const
{
    float width = 0.0f;
    float height = m_fontSize;

    for (const char c : text)
    {
        if (c < FIRST_CHAR || c > LAST_CHAR) continue;

        const int glyphIndex = c - FIRST_CHAR;
        width += m_glyphs[glyphIndex].advance;
    }

    return {width, height};
}
