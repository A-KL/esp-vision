# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0

import random
import sensor
import time


UPDATE_INTERVAL_MS = 2000
EXPOSURE_US_RANGE = (1000, 20000)
GAIN_RANGE = (1.0, 8.0)
WBG_RANGE = (0.8, 2.5)
CCM_DIAGONAL_RANGE = (0.7, 1.4)
CCM_OFF_DIAGONAL_RANGE = (-0.2, 0.2)
ISP_READBACK_BLOCKS = ("pixel_gain", "wb", "ccm")


def preview(duration_ms):
    start = time.ticks_ms()
    while time.ticks_diff(time.ticks_ms(), start) < duration_ms:
        sensor.snapshot().flush()


def try_set_isp(block, **params):
    # The usable exposure and gain ranges depend on the sensor and its mode;
    # report a rejected value and keep previewing instead of stopping.
    try:
        sensor.set_isp(block, **params)
    except ValueError as e:
        print("skipped", block, params, "-", e)


def randomize_isp():
    # Exposure is quantized to the nearest 100 microseconds by the driver.
    exposure_us = random.randint(*EXPOSURE_US_RANGE)
    try_set_isp("exposure", value=exposure_us)
    try_set_isp("pixel_gain", value=random.uniform(*GAIN_RANGE))
    try_set_isp("wb", enable=True, rg=random.uniform(*WBG_RANGE), bg=random.uniform(*WBG_RANGE))
    matrix = [
        random.uniform(*(CCM_DIAGONAL_RANGE if row == col else CCM_OFF_DIAGONAL_RANGE))
        for row in range(3)
        for col in range(3)
    ]
    try_set_isp("ccm", enable=True, matrix=matrix)
    print("exposure requested (us):", exposure_us)
    for name in ISP_READBACK_BLOCKS:
        print(name, sensor.get_isp(name))


if not hasattr(sensor, "get_ipa") or not hasattr(sensor, "set_isp"):
    raise SystemExit("IPA/ISP control is not supported.")

sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)

if sensor.get_ipa() is None:
    raise SystemExit("Runtime IPA control is not supported.")

sensor.set_ipa(False)

for name in ISP_READBACK_BLOCKS:
    if sensor.get_isp(name) is None:
        raise SystemExit("ISP control is not supported: " + name)

print("IPA:", sensor.get_ipa())
print("Random ISP preview; update every", UPDATE_INTERVAL_MS, "ms")
print("Ctrl-C stops the preview and keeps the current settings.")

try:
    while True:
        randomize_isp()
        preview(UPDATE_INTERVAL_MS)
except KeyboardInterrupt:
    print("ISP preview stopped.")
