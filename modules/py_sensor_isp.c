/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <math.h>
#include <stddef.h>

#include "py/objlist.h"
#include "py/runtime.h"
#include "camera.h"
#include "camera_isp.h"
#include "py_sensor_isp.h"

typedef enum {
    ISP_BOOL, ISP_U8, ISP_U16, ISP_I32, ISP_FLOAT, ISP_FLOAT_ARRAY, ISP_U8_ARRAY, ISP_LSC_ARRAY, ISP_SIZE,
} sensor_isp_field_type_t;

typedef struct {
    qstr name;
    size_t offset;
    sensor_isp_field_type_t type;
    size_t count;
} sensor_isp_field_t;

#define FIELD(name, member, type, count) {MP_QSTR_##name, offsetof(esp_vision_isp_config_t, member), type, count}
#define ENABLE FIELD(enable, enable, ISP_BOOL, 1)
static const sensor_isp_field_t s_exposure[] = {FIELD(value, exposure_us, ISP_I32, 1)};
static const sensor_isp_field_t s_pixel_gain[] = {FIELD(value, gain, ISP_FLOAT, 1)};
static const sensor_isp_field_t s_demosaic[] = {ENABLE, FIELD(gradient_ratio, gradient_ratio, ISP_FLOAT, 1)};
static const sensor_isp_field_t s_wb[] = {ENABLE, FIELD(rg, rg, ISP_FLOAT, 1), FIELD(bg, bg, ISP_FLOAT, 1)};
static const sensor_isp_field_t s_lsc[] = {
    ENABLE, FIELD(lsc_gain_size, lsc_gain_size, ISP_SIZE, 1),
    FIELD(gain_r, lsc_gain[0], ISP_LSC_ARRAY, 0), FIELD(gain_gr, lsc_gain[1], ISP_LSC_ARRAY, 0),
    FIELD(gain_gb, lsc_gain[2], ISP_LSC_ARRAY, 0), FIELD(gain_b, lsc_gain[3], ISP_LSC_ARRAY, 0),
};
static const sensor_isp_field_t s_blc[] = {
    ENABLE, FIELD(stretch_enable, stretch_enable, ISP_BOOL, 1),
    FIELD(top_left_offset, offsets[0], ISP_U16, 1), FIELD(top_right_offset, offsets[1], ISP_U16, 1),
    FIELD(bottom_left_offset, offsets[2], ISP_U16, 1), FIELD(bottom_right_offset, offsets[3], ISP_U16, 1),
};
static const sensor_isp_field_t s_bf[] = {
    ENABLE, FIELD(level, level, ISP_U8, 1), FIELD(matrix, matrix, ISP_FLOAT_ARRAY, ESP_VISION_ISP_MATRIX_SIZE),
};
static const sensor_isp_field_t s_sharpen[] = {
    ENABLE, FIELD(h_thresh, h_thresh, ISP_U8, 1), FIELD(l_thresh, l_thresh, ISP_U8, 1),
    FIELD(h_coeff, h_coeff, ISP_FLOAT, 1), FIELD(m_coeff, m_coeff, ISP_FLOAT, 1),
    FIELD(matrix, matrix, ISP_FLOAT_ARRAY, ESP_VISION_ISP_MATRIX_SIZE),
};
static const sensor_isp_field_t s_ccm[] = {ENABLE, FIELD(matrix, matrix, ISP_FLOAT_ARRAY, ESP_VISION_ISP_MATRIX_SIZE)};
static const sensor_isp_field_t s_gamma[] = {
    ENABLE, FIELD(x, x, ISP_U8_ARRAY, ESP_VISION_ISP_GAMMA_POINTS), FIELD(y, y, ISP_U8_ARRAY, ESP_VISION_ISP_GAMMA_POINTS),
};
#undef FIELD
#undef ENABLE

typedef struct {
    qstr name;
    const sensor_isp_field_t *fields;
    size_t count;
} sensor_isp_block_t;

#define BLOCK(name) {MP_QSTR_##name, s_##name, MP_ARRAY_SIZE(s_##name)}
static const sensor_isp_block_t s_blocks[ESP_VISION_ISP_BLOCK_COUNT] = {
    BLOCK(exposure), BLOCK(pixel_gain), BLOCK(demosaic), BLOCK(wb), BLOCK(lsc),
    BLOCK(blc), BLOCK(bf), BLOCK(sharpen), BLOCK(ccm), BLOCK(gamma),
};
#undef BLOCK

static esp_vision_isp_block_t sensor_isp_block(mp_obj_t name_in)
{
    // Look the name up without interning it, so misspelled block names do not
    // permanently consume qstr pool memory.
    size_t len;
    const char *str = mp_obj_str_get_data(name_in, &len);
    qstr name = qstr_find_strn(str, len);
    for (size_t i = 0; name != MP_QSTRnull && i < MP_ARRAY_SIZE(s_blocks); i++) {
        if (s_blocks[i].name == name) {
            return i;
        }
    }
    mp_raise_ValueError(MP_ERROR_TEXT("unknown ISP block"));
}

static void sensor_isp_raise(esp_err_t err)
{
    if (err == ESP_ERR_INVALID_ARG || err == ESP_ERR_INVALID_SIZE) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid ISP parameters"));
    }
    mp_raise_msg_varg(&mp_type_OSError, MP_ERROR_TEXT("ISP error: %s"), esp_err_to_name(err));
}

static esp_err_t sensor_isp_read(esp_vision_isp_block_t block, esp_vision_isp_config_t *config)
{
    if (block != ESP_VISION_ISP_LSC) {
        return esp_vision_camera_get_isp(block, config);
    }
    // Probe the table length first: a read that leaves the destinations NULL
    // only reports lsc_gain_size, which keeps the tables sized to the capture
    // instead of to ESP_VISION_ISP_LSC_MAX_POINTS.
    esp_err_t ret = esp_vision_camera_get_isp(block, config);
    if (ret != ESP_OK) {
        return ret;
    }
    if ((config->lsc_gain_size == 0) || (config->lsc_gain_size > ESP_VISION_ISP_LSC_MAX_POINTS)) {
        return ESP_ERR_INVALID_SIZE;
    }
    for (size_t i = 0; i < 4; i++) {
        config->lsc_gain[i] = m_new(float, config->lsc_gain_size);
    }
    return esp_vision_camera_get_isp(block, config);
}

static mp_obj_t sensor_isp_field_get(const sensor_isp_field_t *field, esp_vision_isp_config_t *config)
{
    void *data = (uint8_t *)config + field->offset;
    switch (field->type) {
    case ISP_BOOL:
        return mp_obj_new_bool(*(bool *)data);
    case ISP_U8:
        return mp_obj_new_int(*(uint8_t *)data);
    case ISP_U16:
        return mp_obj_new_int(*(uint16_t *)data);
    case ISP_I32:
        return mp_obj_new_int(*(int32_t *)data);
    case ISP_SIZE:
        return mp_obj_new_int_from_uint(*(size_t *)data);
    case ISP_FLOAT:
        return mp_obj_new_float(*(float *)data);
    default:
        break;
    }
    size_t count = field->type == ISP_LSC_ARRAY ? config->lsc_gain_size : field->count;
    if (field->type == ISP_LSC_ARRAY) {
        data = *(float **)data;
    }
    mp_obj_t result = mp_obj_new_list(count, NULL);
    mp_obj_list_t *list = MP_OBJ_TO_PTR(result);
    for (size_t i = 0; i < count; i++) {
        list->items[i] = field->type == ISP_U8_ARRAY ? mp_obj_new_int(((uint8_t *)data)[i]) :
                         mp_obj_new_float(((float *)data)[i]);
    }
    return result;
}

static float sensor_isp_float(mp_obj_t value)
{
    float result = mp_obj_get_float(value);
    if (!isfinite(result)) {
        mp_raise_ValueError(MP_ERROR_TEXT("ISP values must be finite"));
    }
    return result;
}

static mp_int_t sensor_isp_uint(mp_obj_t value, mp_int_t max)
{
    mp_int_t result = mp_obj_get_int(value);
    if (result < 0 || result > max) {
        mp_raise_ValueError(MP_ERROR_TEXT("ISP integer out of range"));
    }
    return result;
}

static void sensor_isp_field_set(const sensor_isp_field_t *field, esp_vision_isp_config_t *config, mp_obj_t value)
{
    void *data = (uint8_t *)config + field->offset;
    switch (field->type) {
    case ISP_BOOL:
        *(bool *)data = mp_obj_is_true(value);
        return;
    case ISP_U8:
        *(uint8_t *)data = sensor_isp_uint(value, UINT8_MAX);
        return;
    case ISP_U16:
        *(uint16_t *)data = sensor_isp_uint(value, UINT16_MAX);
        return;
    case ISP_I32:
        *(int32_t *)data = mp_obj_get_int(value);
        return;
    case ISP_SIZE:
        if ((size_t)sensor_isp_uint(value, ESP_VISION_ISP_LSC_MAX_POINTS) != config->lsc_gain_size) {
            mp_raise_ValueError(MP_ERROR_TEXT("LSC table size must match the capture size"));
        }
        return;
    case ISP_FLOAT:
        *(float *)data = sensor_isp_float(value);
        return;
    default:
        break;
    }
    size_t count;
    mp_obj_t *items;
    mp_obj_get_array(value, &count, &items);
    size_t expected = field->type == ISP_LSC_ARRAY ? config->lsc_gain_size : field->count;
    if (count != expected) {
        mp_raise_ValueError(MP_ERROR_TEXT("ISP array length mismatch"));
    }
    if (field->type == ISP_LSC_ARRAY) {
        data = *(float **)data;
    }
    for (size_t i = 0; i < count; i++) {
        if (field->type == ISP_U8_ARRAY) {
            ((uint8_t *)data)[i] = sensor_isp_uint(items[i], UINT8_MAX);
        } else {
            ((float *)data)[i] = sensor_isp_float(items[i]);
        }
    }
    if (field->name == MP_QSTR_x || field->name == MP_QSTR_y) {
        config->gamma_curve_changed = true;
    }
}

static mp_obj_t sensor_get_isp(mp_obj_t name_in)
{
    esp_vision_isp_block_t block = sensor_isp_block(name_in);
    esp_vision_isp_config_t config = {0};
    esp_err_t ret = sensor_isp_read(block, &config);
    if (ret == ESP_ERR_NOT_SUPPORTED) {
        return mp_const_none;
    }
    if (ret != ESP_OK) {
        sensor_isp_raise(ret);
    }
    const sensor_isp_block_t *schema = &s_blocks[block];
    mp_obj_t result = mp_obj_new_dict(schema->count);
    for (size_t i = 0; i < schema->count; i++) {
        mp_obj_dict_store(result, MP_OBJ_NEW_QSTR(schema->fields[i].name), sensor_isp_field_get(&schema->fields[i], &config));
    }
    return result;
}
MP_DEFINE_CONST_FUN_OBJ_1(sensor_get_isp_obj, sensor_get_isp);

static mp_obj_t sensor_set_isp(size_t n_args, const mp_obj_t *args, mp_map_t *kwargs)
{
    mp_arg_check_num(n_args, kwargs->used, 1, 1, true);
    esp_vision_isp_block_t block = sensor_isp_block(args[0]);
    if (kwargs->used == 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("ISP parameters are required"));
    }
    bool ipa_enabled = false;
    esp_err_t ret = esp_vision_camera_get_ipa(&ipa_enabled);
    if (ret == ESP_OK && ipa_enabled) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("disable IPA before setting ISP parameters"));
    }
    esp_vision_isp_config_t config = {0};
    // Scalar controls replace their sole value and do not require readback.
    if (block != ESP_VISION_ISP_EXPOSURE && block != ESP_VISION_ISP_PIXEL_GAIN) {
        ret = sensor_isp_read(block, &config);
        if (ret != ESP_OK) {
            sensor_isp_raise(ret);
        }
    }
    const sensor_isp_block_t *schema = &s_blocks[block];
    size_t used = 0;
    for (size_t i = 0; i < schema->count; i++) {
        const sensor_isp_field_t *field = &schema->fields[i];
        mp_map_elem_t *entry = mp_map_lookup(kwargs, MP_OBJ_NEW_QSTR(field->name), MP_MAP_LOOKUP);
        if (entry != NULL) {
            sensor_isp_field_set(field, &config, entry->value);
            if (field->name != MP_QSTR_enable) {
                config.params_changed = true;
            }
            used++;
        }
    }
    if (used != kwargs->used) {
        mp_raise_TypeError(MP_ERROR_TEXT("unknown parameter for ISP block"));
    }
    ret = esp_vision_camera_set_isp(block, &config);
    if (ret != ESP_OK) {
        sensor_isp_raise(ret);
    }
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_KW(sensor_set_isp_obj, 1, sensor_set_isp);
