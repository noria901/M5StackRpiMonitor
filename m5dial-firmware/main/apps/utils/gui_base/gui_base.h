#pragma once

/**
 * GUI_Base stub matching the real M5Dial-UserDemo API.
 * Replace with UserDemo's gui_base.h when integrating.
 */

#include <cstdint>
#include <string>
#include <LovyanGFX.hpp>

// Font macros used by UserDemo
#define GUI_FONT_CN_BIG  &fonts::lgfxJapanGothicP_24
#define GUI_FONT_CN_SMALL &fonts::lgfxJapanGothicP_16

struct BasicObeject_t {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

class GUI_Base {
protected:
    LGFX_Sprite* _canvas = nullptr;
    LGFX_Sprite* _icon = nullptr;
    uint32_t _theme_color = 0xFFFFFF;
    int _canvas_half_width = 0;
    int _canvas_half_height = 0;

public:
    GUI_Base() = default;
    virtual ~GUI_Base() = default;

    inline void setCanvas(LGFX_Sprite* canvas, LGFX_Sprite* icon) {
        _canvas = canvas;
        _icon = icon;
        _canvas_half_width = _canvas->width() / 2;
        _canvas_half_height = _canvas->height() / 2;
    }
    inline void setThemeColor(const uint32_t& color) { _theme_color = color; }

    virtual void init();
    void init(LGFX_Sprite* canvas, LGFX_Sprite* icon) {
        setCanvas(canvas, icon);
        init();
    }

protected:
    void _draw_quit_button(const int& buttonColor = TFT_WHITE);
    void _draw_banner(const std::string& label, int x, int y, const int& bannerColor = TFT_WHITE);
    void _draw_top_banner(const std::string& label, const int& bannerColor = TFT_WHITE);
    void _draw_top_icon();
};
