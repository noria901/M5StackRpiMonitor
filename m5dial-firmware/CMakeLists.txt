# M5Dial RPi Monitor - Top-level CMakeLists.txt
# Based on M5Dial-UserDemo project structure
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
    "components/LovyanGFX"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(m5dial-rpi-monitor)
