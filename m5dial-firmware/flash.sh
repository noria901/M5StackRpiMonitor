#!/bin/bash

# Configs
IDF_VERSION=v5.1.3
IDF_PATH=$HOME/esp/esp-idf-${IDF_VERSION}/
SERIAL_PORT=/dev/ttyACM0

### Usage: flash.sh [options]
###
### Options:
###   -p, --port PORT   Serial port (default: /dev/ttyACM0)
###   -b, --build-only  Build only, do not flash
###   -h, --help        Show this help

help() {
    sed -rn 's/^### ?//;T;p' "$0"
}

if [[ "$1" == "-h" ]] || [[ "$1" == "--help" ]]; then
    help
    exit 0
fi

# Parse args
BUILD_ONLY=false
while [[ $# -gt 0 ]]; do
    case "$1" in
        -p|--port)
            SERIAL_PORT="$2"
            shift 2
            ;;
        -b|--build-only)
            BUILD_ONLY=true
            shift
            ;;
        *)
            shift
            ;;
    esac
done

# Install ESP-IDF if not present
install_idf() {
    echo "==> ESP-IDF ${IDF_VERSION} not found at ${IDF_PATH}"
    echo "==> Installing ESP-IDF ${IDF_VERSION}..."

    mkdir -p "$HOME/esp"
    git clone -b "${IDF_VERSION}" --recursive \
        https://github.com/espressif/esp-idf.git "${IDF_PATH}"

    cd "${IDF_PATH}"
    ./install.sh
    cd -
    echo "==> ESP-IDF ${IDF_VERSION} installed successfully"
}

if [ ! -f "${IDF_PATH}/export.sh" ]; then
    install_idf
fi

# Source ESP-IDF environment
. "${IDF_PATH}/export.sh"

# Build and flash
if [ "$BUILD_ONLY" = true ]; then
    idf.py build
else
    idf.py -p "${SERIAL_PORT}" flash -b 1500000 monitor
fi
