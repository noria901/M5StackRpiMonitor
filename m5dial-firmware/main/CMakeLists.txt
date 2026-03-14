# Collect HAL sources
file(GLOB_RECURSE HAL_SRCS hal/*.c hal/*.cpp)

# Collect App sources
file(GLOB_RECURSE APP_SRCS apps/*.c apps/*.cpp)

# Collect HAL include directories
set(HAL_INCS
    "hal"
)

# Collect App include directories
set(APP_INCS
    "apps"
    "apps/app_rpi_monitor"
    "apps/app_ble_scanner"
    "apps/app_ble_scanner/gui"
    "apps/utils"
    "apps/utils/gui_base"
    "apps/launcher"
)

idf_component_register(
    SRCS "main.cpp" ${HAL_SRCS} ${APP_SRCS}
    INCLUDE_DIRS "." ${HAL_INCS} ${APP_INCS}
    REQUIRES
        bt
        nvs_flash
        json
)
