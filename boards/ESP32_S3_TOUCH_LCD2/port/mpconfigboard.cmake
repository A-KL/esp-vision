set(IDF_TARGET esp32s3)

set(SDKCONFIG_DEFAULTS
    boards/sdkconfig.base
    boards/ESP32_S3_TOUCH_LCD2/sdkconfig.s3_touch_lcd2
    boards/ESP32_S3_TOUCH_LCD2/sdkconfig.defaults.board
    boards/ESP32_S3_TOUCH_LCD2/sdkconfig.board
)

# Keep the first bring-up independent of optional MicroPython submodules.
set(MICROPY_PY_BTREE OFF)
