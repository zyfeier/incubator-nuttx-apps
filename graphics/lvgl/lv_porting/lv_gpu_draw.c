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

#include "lv_gpu_interface.h"

#define USE_GLOBAL_PATH_DATA_BUFFER 1

#if (USE_GLOBAL_PATH_DATA_BUFFER == 1)
uint8_t g_path_data_buffer[196];
#endif

#define R(color) ((color) & 0x00ff0000) >> 16
#define G(color) ((color) & 0x0000ff00) >> 8
#define B(color) ((color) & 0xff)
#define ARGB(a, r, g, b) ((a) << 24) | ((r) << 16) | ((g) << 8 ) | (b)
#define ABGR(a, b, g, r) ((a) << 24) | ((b) << 16) | ((g) << 8 ) | (r)

/*! 32-bit RGBA format with 8 bits per color channel. Red is in bits 7:0, green
   in bits 15:8, blue in bits 23:16, and the alpha channel is in bits 31:24. */
vg_lite_color_t get_vg_lite_color_lvgl_mix(uint32_t color_argb888, lv_opa_t opa)
{
    uint32_t alpha = opa;
    return ABGR(alpha, LV_UDIV255((color_argb888 & 0xFF) * opa),
                LV_UDIV255(((color_argb888 & 0xFF00) >> 8) * opa),
                LV_UDIV255(((color_argb888 & 0xFF0000) >> 16) * opa));
}

uint32_t get_lvgl_mix_color(uint32_t color_argb888, uint8_t opa)
{
    uint32_t alpha = opa;
    return ARGB(alpha, LV_UDIV255(((color_argb888 & 0xFF0000) >> 16) * opa),
                LV_UDIV255(((color_argb888 & 0xFF00) >> 8) * opa),
                LV_UDIV255((color_argb888 & 0xFF) * opa));
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

void malloc_float_path_data(vg_lite_path_t* vg_lite_path,
                            lv_gpu_path_data_t* path_data, uint8_t* path_cmds,
                            size_t cmds_size)
{
    int32_t data_size
        = vg_lite_path_calc_length(path_cmds, cmds_size, VG_LITE_FP32);

    vg_lite_init_path(vg_lite_path, VG_LITE_FP32, VG_LITE_HIGH, data_size, NULL,
                      0, 0, 0, 0);

    path_data->data_size = data_size;

#if (USE_GLOBAL_PATH_DATA_BUFFER == 1)
    path_data->data = g_path_data_buffer;
#else
    path_data->data = malloc(data_size);
    memset(path_data->data, 0, data_size);
#endif

    vg_lite_path->path = path_data->data;
}

void free_float_path_data(lv_gpu_path_data_t* path_data)
{
#if (USE_GLOBAL_PATH_DATA_BUFFER == 0)
    free(path_data->data);
#endif
}

void fill_path_clip_area(vg_lite_path_t* vg_lite_path,
                         const lv_area_t* clip_area)
{
    vg_lite_path->bounding_box[0] = clip_area->x1;
    vg_lite_path->bounding_box[1] = clip_area->y1;
    vg_lite_path->bounding_box[2] = clip_area->x2;
    vg_lite_path->bounding_box[3] = clip_area->y2;
}

size_t init_vg_lite_buffer_use_lv_buffer(vg_lite_buffer_t* vgbuf)
{
    lv_disp_t* disp = _lv_refr_get_disp_refreshing();
    lv_disp_draw_buf_t* draw_buf = lv_disp_get_draw_buf(disp);
    const lv_area_t* disp_area = &draw_buf->area;
    lv_color_t* disp_buf = draw_buf->buf_act;
    int32_t disp_w = lv_area_get_width(disp_area);
    int32_t disp_h = lv_area_get_height(disp_area);
    uint32_t stride = disp_w * sizeof(lv_color_t);

    lv_res_t ret = init_vg_buf(vgbuf, disp_w, disp_h, stride, disp_buf,
                               VGLITE_PX_FMT, false);
    LV_ASSERT(ret == LV_RES_OK);

    return disp_h * stride;
}

vg_lite_error_t vg_lite_update_grad_as_lvgl(vg_lite_linear_gradient_t* grad,
                                            uint8_t global_alpha)
{
    vg_lite_error_t error = VG_LITE_SUCCESS;
    int32_t r0, g0, b0;
    int32_t r1, g1, b1;
    int32_t lr, lg, lb;
    uint32_t i;
    int32_t j;
    int32_t ds, dr, dg, db;
    uint32_t *buffer = (uint32_t *)grad->image.memory;

    if (grad->count == 0) {
        /* If no valid stops have been specified (e.g., due to an empty input
        * array, out-of-range, or out-of-order stops), a stop at 0 with color
        * 0xFF000000 (opaque black) and a stop at 255 with color 0xFFFFFFFF
        * (opaque white) are implicitly defined. */
        grad->stops[0] = 0;
        grad->colors[0] = 0xFF000000;   /* Opaque black */
        grad->stops[1] = 255;
        grad->colors[1] = 0xFFFFFFFF;   /* Opaque white */
        grad->count = 2;
    } else if (grad->count && grad->stops[0] != 0) {
        /* If at least one valid stop has been specified, but none has been
        * defined with an offset of 0, an implicit stop is added with an
        * offset of 0 and the same color as the first user-defined stop. */
        for (i = 0; i < grad->stops[0]; i++)
            buffer[i] = grad->colors[0];
    }

    r0 = R(grad->colors[0]);
    g0 = G(grad->colors[0]);
    b0 = B(grad->colors[0]);

    /* Calculate the colors for each pixel of the image. */
    for (i = 0; i < grad->count - 1; i++) {
        buffer[grad->stops[i]] = get_lvgl_mix_color(grad->colors[i], global_alpha);

        ds = grad->stops[i + 1] - grad->stops[i];

        r1 = R(grad->colors[i + 1]);
        g1 = G(grad->colors[i + 1]);
        b1 = B(grad->colors[i + 1]);

        dr = r1 - r0;
        dg = g1 - g0;
        db = b1 - b0;

        for (j = 1; j < ds; j++) {
            lr = r0 + dr * j / ds;
            lg = g0 + dg * j / ds;
            lb = b0 + db * j / ds;

            lr = LV_UDIV255(global_alpha * lr);
            lg = LV_UDIV255(global_alpha * lg);
            lb = LV_UDIV255(global_alpha * lb);

            buffer[grad->stops[i] + j] = ARGB(global_alpha, lr, lg, lb);
        }

        r0 = r1;
        g0 = g1;
        b0 = b1;
    }

    /* If at least one valid stop has been specified, but none has been defined
    * with an offset of 255, an implicit stop is added with an offset of 255
    * and the same color as the last user-defined stop. */
    for (i = grad->stops[grad->count - 1]; i < 255; i++)
        buffer[i] = get_lvgl_mix_color(grad->colors[grad->count - 1], global_alpha);
    /* Last pixel */
    buffer[i] = get_lvgl_mix_color(grad->colors[grad->count - 1], global_alpha);
    return error;
}