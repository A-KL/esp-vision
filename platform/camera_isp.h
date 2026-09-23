/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define ESP_VISION_ISP_MATRIX_SIZE 9
#define ESP_VISION_ISP_GAMMA_POINTS 16
// Upper bound for one LSC channel's gain table. The ISP needs one grid point
// every 64 pixels plus a boundary point per axis, so the largest capture the
// ISP supports, 1920x1080, needs (1919 / 64 + 2) * (1079 / 64 + 2) = 31 * 18 =
// 558 entries. Size tables from the lsc_gain_size a read reports rather than
// from this bound; it only exists to reject implausible device answers.
#define ESP_VISION_ISP_LSC_MAX_POINTS 560

typedef enum {
    ESP_VISION_ISP_EXPOSURE,
    ESP_VISION_ISP_PIXEL_GAIN,
    ESP_VISION_ISP_DEMOSAIC,
    ESP_VISION_ISP_WB,
    ESP_VISION_ISP_LSC,
    ESP_VISION_ISP_BLC,
    ESP_VISION_ISP_BF,
    ESP_VISION_ISP_SHARPEN,
    ESP_VISION_ISP_CCM,
    ESP_VISION_ISP_GAMMA,
    ESP_VISION_ISP_BLOCK_COUNT,
} esp_vision_isp_block_t;

typedef struct {
    bool enable;
    bool stretch_enable;
    bool params_changed;
    bool gamma_curve_changed;
    int32_t exposure_us;
    float gain;
    float gradient_ratio;
    float rg;
    float bg;
    uint8_t level;
    uint8_t h_thresh;
    uint8_t l_thresh;
    float h_coeff;
    float m_coeff;
    uint16_t offsets[4];
    float matrix[ESP_VISION_ISP_MATRIX_SIZE];
    uint8_t x[ESP_VISION_ISP_GAMMA_POINTS];
    uint8_t y[ESP_VISION_ISP_GAMMA_POINTS];
    size_t lsc_gain_size;
    // Caller-owned arrays, each holding at least lsc_gain_size elements. A read
    // that leaves them NULL only reports lsc_gain_size, so callers can size
    // their tables before reading the gains into them.
    float *lsc_gain[4];
} esp_vision_isp_config_t;

esp_err_t esp_vision_camera_set_ipa(bool enable);
esp_err_t esp_vision_camera_get_ipa(bool *enabled);
esp_err_t esp_vision_camera_get_isp(esp_vision_isp_block_t block, esp_vision_isp_config_t *config);
esp_err_t esp_vision_camera_set_isp(esp_vision_isp_block_t block, const esp_vision_isp_config_t *config);

esp_err_t esp_vision_isp_validate(esp_vision_isp_block_t block, const esp_vision_isp_config_t *config);

// Board hooks for stopping and restarting capture around an IPA controller change.
// The board keeps the camera context; these only cover the steps that touch it.
typedef struct {
    void (*release_buffers)(void);
    esp_err_t (*open_device)(void);
    esp_err_t (*set_input_format)(void);
    void (*update_active_window)(void);
    esp_err_t (*init_buffers)(void);
    esp_err_t (*start_stream)(void);
    void (*cleanup)(void);
} esp_vision_camera_restart_ops_t;

// True while the ESP Video ISP pipeline controller is running.
bool esp_vision_video_isp_controller_active(void);

// Resolve the IPA configuration for a MIPI-CSI camera. *ipa_config is NULL when
// this sensor has no runtime controller. Only meaningful when the pipeline
// controller is enabled.
esp_err_t esp_vision_video_isp_lookup_ipa(const char *dev_path, const char *meta_path, const void **ipa_config);

// Read whether that controller is running. ready is the camera streaming state.
esp_err_t esp_vision_video_isp_get_controller(bool ready, const void *ipa_config, bool *enabled);

// Stop capture, switch the controller, and start capture again. On failure the
// ops cleanup hook shuts the camera down. *fd is cleared only after close succeeds.
esp_err_t esp_vision_video_isp_set_controller(bool enable, int *fd, bool *streaming, bool *initialized,
                                              const char *isp_path, const char *cam_path, const void *ipa_config,
                                              const esp_vision_camera_restart_ops_t *ops);

// ESP Video backend helpers. The board retains ownership of the capture handle.
esp_err_t esp_vision_video_isp_get(int camera_fd, const char *isp_path, esp_vision_isp_block_t block,
                                   esp_vision_isp_config_t *config);
esp_err_t esp_vision_video_isp_set(int camera_fd, const char *isp_path, esp_vision_isp_block_t block,
                                   const esp_vision_isp_config_t *config);
esp_err_t esp_vision_video_isp_save_lsc(const char *isp_path);
esp_err_t esp_vision_video_isp_restore_lsc(const char *isp_path);
// Drop the per-session ISP state. Pass free_lsc_tables = false when the ISP
// device may still reference the LSC tables; they are then kept, and the
// installed one stays untouched, instead of being freed under the hardware.
void esp_vision_video_isp_release(bool free_lsc_tables);
