/****************************************************************************
 * apps/graphics/lvgl/lv_porting/lv_gpu_draw.c
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

#include "lv_gpu_draw.h"
#include <stdlib.h>

/*! 32-bit RGBA format with 8 bits per color channel. Red is in bits 7:0, green
   in bits 15:8, blue in bits 23:16, and the alpha channel is in bits 31:24. */
vg_lite_color_t get_vg_lite_color(uint32_t color_argb888, lv_opa_t opa)
{
    uint32_t alpha = opa;
    return (color_argb888 & (alpha << 24)) | ((color_argb888 & 0xFF) << 16)
        | (color_argb888 & 0xFF00) | ((color_argb888 & 0xFF0000) >> 16);
}

vg_lite_blend_t get_vg_lite_blend(lv_blend_mode_t blend_mode)
{
    /**< Simply mix according to the opacity value*/
    // LV_BLEND_MODE_NORMAL,

    /**< Add the respective color channels*/
    // LV_BLEND_MODE_ADDITIVE,

    /**< Subtract the foreground from the background*/
    // LV_BLEND_MODE_SUBTRACTIVE,

    /**< Multiply the foreground and background*/
    // LV_BLEND_MODE_MULTIPLY,
    if (blend_mode == LV_BLEND_MODE_NORMAL) {
        return VG_LITE_BLEND_SRC_OVER;
    } else if (blend_mode == LV_BLEND_MODE_ADDITIVE) {
        return VG_LITE_BLEND_ADDITIVE;
    } else if (blend_mode == LV_BLEND_MODE_SUBTRACTIVE) {
        return VG_LITE_BLEND_SUBTRACT;
    } else if (blend_mode == LV_BLEND_MODE_MULTIPLY) {
        return VG_LITE_BLEND_MULTIPLY;
    }
    return VG_LITE_BLEND_NONE;
}

int32_t malloc_float_path_data(vg_lite_path_t* vg_lite_path,
                               uint8_t* path_cmds, size_t cmds_size)
{
    int32_t data_size
        = vg_lite_path_calc_length(path_cmds, cmds_size, VG_LITE_FP32);

    vg_lite_path->path = malloc(data_size);
    memset(vg_lite_path->path, 0, data_size);

    return data_size;
}