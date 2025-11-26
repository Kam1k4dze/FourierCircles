#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_dialog.h>
#include <chrono>
#include <cmath>
#include <vector>
#include <numbers>
#include <algorithm>
#include <format>
#include <string>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include "FourierCircles.h"
#include "svg.h"
#include "TextRenderer.h"
#include "embedded_svg.h"

using geometry::Vec2f;


#ifdef __EMSCRIPTEN__
constexpr bool IS_EMSCRIPTEN = true;
#else
constexpr bool IS_EMSCRIPTEN = false;
#endif
constexpr float PI_F = std::numbers::pi_v<float>;
constexpr int INITIAL_WINDOW_W = 2560;
constexpr int INITIAL_WINDOW_H = 1440;
constexpr float SIMULATION_PERIOD = 60.0f; // Seconds for one full cycle
constexpr size_t CONTOUR_SAMPLES = 2000;
constexpr size_t CONTOUR_CACHE_RESERVE = static_cast<size_t>(CONTOUR_SAMPLES) + 2u;
constexpr size_t SVG_SAMPLE_COUNT = 100;
constexpr float SVG_INITIAL_OFFSET_X = 100.0f;
constexpr float SVG_INITIAL_SCALE = 1.5f;
constexpr float ZOOM_STEP = 1.1f;
constexpr float ZOOM_MIN = 0.01f;
constexpr float ZOOM_MAX = 500.0f;
constexpr float MIN_DRAWABLE_RADIUS = 1.0f;
constexpr float CIRCLE_SEGMENTS_PER_PIXEL = 3.0f;
constexpr uint8_t CIRCLE_SEGMENTS_MIN = 16;
constexpr uint8_t CIRCLE_SEGMENTS_MAX = 128;
constexpr size_t CIRCLE_POINT_BUFFER_SIZE = static_cast<size_t>(CIRCLE_SEGMENTS_MAX) + 2u;
constexpr Uint8 CIRCLE_OUTLINE_ALPHA = 40;
constexpr size_t MIN_ACTIVE_VECTOR_COUNT = 0;
constexpr size_t MIN_VECTOR_STEP = 1;
constexpr int UI_MARGIN_X = 10;
constexpr int UI_MARGIN_Y = 10;
constexpr float BASE_UI_FONT_SIZE = 20.0f;
constexpr float UI_LINE_SPACING_MULTIPLIER = 1.2f;
constexpr float ORIGINAL_POINT_MARKER_HALF_SIZE = 2.0f;
constexpr float ORIGINAL_POINT_MARKER_SIZE = ORIGINAL_POINT_MARKER_HALF_SIZE * 2.0f;
constexpr float TIP_MARKER_HALF_SIZE = 3.0f;
constexpr float TIP_MARKER_SIZE = TIP_MARKER_HALF_SIZE * 2.0f;
constexpr int WINDOW_TITLE_UPDATE_INTERVAL = 10;
constexpr float TIME_SCALE_STEP = 0.1f;
constexpr float TIME_SCALE_MIN = 0.1f;
constexpr float TIME_SCALE_MAX = 10.0f;


constexpr SDL_Color COLOR_BG = {15, 18, 25, 255};
constexpr SDL_Color COLOR_CONTOUR = {240, 84, 120, 255};
constexpr SDL_Color COLOR_SAMPLE_POINTS = {100, 160, 180, 200};
constexpr SDL_Color COLOR_TIP = {252, 191, 73, 255};
constexpr SDL_Color COLOR_CIRCLE = {255, 255, 255, CIRCLE_OUTLINE_ALPHA};
constexpr SDL_Color COLOR_UI_TEXT = {234, 226, 183, 255};
constexpr SDL_Color COLOR_DIALOG_OVERLAY = {10, 15, 20, 200};

constexpr SDL_Color ARM_COLORS[5] = {
    {0x63, 0x66, 0xF1, 255},
    {0x8B, 0x5C, 0xF6, 255},
    {0x06, 0xB6, 0xD4, 255},
    {0xF5, 0x9E, 0x0B, 255},
    {0x10, 0xB9, 0x81, 255},
};


struct Camera
{
    Vec2f position = {0.0f, 0.0f};
    float zoom = 1.0f;
    bool follow_mode = false;

    [[nodiscard]] Vec2f worldToScreen(const Vec2f v) const
    {
        return v * zoom + position;
    }

    [[nodiscard]] Vec2f worldToScreen(const float x, const float y) const
    {
        return Vec2f(x, y) * zoom + position;
    }

    [[nodiscard]] Vec2f screenToWorld(const Vec2f s) const
    {
        return (s - position) / zoom;
    }

    [[nodiscard]] Vec2f screenToWorld(const float x, const float y) const
    {
        return (Vec2f(x, y) - position) / zoom;
    }
};

struct AppState
{
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;

    FourierCircles fc;
    std::vector<Vec2f> original_points;
    std::vector<Vec2f> contour_cache;

    std::chrono::steady_clock::time_point last_tick;
    float accumulated_time = 0.0f;
    float time_scale = 1.0f;
    bool paused = false;

    size_t active_vectors = 0;
    size_t max_vectors = 0;
    size_t vector_step = MIN_VECTOR_STEP;
    bool dirty_contour = true;

    Camera cam;
    Vec2f current_tip = {0, 0};

    bool show_ui = true;
    bool show_original_points = false;
    bool show_sample_count_prompt = false;
    std::string sample_count_text;

    std::string current_svg_path;
    int svg_sample_count = SVG_SAMPLE_COUNT;

    bool is_dragging = false;
    Vec2f drag_start = {0, 0};
    Vec2f cam_start = {0, 0};

    TextRenderer textRenderer;
    float currentDpiScale = 1.0f;
    float currentFontSize = BASE_UI_FONT_SIZE;
};


SDL_FPoint* as_sdl_fpoints(Vec2f* v)
{
    static_assert(std::is_standard_layout_v<Vec2f>, "Vec2f must be standard-layout");
    static_assert(std::is_standard_layout_v<SDL_FPoint>, "SDL_FPoint must be standard-layout");
    static_assert(sizeof(Vec2f) == sizeof(SDL_FPoint), "Vec2f and SDL_FPoint must have identical size");
    static_assert(alignof(Vec2f) == alignof(SDL_FPoint), "Vec2f and SDL_FPoint must have identical alignment");
    static_assert(offsetof(Vec2f, x) == offsetof(SDL_FPoint, x), "x offset mismatch");
    static_assert(offsetof(Vec2f, y) == offsetof(SDL_FPoint, y), "y offset mismatch");
    return reinterpret_cast<SDL_FPoint*>(v);
}

void loadSVG(AppState* app, const std::string& path, const int sample_count)
{
    if (path.empty())
    {
        app->original_points = svg::readSVGCurveFromString(default_svg_content, sample_count);
    }
    else
    {
        app->original_points = svg::readSVGCurveFromFile(path, sample_count);
    }
    const bool failed = std::all_of(app->original_points.begin(), app->original_points.end(),
                                    [](const Vec2f& p) { return p == Vec2f{0.0f, 0.0f}; });

    if (failed)
    {
        SDL_Log("Failed to load SVG from file: %s, using embedded default", path.c_str());
        // Fall back to embedded SVG
        app->original_points = svg::readSVGCurveFromString(default_svg_content, sample_count);
    }

    for (auto& point : app->original_points)
    {
        point.x += SVG_INITIAL_OFFSET_X;
        point *= SVG_INITIAL_SCALE;
    }

    app->fc.calculateCoefficients(app->original_points);

    app->max_vectors = sample_count;
    app->active_vectors = app->max_vectors;
    app->dirty_contour = true;

    app->current_svg_path = path;
    app->svg_sample_count = sample_count;
    app->accumulated_time = 0.0f;
}


void regenerateContour(AppState* app)
{
    app->contour_cache.clear();
    app->contour_cache.reserve(CONTOUR_CACHE_RESERVE);

    const size_t count = std::min(app->active_vectors, app->max_vectors);

    // We simply iterate t from 0 to 1
    for (int i = 0; i <= CONTOUR_SAMPLES; i++)
    {
        const float t = static_cast<float>(i) / static_cast<float>(CONTOUR_SAMPLES);
        app->fc.calculateVectors(t);
        const auto& vectors = app->fc.getVectors();

        Vec2f accumulator = {0.f, 0.f};
        // Sum active vectors
        for (size_t v = 0; v < count && v < vectors.size(); ++v)
        {
            accumulator += vectors[v];
        }
        app->contour_cache.emplace_back(accumulator.x, accumulator.y);
    }

    app->dirty_contour = false;
}

void setActiveVectors(AppState* app, const size_t target)
{
    const size_t min_allowed = app->max_vectors > 0 ? MIN_ACTIVE_VECTOR_COUNT : 0;
    const size_t upper = app->max_vectors > 0 ? app->max_vectors : MIN_ACTIVE_VECTOR_COUNT;
    const size_t clamped = std::clamp(target, min_allowed, upper);
    if (clamped == app->active_vectors) return;
    app->active_vectors = clamped;
    app->dirty_contour = true;
}

// Draw a circle with LOD based on zoom level
void drawCircle(SDL_Renderer* renderer, const Vec2f center, const float radius_screen)
{
    if (radius_screen < MIN_DRAWABLE_RADIUS) return;

    const auto segments = std::clamp(
        static_cast<decltype(CIRCLE_SEGMENTS_MIN)>(radius_screen * CIRCLE_SEGMENTS_PER_PIXEL),
        CIRCLE_SEGMENTS_MIN, CIRCLE_SEGMENTS_MAX);

    static std::vector<Vec2f> points;
    if (points.size() < segments + 1) points.resize(CIRCLE_POINT_BUFFER_SIZE);

    for (int i = 0; i <= segments; ++i)
    {
        const float theta = 2.0f * PI_F * static_cast<float>(i) / static_cast<float>(segments);
        points[i] = {
            center.x + radius_screen * std::cos(theta),
            center.y + radius_screen * std::sin(theta)
        };
    }

    SDL_SetRenderDrawColor(renderer, COLOR_CIRCLE.r, COLOR_CIRCLE.g, COLOR_CIRCLE.b, COLOR_CIRCLE.a);
    SDL_RenderLines(renderer, as_sdl_fpoints(points.data()), segments + 1);
}


void drawUI(AppState* app)
{
    if (!app->show_ui && !app->show_sample_count_prompt) return;

    int w, h;
    SDL_GetWindowSize(app->window, &w, &h);

    if (app->show_sample_count_prompt)
    {
        SDL_SetRenderDrawColor(app->renderer, COLOR_DIALOG_OVERLAY.r, COLOR_DIALOG_OVERLAY.g,
                               COLOR_DIALOG_OVERLAY.b, COLOR_DIALOG_OVERLAY.a);
        SDL_FRect overlay = {0, 0, static_cast<float>(w), static_cast<float>(h)};
        SDL_RenderFillRect(app->renderer, &overlay);

        SDL_SetTextureColorMod(app->textRenderer.m_atlasTexture, COLOR_UI_TEXT.r, COLOR_UI_TEXT.g, COLOR_UI_TEXT.b);

        float dialog_x = static_cast<float>(w) / 2.0f - 200.0f;
        float dialog_y = static_cast<float>(h) / 2.0f - 60.0f;
        float dialog_spacing = app->currentFontSize * UI_LINE_SPACING_MULTIPLIER;

        auto printDialogLine = [&](const std::string& text)
        {
            app->textRenderer.renderText(dialog_x, dialog_y, text);
            dialog_y += dialog_spacing;
        };

        printDialogLine("SAMPLE COUNT");
        printDialogLine("");
        printDialogLine("Enter number of points (1-10000):");
        printDialogLine(std::format("> {}_", app->sample_count_text));
        printDialogLine("");
        printDialogLine("[Enter] Confirm  |  [Esc] Cancel");

        return; // Don't show regular UI when dialog is active
    }

    if (!app->show_ui) return;

    SDL_SetTextureColorMod(app->textRenderer.m_atlasTexture, COLOR_UI_TEXT.r, COLOR_UI_TEXT.g, COLOR_UI_TEXT.b);

    float y = UI_MARGIN_Y;
    float x = UI_MARGIN_X;
    float spacing = app->currentFontSize * UI_LINE_SPACING_MULTIPLIER;

    auto printLine = [&](const std::string& text)
    {
        app->textRenderer.renderText(x, y, text);
        y += spacing;
    };

    printLine("FOURIER CIRCLES by Kam1k4dze");
    printLine("");

    std::string status = app->paused ? "PAUSED" : "RUNNING";
    printLine(std::format("Status: {}", status));
    printLine(std::format("Active: {} / {} vectors", app->active_vectors, app->max_vectors));
    printLine(std::format("Samples: {}", app->original_points.size()));
    printLine(std::format("Zoom: {:.2f}x", app->cam.zoom));
    printLine(std::format("Speed: {:.1f}x", app->time_scale));
    printLine("");

    printLine("FILE:");
    printLine("  [L] Load SVG file");
    printLine("  [S] Change sample count");
    printLine("");

    printLine("ANIMATION:");
    std::string pause_action = app->paused ? "Resume" : "Pause";
    printLine(std::format("  [Space] {}", pause_action));
    printLine("  [Left/Right] Adjust speed");
    printLine("");

    printLine("VECTORS:");
    std::string vector_word = app->vector_step == 1 ? "vector" : "vectors";
    printLine(std::format("  [Up/Down] +/- {} {}", app->vector_step, vector_word));
    printLine("  [Ctrl+Up] Maximum");
    printLine("  [Ctrl+Down] Minimum");
    printLine(std::format("  [,/.] Adjust step: {}", app->vector_step));
    printLine("");

    printLine("CAMERA:");
    std::string follow_state = app->cam.follow_mode ? "Free camera" : "Follow tip";
    printLine(std::format("  [F] {} (toggle)", follow_state));
    printLine("  [Drag] Pan view");
    printLine("  [Wheel] Zoom");
    printLine("");

    printLine("DISPLAY:");
    std::string points_action = app->show_original_points ? "Hide" : "Show";
    printLine(std::format("  [P] {} sample points", points_action));
    printLine("  [H] Hide help");
}

void showSampleCountPrompt(AppState* app)
{
    app->show_sample_count_prompt = true;
    app->sample_count_text = std::to_string(app->svg_sample_count);
    SDL_StartTextInput(app->window);
}

void processSampleCountInput(AppState* app)
{
    try
    {
        const int sample_count = std::stoi(app->sample_count_text);
        if (sample_count < 1 || sample_count > 10000)
        {
            SDL_Log("Sample count must be between 1 and 10000");
            return;
        }

        loadSVG(app, app->current_svg_path, sample_count);
        SDL_Log("Loaded SVG with %d samples", sample_count);
    }
    catch (const std::exception& e)
    {
        SDL_Log("Invalid sample count: %s", e.what());
    }

    app->show_sample_count_prompt = false;
    app->sample_count_text = "";
    SDL_StopTextInput(app->window);
}

#ifdef __EMSCRIPTEN__
// Global pointer to store the app state for Emscripten file dialog callback
static AppState* g_emscripten_app = nullptr;

// This function is called from JavaScript when a file is selected
extern "C" {
EMSCRIPTEN_KEEPALIVE
void emscripten_file_selected(const char* filepath)
{
    if (!g_emscripten_app) return;

    if (filepath && filepath[0] != '\0')
    {
        g_emscripten_app->current_svg_path = filepath;

        showSampleCountPrompt(g_emscripten_app);

        SDL_Log("Selected file: %s", filepath);
    }
    else
    {
        // User canceled or error occurred
        SDL_Log("File selection canceled");
    }
}
}

void showFileDialog(AppState* app)
{
    g_emscripten_app = app;

    // Trigger the JavaScript file input dialog
    EM_ASM({document.getElementById('fileInput').click();});
}
#else
// Desktop file dialog implementation
void fileDialogCallback(void* userdata, const char* const* filelist, int filter)
{
    auto* app = static_cast<AppState*>(userdata);

    if (filelist == nullptr)
    {
        SDL_Log("File dialog error: %s", SDL_GetError());
        return;
    }

    if (filelist[0] == nullptr)
    {
        SDL_Log("No file selected");
        return;
    }

    app->current_svg_path = filelist[0];

    showSampleCountPrompt(app);

    SDL_Log("Selected file: %s", filelist[0]);
}

void showFileDialog(AppState* app)
{
    constexpr SDL_DialogFileFilter filter = {"SVG Files", "svg"};

    const char* default_location = nullptr;
    if (!app->current_svg_path.empty())
    {
        default_location = app->current_svg_path.c_str();
    }

    SDL_ShowOpenFileDialog(fileDialogCallback, app, app->window, &filter, 1, default_location, false);
}
#endif // __EMSCRIPTEN__

void updateDpiScale(AppState* app)
{
    float displayScale = SDL_GetWindowDisplayScale(app->window);

    if (displayScale != app->currentDpiScale)
    {
        app->currentDpiScale = displayScale;
        app->currentFontSize = BASE_UI_FONT_SIZE * displayScale;

        if (!app->textRenderer.rebuildAtlas(app->currentFontSize))
        {
            SDL_Log("Failed to rebuild font atlas for DPI scale %.2f", displayScale);
        }
        else
        {
            SDL_Log("Updated font size to %.1f for DPI scale %.2f", app->currentFontSize, displayScale);
        }
    }
}


SDL_AppResult SDL_AppInit(void** appstate, int argc, char** argv)
{
    auto* app = new AppState();
    *appstate = app;

    if (!SDL_Init(SDL_INIT_VIDEO)) return SDL_APP_FAILURE;

    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);

    // Use the driver geometry API (correct, draws thicker diagonal lines)
    SDL_SetHint(SDL_HINT_RENDER_LINE_METHOD, "3");

    if (!SDL_CreateWindowAndRenderer("Fourier Circles", INITIAL_WINDOW_W, INITIAL_WINDOW_H,
                                     SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY, &app->window,
                                     &app->renderer))
    {
        return SDL_APP_FAILURE;
    }

    SDL_SetRenderVSync(app->renderer, 1);

    SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);

    app->currentDpiScale = SDL_GetWindowDisplayScale(app->window);
    app->currentFontSize = BASE_UI_FONT_SIZE * app->currentDpiScale;

    SDL_Log("Initial DPI scale: %.2f, Font size: %.1f", app->currentDpiScale, app->currentFontSize);

    if (!app->textRenderer.init(app->renderer, "", app->currentFontSize))
    {
        SDL_Log("Failed to initialize text renderer");
        return SDL_APP_FAILURE;
    }
    // SDL_Log("Loading embedded default SVG");
    loadSVG(app, {}, SVG_SAMPLE_COUNT);

    app->cam.position = {INITIAL_WINDOW_W / 2.0f, INITIAL_WINDOW_H / 2.0f};
    app->last_tick = std::chrono::steady_clock::now();

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event)
{
    auto* app = static_cast<AppState*>(appstate);

    switch (event->type)
    {
    case SDL_EVENT_QUIT:
        return SDL_APP_SUCCESS;

    case SDL_EVENT_KEY_DOWN:
        {
            const bool ctrl = (event->key.mod & SDL_KMOD_CTRL) != 0;

            if (app->show_sample_count_prompt)
            {
                if (event->key.key == SDLK_RETURN || event->key.key == SDLK_KP_ENTER)
                {
                    processSampleCountInput(app);
                }
                else if (event->key.key == SDLK_ESCAPE)
                {
                    app->show_sample_count_prompt = false;
                    app->sample_count_text = "";
                    SDL_StopTextInput(app->window);
                }
                else if (event->key.key == SDLK_BACKSPACE && !app->sample_count_text.empty())
                {
                    app->sample_count_text.pop_back();
                }
                return SDL_APP_CONTINUE;
            }

            if (!IS_EMSCRIPTEN && event->key.key == SDLK_ESCAPE) return SDL_APP_SUCCESS;

            if (event->key.key == SDLK_L) showFileDialog(app);
            if (event->key.key == SDLK_S) showSampleCountPrompt(app);

            if (event->key.key == SDLK_SPACE) app->paused = !app->paused;
            if (event->key.key == SDLK_RIGHT)
            {
                app->time_scale = std::clamp(app->time_scale + TIME_SCALE_STEP, TIME_SCALE_MIN, TIME_SCALE_MAX);
            }
            if (event->key.key == SDLK_LEFT)
            {
                app->time_scale = std::clamp(app->time_scale - TIME_SCALE_STEP, TIME_SCALE_MIN, TIME_SCALE_MAX);
            }

            if (event->key.key == SDLK_F)
            {
                app->cam.follow_mode = !app->cam.follow_mode;
                if (!app->cam.follow_mode)
                {
                    // Recenter on tip instantly so we don't jump to 0,0
                    int w, h;
                    SDL_GetWindowSize(app->window, &w, &h);
                    const Vec2f window_size = {static_cast<float>(w), static_cast<float>(h)};
                    const Vec2f scr = app->cam.worldToScreen(app->current_tip);
                    app->cam.position += window_size / 2.0f - scr;
                }
            }

            if (event->key.key == SDLK_P) app->show_original_points = !app->show_original_points;
            if (event->key.key == SDLK_H) app->show_ui = !app->show_ui;

            if (event->key.key == SDLK_UP)
            {
                if (ctrl)
                {
                    setActiveVectors(app, app->max_vectors);
                }
                else
                {
                    setActiveVectors(app, app->active_vectors + app->vector_step);
                }
            }
            else if (event->key.key == SDLK_DOWN)
            {
                if (ctrl)
                {
                    setActiveVectors(app, MIN_ACTIVE_VECTOR_COUNT);
                }
                else
                {
                    const size_t target = app->active_vectors > app->vector_step
                                              ? app->active_vectors - app->vector_step
                                              : MIN_ACTIVE_VECTOR_COUNT;
                    setActiveVectors(app, target);
                }
            }
            else if (event->key.key == SDLK_COMMA)
            {
                if (app->vector_step > MIN_VECTOR_STEP)
                {
                    app->vector_step--;
                }
            }
            else if (event->key.key == SDLK_PERIOD)
            {
                app->vector_step = std::min(app->vector_step + 1, app->max_vectors);
            }
        }
        break;

    case SDL_EVENT_MOUSE_WHEEL:
        {
            float mouseX, mouseY;
            SDL_GetMouseState(&mouseX, &mouseY);
            const Vec2f worldBefore = app->cam.screenToWorld(mouseX, mouseY);

            if (event->wheel.y > 0) app->cam.zoom *= ZOOM_STEP;
            if (event->wheel.y < 0) app->cam.zoom /= ZOOM_STEP;
            app->cam.zoom = std::clamp(app->cam.zoom, ZOOM_MIN, ZOOM_MAX);

            const Vec2f worldAfter = app->cam.screenToWorld(mouseX, mouseY);
            app->cam.position += (worldAfter - worldBefore) * app->cam.zoom;
            break;
        }

    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (event->button.button == SDL_BUTTON_LEFT)
        {
            app->is_dragging = true;
            app->drag_start = {event->button.x, event->button.y};
            app->cam_start = app->cam.position;
            app->cam.follow_mode = false;
        }
        break;

    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (event->button.button == SDL_BUTTON_LEFT) app->is_dragging = false;
        break;

    case SDL_EVENT_MOUSE_MOTION:
        if (app->is_dragging)
        {
            app->cam.position = app->cam_start +
                Vec2f(event->motion.x, event->motion.y) - app->drag_start;
        }
        break;

    case SDL_EVENT_TEXT_INPUT:
        if (app->show_sample_count_prompt)
        {
            // Only accept digits
            for (const char* c = event->text.text; *c != '\0'; ++c)
            {
                if (*c >= '0' && *c <= '9')
                {
                    app->sample_count_text += *c;
                }
            }
        }
        break;

    case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
        updateDpiScale(app);
        break;

    default:
        break;
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate)
{
    auto* app = static_cast<AppState*>(appstate);

    const auto now = std::chrono::steady_clock::now();
    const float dt = std::chrono::duration<float>(now - app->last_tick).count();
    app->last_tick = now;

    if (!app->paused)
    {
        app->accumulated_time += dt * app->time_scale;
    }

    // Wrap time to 0..1 for calculation
    const float periodT = std::fmod(app->accumulated_time, SIMULATION_PERIOD) / SIMULATION_PERIOD;

    if (app->dirty_contour) regenerateContour(app);

    app->fc.calculateVectors(periodT);
    const auto& vectors = app->fc.getVectors();


    // Calculate Tip Position in World Space
    Vec2f tip = {0, 0};
    const size_t limit = std::min(app->active_vectors, vectors.size());
    for (size_t i = 0; i < limit; ++i) tip += vectors[i];
    app->current_tip = tip;

    if (app->cam.follow_mode)
    {
        int w, h;
        SDL_GetWindowSize(app->window, &w, &h);
        const Vec2f window_size = {static_cast<float>(w), static_cast<float>(h)};
        app->cam.position = window_size / 2.0f - tip * app->cam.zoom;
    }


    SDL_SetRenderDrawColor(app->renderer, COLOR_BG.r, COLOR_BG.g, COLOR_BG.b, COLOR_BG.a);
    SDL_RenderClear(app->renderer);

    // Draw Contour
    SDL_SetRenderDrawColor(app->renderer, COLOR_CONTOUR.r, COLOR_CONTOUR.g, COLOR_CONTOUR.b, COLOR_CONTOUR.a);
    if (!app->contour_cache.empty())
    {
        static std::vector<Vec2f> screenContour;
        screenContour.resize(app->contour_cache.size());

        for (size_t i = 0; i < app->contour_cache.size(); ++i)
        {
            screenContour[i] = app->cam.worldToScreen(
                app->contour_cache[i].x, app->contour_cache[i].y);
        }

        SDL_RenderLines(app->renderer, as_sdl_fpoints(screenContour.data()), static_cast<int>(screenContour.size()));
    }

    // Draw Sample Points
    if (app->show_original_points)
    {
        SDL_SetRenderDrawColor(app->renderer, COLOR_SAMPLE_POINTS.r, COLOR_SAMPLE_POINTS.g,
                               COLOR_SAMPLE_POINTS.b, COLOR_SAMPLE_POINTS.a);
        for (const auto& p : app->original_points)
        {
            const Vec2f scr = app->cam.worldToScreen(p);
            SDL_FRect r = {
                scr.x - ORIGINAL_POINT_MARKER_HALF_SIZE, scr.y - ORIGINAL_POINT_MARKER_HALF_SIZE,
                ORIGINAL_POINT_MARKER_SIZE, ORIGINAL_POINT_MARKER_SIZE
            };
            SDL_RenderFillRect(app->renderer, &r);
        }
    }
    // Draw Epicycles
    Vec2f prev = {0, 0};
    for (size_t i = 0; i < limit; ++i)
    {
        const Vec2f center = app->cam.worldToScreen(prev);
        const float radius = vectors[i].length() * app->cam.zoom;

        drawCircle(app->renderer, center, radius);

        prev += vectors[i];

        const Vec2f end = app->cam.worldToScreen(prev);

        // Draw arm
        const auto& [r, g, b, a] = ARM_COLORS[i % 5];
        SDL_SetRenderDrawColor(app->renderer, r, g, b, a);
        SDL_RenderLine(app->renderer, center.x, center.y, end.x, end.y);
    }


    // Highlight Tip
    SDL_SetRenderDrawColor(app->renderer, COLOR_TIP.r, COLOR_TIP.g, COLOR_TIP.b, COLOR_TIP.a);
    const Vec2f tipScr = app->cam.worldToScreen(tip);
    const SDL_FRect tipRect = {
        tipScr.x - TIP_MARKER_HALF_SIZE, tipScr.y - TIP_MARKER_HALF_SIZE,
        TIP_MARKER_SIZE, TIP_MARKER_SIZE
    };
    SDL_RenderFillRect(app->renderer, &tipRect);

    drawUI(app);

    SDL_RenderPresent(app->renderer);
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult result)
{
    if (const auto* app = static_cast<AppState*>(appstate))
    {
        SDL_DestroyRenderer(app->renderer);
        SDL_DestroyWindow(app->window);
        delete app;
    }
}
