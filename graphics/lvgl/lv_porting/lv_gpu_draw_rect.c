/****************************************************************************
 * apps/graphics/lvgl/lv_porting/lv_gpu_draw_arc.c
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

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "lv_gpu_draw.h"

#include "lv_gpu_interface.h"

typedef enum {
    RECT_TYPE_ROUNDED,
    RECT_TYPE_DEFAULT,
} rect_type;

uint8_t rounded_rect_path_cmds[] = {
    VLC_OP_MOVE,
    VLC_OP_LINE,
    VLC_OP_SCCWARC,
    VLC_OP_LINE,
    VLC_OP_SCCWARC,
    VLC_OP_LINE,
    VLC_OP_SCCWARC,
    VLC_OP_LINE,
    VLC_OP_SCCWARC,
    VLC_OP_END
};

static uint8_t default_rect_path_cmds[] = {
    VLC_OP_MOVE,
    VLC_OP_LINE,
    VLC_OP_LINE,
    VLC_OP_LINE,
    VLC_OP_END
};

static float get_draw_radius(lv_coord_t rect_width,lv_coord_t rect_height,
                             lv_coord_t radius)
{
    float short_side = LV_MIN(rect_width, rect_height);
    if (radius > short_side / 2) {
        return short_side / 2;
    }
    return radius;
}

static void draw_gradient_path(vg_lite_buffer_t* vg_lite_buffer,
                               vg_lite_path_t* vg_lite_path,
                               vg_lite_matrix_t* path_matrix,
                               const lv_area_t* draw_area,
                               float rect_width, float rect_height,
                               const lv_draw_rect_dsc_t* dsc,
                               lv_grad_dir_t new_grad_dir,
                               lv_opa_t new_opa,
                               vg_lite_blend_t blend)
{
    vg_lite_linear_gradient_t grad;
    memset(&grad, 0, sizeof(grad));
    if (VG_LITE_SUCCESS != vg_lite_init_grad(&grad)) {
        GPU_ERROR("Linear gradient is not supported.\n");
        return;
    }

    uint32_t start_color = lv_color_to32(dsc->bg_color);
    uint32_t end_color = lv_color_to32(dsc->bg_grad_color);

    uint32_t ramps[] = { start_color, start_color, end_color};
    uint32_t stops[] = { 0, dsc->bg_main_color_stop, dsc->bg_grad_color_stop};

    vg_lite_set_grad(&grad, 3, ramps, stops);
    vg_lite_update_grad_as_lvgl(&grad, new_opa);

    vg_lite_matrix_t* mat_grad = vg_lite_get_grad_matrix(&grad);
    vg_lite_identity(mat_grad);
    vg_lite_translate(draw_area->x1, draw_area->y1, mat_grad);

    if (new_grad_dir == LV_GRAD_DIR_VER) {
        vg_lite_rotate(90.0f, mat_grad);
        vg_lite_scale(rect_height / 256, rect_width / 256, mat_grad);
    } else {
        vg_lite_scale(rect_width / 256, rect_height / 256, mat_grad);
    }

    vg_lite_draw_gradient(vg_lite_buffer, vg_lite_path, VG_LITE_FILL_NON_ZERO,
                          path_matrix, &grad, blend);

    vg_lite_clear_grad(&grad);
}

static void fill_rect_border_half(vg_lite_path_t* vg_lite_path,
                             int8_t x_vector, float center_x, float center_y,
                             float half_rect_width, float half_rect_height,
                             float small_half_rect_width,
                             float small_half_rect_height)
{
    const uint8_t path_cmds[] = {
        VLC_OP_MOVE,
        VLC_OP_LINE,
        VLC_OP_LINE,
        VLC_OP_LINE,
        VLC_OP_LINE,
        VLC_OP_LINE,
        VLC_OP_LINE,
        VLC_OP_LINE,
        VLC_OP_END
    };

    const float path_data_float[] = {
        center_x, center_y - half_rect_height,
        center_x + half_rect_width * x_vector, center_y - half_rect_height,
        center_x + half_rect_width * x_vector, center_y + half_rect_height,
        center_x, center_y + half_rect_height,
        center_x, center_y + small_half_rect_height,
        center_x + small_half_rect_width * x_vector, center_y + small_half_rect_height,
        center_x + small_half_rect_width * x_vector, center_y - small_half_rect_height,
        center_x, center_y - small_half_rect_height,
    };

    malloc_float_path_data(vg_lite_path, path_cmds, sizeof(path_cmds));

    vg_lite_path_append(vg_lite_path, path_cmds, path_data_float, sizeof(path_cmds));
}

static void fill_rounded_rect_border_half(vg_lite_path_t* vg_lite_path,
                             int8_t x_vector, float center_x, float center_y,
                             float radius, float small_radius,
                             float half_rect_width, float half_rect_height,
                             float small_half_rect_width,
                             float small_half_rect_height)
{
    uint8_t path_cmds[] = {
        VLC_OP_MOVE,
        VLC_OP_LINE,
        VLC_OP_SCCWARC,
        VLC_OP_LINE,
        VLC_OP_SCCWARC,
        VLC_OP_LINE,
        VLC_OP_LINE,
        VLC_OP_LINE,
        VLC_OP_SCWARC,
        VLC_OP_LINE,
        VLC_OP_SCWARC,
        VLC_OP_LINE,
        VLC_OP_END
    };

    if (x_vector < 0) {
        path_cmds[2] = VLC_OP_SCWARC;
        path_cmds[4] = VLC_OP_SCWARC;
        path_cmds[8] = VLC_OP_SCCWARC;
        path_cmds[10] = VLC_OP_SCCWARC;
    }

    int32_t data_size = malloc_float_path_data(vg_lite_path, path_cmds,
                                               sizeof(path_cmds));

    char    *pchar;
    float   *pfloat;

    pchar = (char*)vg_lite_path->path;
    pfloat = (float*)vg_lite_path->path;
    *pchar = path_cmds[0];
    pfloat++;
    *pfloat++ = center_x;
    *pfloat++ = center_y - half_rect_height;

    pchar = (char*)pfloat;
    *pchar = path_cmds[1];
    pfloat++;
    *pfloat++ = center_x + (half_rect_width - radius) * x_vector;
    *pfloat++ = center_y - half_rect_height;

    pchar = (char*)pfloat;
    *pchar = path_cmds[2];
    pfloat++;
    *pfloat++ = radius;
    *pfloat++ = radius;
    *pfloat++ = 0;
    *pfloat++ = center_x + half_rect_width * x_vector;
    *pfloat++ = center_y - half_rect_height + radius;

    pchar = (char*)pfloat;
    *pchar = path_cmds[3];
    pfloat++;
    *pfloat++ = center_x + half_rect_width * x_vector;
    *pfloat++ = center_y + half_rect_height - radius;

    pchar = (char*)pfloat;
    *pchar = path_cmds[4];
    pfloat++;
    *pfloat++ = radius;
    *pfloat++ = radius;
    *pfloat++ = 0;
    *pfloat++ = center_x + (half_rect_width - radius) * x_vector;
    *pfloat++ = center_y + half_rect_height;

    pchar = (char*)pfloat;
    *pchar = path_cmds[5];
    pfloat++;
    *pfloat++ = center_x;
    *pfloat++ = center_y + half_rect_height;

    pchar = (char*)pfloat;
    *pchar = path_cmds[6];
    pfloat++;
    *pfloat++ = center_x;
    *pfloat++ = center_y + small_half_rect_height;

    pchar = (char*)pfloat;
    *pchar = path_cmds[7];
    pfloat++;
    *pfloat++ = center_x + (small_half_rect_width - small_radius) * x_vector;
    *pfloat++ = center_y + small_half_rect_height;

    pchar = (char*)pfloat;
    *pchar = path_cmds[8];
    pfloat++;
    *pfloat++ = small_radius;
    *pfloat++ = small_radius;
    *pfloat++ = 0;
    *pfloat++ = center_x + small_half_rect_width * x_vector;
    *pfloat++ = center_y + small_half_rect_height - small_radius;

    pchar = (char*)pfloat;
    *pchar = path_cmds[9];
    pfloat++;
    *pfloat++ = center_x + small_half_rect_width * x_vector;
    *pfloat++ = center_y - small_half_rect_height + small_radius;

    pchar = (char*)pfloat;
    *pchar = path_cmds[10];
    pfloat++;
    *pfloat++ = radius;
    *pfloat++ = radius;
    *pfloat++ = 0;
    *pfloat++ = center_x + (small_half_rect_width - small_radius) * x_vector;
    *pfloat++ = center_y - small_half_rect_height;

    pchar = (char*)pfloat;
    *pchar = path_cmds[9];
    pfloat++;
    *pfloat++ = center_x;
    *pfloat++ = center_y - small_half_rect_height;

    pchar = (char*)pfloat;
    *pchar = 0;

    vg_lite_init_arc_path(vg_lite_path, VG_LITE_FP32, VG_LITE_HIGH,
                          data_size, vg_lite_path->path, 0, 0, 0, 0);
}

static void draw_full_border_path(vg_lite_buffer_t* vg_buf, vg_lite_path_t* vg_lite_path,
                                  rect_type draw_rect_type, const lv_area_t* draw_coords,
                                  lv_coord_t rect_width, lv_coord_t rect_height,
                                  float radius, float border_width,
                                  const lv_area_t* clip_area, vg_lite_color_t color,
                                  vg_lite_blend_t blend)
{
    const float half_rect_width = ((float)rect_width) / 2;
    const float half_rect_height = ((float)rect_height) / 2;

    const float center_x = draw_coords->x1 + half_rect_width;
    const float center_y = draw_coords->y1 + half_rect_height;

    const float small_radius = radius - border_width;

    const float small_half_rect_width = half_rect_width - border_width;
    const float small_half_rect_height = half_rect_height - border_width;

    vg_lite_matrix_t path_matrix;
    vg_lite_identity(&path_matrix);

    int8_t x_vector = -1;
    for (int i = 0; i < 2; i++) {
        if (i == 1) {
            x_vector = 1;
        }

        if (draw_rect_type == RECT_TYPE_DEFAULT) {
            fill_rect_border_half(vg_lite_path, x_vector, center_x, center_y,
                                  half_rect_width, half_rect_height,
                                  small_half_rect_width, small_half_rect_height);
        } else {
            fill_rounded_rect_border_half(vg_lite_path, x_vector, center_x, center_y,
                                          radius, small_radius, half_rect_width,
                                          half_rect_height, small_half_rect_width,
                                          small_half_rect_height);
        }

        fill_path_clip_area(vg_lite_path, clip_area);
        vg_lite_draw(vg_buf, vg_lite_path, VG_LITE_FILL_NON_ZERO,
                &path_matrix, blend, color);
        free(vg_lite_path->path);
    }
}

static bool draw_outline_path(vg_lite_buffer_t* vg_buf, vg_lite_path_t* vg_lite_path,
                             rect_type draw_rect_type, const lv_area_t* draw_coords,
                             const lv_area_t* clip_area, const lv_draw_rect_dsc_t* dsc,
                             float draw_radius, vg_lite_blend_t blend)
{
    lv_opa_t opa = dsc->outline_opa;
    if(opa > LV_OPA_MAX) opa = LV_OPA_COVER;

    uint32_t outline_color_argb8888 = lv_color_to32(dsc->outline_color);
    vg_lite_color_t outline_color = get_vg_lite_color_lvgl_mix(
        outline_color_argb8888, opa);

    lv_area_t outline_coords;
    lv_area_copy(&outline_coords, draw_coords);

    lv_coord_t pad = dsc->outline_pad - 1;
    outline_coords.x1 -= (pad + dsc->outline_width);
    outline_coords.y1 -= (pad + dsc->outline_width);
    outline_coords.x2 += (pad + dsc->outline_width);
    outline_coords.y2 += (pad + dsc->outline_width);

    lv_area_t res_p;
    if (_lv_area_intersect(&res_p, &outline_coords, clip_area) == false) {
        return false;
    }

    draw_radius = draw_radius + pad + dsc->outline_width;

    lv_coord_t rect_width = outline_coords.x2 - outline_coords.x1 + 1;
    lv_coord_t rect_height = outline_coords.y2 - outline_coords.y1 + 1;

    draw_full_border_path(vg_buf, vg_lite_path, draw_rect_type, &outline_coords,
                          rect_width, rect_height, draw_radius, dsc->outline_width,
                          clip_area, outline_color, blend);
    return true;
}

static void fill_rounded_rect_path_data(vg_lite_path_t* vg_lite_path,
                                        uint8_t* path_cmds, float radius,
                                        float rect_width, float rect_height,
                                        float center_x, float center_y)
{
    float half_rect_width = rect_width / 2;
    float half_rect_height = rect_height / 2;

    char* pchar;
    float* pfloat;

    pchar = (char*)vg_lite_path->path;
    pfloat = (float*)vg_lite_path->path;
    *pchar = path_cmds[0];
    pfloat++;
    *pfloat++ = center_x - half_rect_width + radius;
    *pfloat++ = center_y - half_rect_height;

    pchar = (char*)pfloat;
    *pchar = path_cmds[1];
    pfloat++;
    *pfloat++ = center_x + half_rect_width - radius;
    *pfloat++ = center_y - half_rect_height;

    pchar = (char*)pfloat;
    *pchar = path_cmds[2];
    pfloat++;
    *pfloat++ = radius;
    *pfloat++ = radius;
    *pfloat++ = 0;
    *pfloat++ = center_x + half_rect_width;
    *pfloat++ = center_y - half_rect_height + radius;

    pchar = (char*)pfloat;
    *pchar = path_cmds[3];
    pfloat++;
    *pfloat++ = center_x + half_rect_width;
    *pfloat++ = center_y + half_rect_height - radius;

    pchar = (char*)pfloat;
    *pchar = path_cmds[4];
    pfloat++;
    *pfloat++ = radius;
    *pfloat++ = radius;
    *pfloat++ = 0;
    *pfloat++ = center_x + half_rect_width - radius;
    *pfloat++ = center_y + half_rect_height;

    pchar = (char*)pfloat;
    *pchar = path_cmds[5];
    pfloat++;
    *pfloat++ = center_x - half_rect_width + radius;
    *pfloat++ = center_y + half_rect_height;

    pchar = (char*)pfloat;
    *pchar = path_cmds[6];
    pfloat++;
    *pfloat++ = radius;
    *pfloat++ = radius;
    *pfloat++ = 0;
    *pfloat++ = center_x - half_rect_width;
    *pfloat++ = center_y + half_rect_height - radius;

    pchar = (char*)pfloat;
    *pchar = path_cmds[7];
    pfloat++;
    *pfloat++ = center_x - half_rect_width;
    *pfloat++ = center_y - half_rect_height + radius;

    pchar = (char*)pfloat;
    *pchar = path_cmds[8];
    pfloat++;
    *pfloat++ = radius;
    *pfloat++ = radius;
    *pfloat++ = 0;
    *pfloat++ = center_x - half_rect_width + radius;
    *pfloat++ = center_y - half_rect_height;

    pchar = (char*)pfloat;
    *pchar = 0;
}

static void draw_bg_path(vg_lite_buffer_t* vg_buf, vg_lite_path_t* vg_lite_path,
                         rect_type draw_rect_type, const void* img_src,
                         const lv_area_t* coords, const lv_area_t* clip_area,
                         const lv_draw_rect_dsc_t* dsc, bool draw_border,
                         vg_lite_blend_t blend)
{
    if(dsc->bg_opa <= LV_OPA_MIN) return;

    lv_area_t draw_coords;
    lv_area_copy(&draw_coords, coords);

    lv_coord_t rect_width = draw_coords.x2 - draw_coords.x1 + 1;
    lv_coord_t rect_height = draw_coords.y2 - draw_coords.y1 + 1;

    lv_opa_t opa = dsc->bg_opa >= LV_OPA_MAX ? LV_OPA_COVER : dsc->bg_opa;
    lv_grad_dir_t grad_dir = dsc->bg_grad_dir;
    if(dsc->bg_color.full == dsc->bg_grad_color.full) grad_dir = LV_GRAD_DIR_NONE;

    vg_lite_matrix_t path_matrix;
    vg_lite_identity(&path_matrix);

    uint32_t bg_color_argb8888 = lv_color_to32(dsc->bg_color);
    vg_lite_color_t bg_color = get_vg_lite_color_lvgl_mix(bg_color_argb8888,
                                                          opa);

    if (draw_rect_type == RECT_TYPE_DEFAULT) {
        malloc_float_path_data(vg_lite_path, default_rect_path_cmds,
                               sizeof(default_rect_path_cmds));

        float path_data_float[] = {
            draw_coords.x1, draw_coords.y1,
            draw_coords.x2, draw_coords.y1,
            draw_coords.x2, draw_coords.y2,
            draw_coords.x1, draw_coords.y2,
        };

        vg_lite_path_append(vg_lite_path, default_rect_path_cmds,
                            path_data_float, sizeof(default_rect_path_cmds));
    } else {
        float draw_radius = get_draw_radius(rect_width, rect_height, dsc->radius);

        float center_x = draw_coords.x1 + rect_width / 2;
        float center_y = draw_coords.y1 + rect_height / 2;

        int32_t data_size
            = malloc_float_path_data(vg_lite_path, rounded_rect_path_cmds,
                                     sizeof(rounded_rect_path_cmds));

        fill_rounded_rect_path_data(vg_lite_path, rounded_rect_path_cmds,
                                    draw_radius, rect_width, rect_height,
                                    center_x, center_y);

        vg_lite_init_arc_path(vg_lite_path, VG_LITE_FP32, VG_LITE_HIGH,
                              data_size, vg_lite_path->path, 0, 0, 0, 0);
    }

    fill_path_clip_area(vg_lite_path, clip_area);

    if (img_src) {
        // TODO: bg image
    } else {
        if (grad_dir == LV_GRAD_DIR_NONE) {
            vg_lite_draw(vg_buf, vg_lite_path, VG_LITE_FILL_NON_ZERO,
                         &path_matrix, blend, bg_color);
        } else {
            draw_gradient_path(vg_buf, vg_lite_path, &path_matrix, &draw_coords,
                               rect_width, rect_height, dsc, grad_dir, opa, blend);
        }
    }

    free(vg_lite_path->path);
}

bool draw_rect_path(vg_lite_buffer_t* vg_buf, vg_lite_path_t* vg_lite_path,
                    const lv_area_t* coords, const lv_area_t* clip,
                    const lv_draw_rect_dsc_t* dsc)
{
    rect_type draw_rect_type = RECT_TYPE_ROUNDED;

    if (dsc->radius == 0) {
        draw_rect_type = RECT_TYPE_DEFAULT;
    }

    vg_lite_blend_t vg_lite_blend = get_vg_lite_blend(dsc->blend_mode);

    int draw_something = 0;

    lv_coord_t rect_width = coords->x2 - coords->x1 + 1;
    lv_coord_t rect_height = coords->y2 - coords->y1 + 1;

    if (rect_width < 3 || rect_height < 3) {
        draw_rect_type = RECT_TYPE_DEFAULT;
    }

    float draw_radius = 0;
    if (dsc->radius > 0) {
        draw_radius = get_draw_radius(rect_width, rect_height, dsc->radius);
    }

    lv_area_t draw_clip;
    lv_area_copy(&draw_clip, clip);
    draw_clip.x2 += 1;
    draw_clip.y2 += 1;

    lv_area_t res_p;
    if (_lv_area_intersect(&res_p, coords, &draw_clip) == true) {
        draw_something++;

        bool draw_border = false;

        if (dsc->border_opa > LV_OPA_MIN && dsc->border_width != 0
            && dsc->border_side != LV_BORDER_SIDE_NONE && !dsc->border_post) {
            draw_border = true;
        }

        draw_bg_path(vg_buf, vg_lite_path, draw_rect_type, NULL, coords,
                     &draw_clip, dsc, draw_border, vg_lite_blend);

        if (draw_border == true) {
            lv_opa_t opa = dsc->border_opa;
            if(opa > LV_OPA_MAX) opa = LV_OPA_COVER;

            uint32_t border_color_argb8888 = lv_color_to32(dsc->border_color);
            vg_lite_color_t border_color = get_vg_lite_color_lvgl_mix(
                                    border_color_argb8888, opa);

            float draw_border_width = dsc->border_width;
            if (draw_radius > 0 && draw_border_width >= draw_radius) {
                draw_border_width = draw_radius - 1;
            }

            draw_full_border_path(vg_buf, vg_lite_path, draw_rect_type,
                                  coords, rect_width, rect_height,
                                  draw_radius, draw_border_width, &draw_clip,
                                  border_color, vg_lite_blend);
        }
    }

    if(dsc->outline_opa > LV_OPA_MIN && dsc->outline_width != 0) {
        if (draw_outline_path(vg_buf, vg_lite_path, draw_rect_type, coords,
                              &draw_clip, dsc, draw_radius, vg_lite_blend) == true) {
            draw_something++;
        }
    }
    return draw_something > 0;
}