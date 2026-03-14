
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
file(TO_CMAKE_PATH "$ENV{IDF_PATH}" IDF_PATH_CMAKE)
set(NIMBLE_INCS
    ${IDF_PATH_CMAKE}/components/bt/host/nimble/nimble/nimble/host/include
    ${IDF_PATH_CMAKE}/components/bt/host/nimble/nimble/nimble/include
    ${IDF_PATH_CMAKE}/components/bt/host/nimble/nimble/nimble/host/services/gap/include
    ${IDF_PATH_CMAKE}/components/bt/host/nimble/nimble/nimble/host/util/include
    ${IDF_PATH_CMAKE}/components/bt/host/nimble/port/include
    ${IDF_PATH_CMAKE}/components/bt/host/nimble/nimble/porting/nimble/include
    ${IDF_PATH_CMAKE}/components/bt/host/nimble/nimble/porting/npl/freertos/include
    ${IDF_PATH_CMAKE}/components/bt/host/nimble/nimble/nimble/transport/include
    ${IDF_PATH_CMAKE}/components/bt/porting/include
)

idf_component_register(SRCS "main.cpp" ${HAL_SRCS} ${APP_SRCS}
                    INCLUDE_DIRS "." ${HAL_INCS} ${APP_INCS} ${NIMBLE_INCS}
                    REQUIRES nvs_flash json LovyanGFX lvgl esp-idf-rc522 fatfs
                    PRIV_REQUIRES bt esp_adc driver)
