#pragma once

/**
 * APP_BASE stub header for standalone compilation reference.
 *
 * In the actual build, this file is provided by M5Dial-UserDemo's
 * MOONCAKE framework. This stub defines the interface that
 * app_rpi_monitor depends on, so the code structure can be reviewed
 * without the full UserDemo source tree.
 *
 * When integrating into the real UserDemo project, remove this file
 * and use the one from M5Dial-UserDemo/main/apps/app.h instead.
 */

#include <cstdint>
#include <cstddef>

// Forward-declare LovyanGFX types used by the HAL
namespace lgfx {
    class LGFX_Sprite;
    struct IFont;
}

namespace fonts {
    extern const lgfx::IFont Font0;
    extern const lgfx::IFont Font2;
    extern const lgfx::IFont Font4;
}

namespace HAL {

struct TouchPoint {
    int x = 0;
    int y = 0;
};

struct TouchPanel {
    bool isTouched();
    void update();
    TouchPoint getTouchPointBuffer();
};

struct Button {
    int read();       // 0 = pressed, 1 = released
    bool pressed();   // edge detection
};

struct Encoder {
    bool wasMoved(bool clearFlag);
    int getDirection();  // < 1 = CW, >= 1 = CCW
    Button btn;
};

struct Buzzer {
    void tone(int freq, int durationMs);
};

struct HAL {
    lgfx::LGFX_Sprite* canvas = nullptr;
    Encoder encoder;
    TouchPanel tp;

    void buzz(int freq, int durationMs);

    void init();
};

}  // namespace HAL

namespace MOONCAKE {
namespace USER_APP {

class APP_BASE {
public:
    virtual ~APP_BASE() = default;

    virtual void onSetup() {}
    virtual void onCreate() {}
    virtual void onResume() {}
    virtual void onRunning() {}
    virtual void onRunningBG() {}
    virtual void onPause() {}
    virtual void onDestroy() {}

    void setAppName(const char* name) { _name = name; }
    void setAllowBgRunning(bool allow) { _allowBg = allow; }
    void setAppIcon(const void* icon) { _icon = icon; }
    void closeApp() { _goClose = true; }
    void destroyApp() { _goDestroy = true; }

    const char* getAppName() const { return _name; }
    bool shouldClose() const { return _goClose; }
    bool shouldDestroy() const { return _goDestroy; }

    void setAppData(void* data) { _appData = data; }
    void* getAppData() { return _appData; }

private:
    const char* _name = "";
    bool _allowBg = false;
    const void* _icon = nullptr;
    bool _goClose = false;
    bool _goDestroy = false;
    void* _appData = nullptr;
};

}  // namespace USER_APP
}  // namespace MOONCAKE
