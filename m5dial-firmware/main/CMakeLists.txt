
# Hal
file(GLOB_RECURSE HAL_SRCS
    hal/*.c
    hal/*.cpp
)
set(HAL_INCS
    hal/
)

# App
file(GLOB_RECURSE APP_SRCS
    apps/*.c
    apps/*.cpp
)
set(APP_INCS
    apps/
)

# NimBLE include paths (bt component INCLUDE_DIRS not propagated via REQUIRES)
set(NIMBLE_INCS
    $ENV{IDF_PATH}/components/bt/host/nimble/nimble/nimble/host/include
    $ENV{IDF_PATH}/components/bt/host/nimble/nimble/nimble/include
    $ENV{IDF_PATH}/components/bt/host/nimble/nimble/nimble/host/services/gap/include
    $ENV{IDF_PATH}/components/bt/host/nimble/port/include
    $ENV{IDF_PATH}/components/bt/host/nimble/nimble/porting/nimble/include
    $ENV{IDF_PATH}/components/bt/host/nimble/nimble/porting/npl/freertos/include
    $ENV{IDF_PATH}/components/bt/host/nimble/nimble/nimble/transport/include
    $ENV{IDF_PATH}/components/bt/porting/include
)

idf_component_register(SRCS "main.cpp" ${HAL_SRCS} ${APP_SRCS}
                    INCLUDE_DIRS "." ${HAL_INCS} ${APP_INCS} ${NIMBLE_INCS}
                    REQUIRES nvs_flash json LovyanGFX lvgl esp-idf-rc522 fatfs
                    PRIV_REQUIRES bt esp_adc driver)
