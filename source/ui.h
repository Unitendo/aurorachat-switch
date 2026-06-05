#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <string>
#include <unordered_map>
#include <functional>
#include "constants.h"

namespace UI {

// Color Palette
namespace Color {
    constexpr SDL_Color Cyan        = {0, 255, 228, 255};
    constexpr SDL_Color White       = {255, 255, 255, 255};
    constexpr SDL_Color Gray        = {120, 120, 130, 255};
    constexpr SDL_Color DimCyan     = {0, 140, 130, 255};
    constexpr SDL_Color Green       = {50, 255, 150, 255};
    constexpr SDL_Color Red         = {255, 100, 100, 255};
    constexpr SDL_Color DeepRed     = {255, 60, 60, 255};
    constexpr SDL_Color Yellow      = {255, 220, 60, 255};
    constexpr SDL_Color Orange      = {255, 160, 80, 255};
    constexpr SDL_Color Blue        = {80, 200, 255, 255};
    constexpr SDL_Color Purple      = {200, 140, 255, 255};
    constexpr SDL_Color Lavender    = {180, 180, 255, 255};
    constexpr SDL_Color BgDark      = {8, 8, 12, 235};
    constexpr SDL_Color PanelBg     = {10, 12, 24, 240};
    constexpr SDL_Color HintGray    = {80, 80, 90, 255};
    constexpr SDL_Color Transparent = {0, 0, 0, 0};
}

// Windows 98 Palette
namespace W98 {
    constexpr SDL_Color Silver      = {192, 192, 192, 255};
    constexpr SDL_Color Light       = {223, 223, 223, 255};
    constexpr SDL_Color White       = {255, 255, 255, 255};
    constexpr SDL_Color Shadow      = {128, 128, 128, 255};
    constexpr SDL_Color DarkShadow  = {  0,   0,   0, 255};
    constexpr SDL_Color Navy        = {  0,   0, 128, 255};
    constexpr SDL_Color Desktop     = {  0, 128, 128, 255};
    constexpr SDL_Color Black       = {  0,   0,   0, 255};
    constexpr SDL_Color FieldBg     = {255, 255, 255, 255};
    constexpr int       TitleH      = 22;
}

// Text Cache
class TextCache {
public:
    struct Entry {
        SDL_Texture* texture = nullptr;
        int width  = 0;
        int height = 0;
        uint32_t frameUsed = 0;
    };

    void init(SDL_Renderer* r) { renderer_ = r; }

    const Entry& get(const char* text, int size);

    void beginFrame()                  { frame_++; }
    void evict(uint32_t maxAge = 600);
    void clear();

private:
    struct Key {
        std::string text;
        int size;
        bool operator==(const Key& o) const { return text == o.text && size == o.size; }
    };
    struct KeyHash {
        size_t operator()(const Key& k) const {
            size_t h = std::hash<std::string>()(k.text);
            h ^= std::hash<int>()(k.size) << 16;
            return h;
        }
    };

    SDL_Renderer* renderer_ = nullptr;
    uint32_t frame_ = 0;
    std::unordered_map<Key, Entry, KeyHash> cache_;
    static constexpr size_t MAX_ENTRIES = 512;
};

// Input Glyphs
enum class Action { Confirm, Back, Left, Right, Navigate, Pause, Tab, Bomb };

const char* glyphLabel(Action action, bool gamepad);

struct HintPair { Action action; const char* desc; };
std::string buildHintBar(const HintPair* pairs, int count, bool gamepad);

// Context
struct Context {
    SDL_Renderer* renderer  = nullptr;
    SDL_Texture*  desktopBg = nullptr;
    TextCache     textCache;

    int  mouseX  = 0;
    int  mouseY  = 0;
    bool mouseClicked  = false;
    bool mouseReleased = false;
    bool mouseDown     = false;
    bool touchActive   = false;
    int  mouseWheelY   = 0;

    bool usingGamepad = false;
    float dt = 0;

    static constexpr int MAX_ANIM_ITEMS = 64;
    float itemAnim[MAX_ANIM_ITEMS] = {};

    int  hoveredItem = -1;
    int  prevHoveredItem = -1;
    int  clickedItem = -1;

    bool buttonFired = false;
    int clickCooldownFrames = 0;

    void init(SDL_Renderer* r);
    void beginFrame(float dt, bool gamepad);
    void endFrame();
    void shutdown();

    void drawText(const char* text, int x, int y, int size, SDL_Color color);
    void drawTextCentered(const char* text, int y, int size, SDL_Color color);
    void drawTextRight(const char* text, int x, int y, int size, SDL_Color color);
    int  textWidth(const char* text, int size);
    int  textHeight(int size);
    int  drawTextWrapped(const char* text, int x, int y, int size,
                         int maxW, SDL_Color color, bool doDraw = true);

    void drawPanel(int x, int y, int w, int h,
                   SDL_Color bg = Color::PanelBg,
                   SDL_Color border = {0, 180, 160, 80});
    void drawDarkOverlay(uint8_t alpha = 200,
                         uint8_t r = 4, uint8_t g = 6, uint8_t b = 14);
    void drawSeparator(int cx, int y, int halfWidth,
                       SDL_Color color = {0, 180, 160, 60});

    bool menuItem(int idx, const char* label, int cx, int y, int w, int h,
                  SDL_Color accent, bool sel, int fontSize = 20, int selFontSize = 24);
    int  sliderRow(int idx, const char* label, const char* value,
                   int cx, int y, int w, int h,
                   SDL_Color accent, bool sel, bool leftKey, bool rightKey);
    void drawHintBar(const HintPair* pairs, int count, int y = SCREEN_H - 36);

    void drawDesktop();
    void drawWin98Bevel(int x, int y, int w, int h, bool raised);
    void drawWin98Window(int x, int y, int w, int h, const char* title, bool active = true);
    bool win98Button(int idx, const char* label, int x, int y, int w, int h, bool sel);
    void drawWin98TextField(int x, int y, int w, int h, const char* text,
                            bool focused, bool password = false, float blinkT = 0.0f);
    void drawWin98StatusBar(int y, const char* text = nullptr);

    bool pointInRect(int px, int py, int rx, int ry, int rw, int rh) const;
};

} // namespace UI
