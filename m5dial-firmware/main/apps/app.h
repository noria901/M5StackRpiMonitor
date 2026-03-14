#pragma once

/**
 * APP_BASE stub header matching the real M5Dial-UserDemo API.
 *
 * In the actual build, this is provided by M5Dial-UserDemo's
 * MOONCAKE framework. This stub defines the interface for
 * standalone code review. Remove this file and use the real
 * UserDemo app.h when integrating.
 */

#include <string>
#include <cstdint>
#include <cstdio>
#include "utils/gui_base/gui_base.h"

// Logging macro used by UserDemo apps
#define _log(fmt, ...) printf("[%s] " fmt "\n", _tag, ##__VA_ARGS__)
#define _log_mem() do {} while(0)

// millis() compatibility
#include <esp_timer.h>
static inline unsigned long millis() {
    return (unsigned long)(esp_timer_get_time() / 1000);
}

namespace MOONCAKE {

class APP_BASE {
private:
    std::string _name;
    void* _user_data = nullptr;
    void* _icon_addr = nullptr;
    bool _allow_bg_running = false;
    bool _go_close = false;
    bool _go_destroy = false;

protected:
    inline void setAppName(const std::string& name) { _name = name; }
    inline void setAppIcon(void* icon) { _icon_addr = icon; }
    inline void setAllowBgRunning(bool allow) { _allow_bg_running = allow; }
    inline void closeApp() { _go_close = true; }
    inline void destroyApp() { _go_destroy = true; }
    inline void* getUserData() { return _user_data; }

public:
    APP_BASE() = default;
    virtual ~APP_BASE() = default;

    inline void setUserData(void* userData) { _user_data = userData; }
    virtual GUI_Base* getGui() { return nullptr; }

    inline std::string getAppName() { return _name; }
    inline void* getAppIcon() { return _icon_addr; }
    inline bool isAllowBgRunning() { return _allow_bg_running; }
    inline bool isGoingClose() { return _go_close; }
    inline bool isGoingDestroy() { return _go_destroy; }
    inline void resetGoingCloseFlag() { _go_close = false; }
    inline void resetGoingDestroyFlag() { _go_destroy = false; }

    virtual void onSetup() {}
    virtual void onCreate() {}
    virtual void onResume() {}
    virtual void onRunning() {}
    virtual void onRunningBG() {}
    virtual void onPause() {}
    virtual void onDestroy() {}
};

namespace USER_APP {} // namespace placeholder

} // namespace MOONCAKE
