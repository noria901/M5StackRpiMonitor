
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



idf_component_register(SRCS "main.cpp" ${HAL_SRCS} ${APP_SRCS}
                    INCLUDE_DIRS "." ${HAL_INCS} ${APP_INCS}
                    REQUIRES nvs_flash json LovyanGFX lvgl esp-idf-rc522 fatfs
                    PRIV_REQUIRES bt esp_adc driver)

# Workaround: manually add NimBLE include paths
# bt component's INCLUDE_DIRS are not being propagated through REQUIRES/PRIV_REQUIRES
if(CONFIG_BT_NIMBLE_ENABLED)
    idf_build_get_property(idf_path IDF_PATH)
    set(BT_DIR "${idf_path}/components/bt")
    target_include_directories(${COMPONENT_LIB} PRIVATE
        "${BT_DIR}/host/nimble/nimble/nimble/host/include"
        "${BT_DIR}/host/nimble/nimble/nimble/include"
        "${BT_DIR}/host/nimble/nimble/nimble/host/services/gap/include"
        "${BT_DIR}/host/nimble/port/include"
        "${BT_DIR}/host/nimble/nimble/porting/nimble/include"
        "${BT_DIR}/host/nimble/nimble/porting/npl/freertos/include"
        "${BT_DIR}/host/nimble/nimble/nimble/transport/include"
        "${BT_DIR}/porting/include"
    )
endif()
