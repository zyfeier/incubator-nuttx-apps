/****************************************************************************
 * apps/graphics/lvgl/lv_porting/gpu/lv_gpu_draw_arc.c
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

#include "../lv_gpu_interface.h"

// bezier control point angle 45 proportion with radius
#define BEZIER_CTRL_P_45_P_R (0.414213f)

// angle 45 proportion with radius
#define ANGLE_45_P_R (0.707106f)

typedef enum {
    RECT_TYPE_ROUNDED,
    RECT_TYPE_DEFAULT,
} rect_type;

static uint8_t rounded_rect_path_cmds[] = {
    VLC_OP_MOVE,
    VLC_OP_LINE,
    VLC_OP_QUAD,
    VLC_OP_QUAD,
    VLC_OP_LINE,
    VLC_OP_QUAD,
    VLC_OP_QUAD,
    VLC_OP_LINE,
    VLC_OP_QUAD,
    VLC_OP_QUAD,
    VLC_OP_LINE,
    VLC_OP_QUAD,
    VLC_OP_QUAD,
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

    uint8_t stops_count = dsc->bg_grad.stops_count;
    lv_gradient_stop_t* bg_stops = dsc->bg_grad.stops;

    uint32_t ramps[LV_GRADIENT_MAX_STOPS + 1];
    uint32_t stops[LV_GRADIENT_MAX_STOPS + 1];

    ramps[0] = lv_color_to32(bg_stops[0].color);
    stops[0] = 0;

    for (uint8_t i = 0; i < stops_count; i++) {
        ramps[i + 1] = lv_color_to32(bg_stops[i].color);
        stops[i + 1] = bg_stops[i].frac;
    }

    vg_lite_set_grad(&grad, stops_count + 1, ramps, stops);
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
                                  lv_gpu_path_data_t* path_data,
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

    malloc_float_path_data(vg_lite_path, path_data, path_cmds, sizeof(path_cmds));

    vg_lite_path_append(vg_lite_path, path_cmds, path_data_float, sizeof(path_cmds));
}

static void fill_rounded_rect_border_half(vg_lite_path_t* vg_lite_path,
                             lv_gpu_path_data_t* path_data,
                             int8_t x_vector, float center_x, float center_y,
                             float radius, float small_radius,
                             float half_rect_width, float half_rect_height,
                             float small_half_rect_width,
                             float small_half_rect_height)
{
    // big
    const float offset_bezier_ctrl_p_b = BEZIER_CTRL_P_45_P_R * radius;
    const float offset_angle_45_b = ANGLE_45_P_R * radius;

    float half_r_w_b_no_radius = half_rect_width - radius;
    float half_r_h_b_no_radius = half_rect_height - radius;

    // small
    const float offset_bezier_ctrl_p_s = BEZIER_CTRL_P_45_P_R * small_radius;
    const float offset_angle_45_s = ANGLE_45_P_R * small_radius;

    float half_r_w_s_no_radius = small_half_rect_width - small_radius;
    float half_r_h_s_no_radius = small_half_rect_height - small_radius;

    uint8_t path_cmds[] = {
        VLC_OP_MOVE,
        VLC_OP_LINE,
        VLC_OP_QUAD,
        VLC_OP_QUAD,
        VLC_OP_LINE,
        VLC_OP_QUAD,
        VLC_OP_QUAD,
        VLC_OP_LINE,
        VLC_OP_LINE,
        VLC_OP_LINE,
        VLC_OP_QUAD,
        VLC_OP_QUAD,
        VLC_OP_LINE,
        VLC_OP_QUAD,
        VLC_OP_QUAD,
        VLC_OP_LINE,
        VLC_OP_END
    };

    malloc_float_path_data(vg_lite_path, path_data, path_cmds, sizeof(path_cmds));

    float tmp_path_data[] = {
        center_x, center_y - half_rect_height,

        center_x + x_vector * half_r_w_b_no_radius, center_y - half_rect_height,

        center_x + x_vector * (half_r_w_b_no_radius + offset_bezier_ctrl_p_b), center_y - half_rect_height,
        center_x + x_vector * (half_r_w_b_no_radius + offset_angle_45_b), center_y - half_r_h_b_no_radius - offset_angle_45_b,

        center_x + x_vector * half_rect_width, center_y - half_r_h_b_no_radius - offset_bezier_ctrl_p_b,
        center_x + x_vector * half_rect_width, center_y - half_r_h_b_no_radius,

        center_x + x_vector * half_rect_width, center_y + half_r_h_b_no_radius,

        center_x + x_vector * half_rect_width, center_y + half_r_h_b_no_radius + offset_bezier_ctrl_p_b,
        center_x + x_vector * (half_r_w_b_no_radius + offset_angle_45_b), center_y + half_r_h_b_no_radius + offset_angle_45_b,

        center_x + x_vector * (half_r_w_b_no_radius + offset_bezier_ctrl_p_b), center_y + half_rect_height,
        center_x + x_vector * half_r_w_b_no_radius, center_y + half_rect_height,

        center_x, center_y + half_rect_height,

        center_x, center_y + small_half_rect_height,

        center_x + x_vector * half_r_w_s_no_radius, center_y + small_half_rect_height,

        center_x + x_vector * (half_r_w_s_no_radius + offset_bezier_ctrl_p_s), center_y + small_half_rect_height,
        center_x + x_vector * (half_r_w_s_no_radius + offset_angle_45_s), center_y + half_r_h_s_no_radius + offset_angle_45_s,

        center_x + x_vector * small_half_rect_width, center_y + half_r_h_s_no_radius + offset_bezier_ctrl_p_s,
        center_x + x_vector * small_half_rect_width, center_y + half_r_h_s_no_radius,

        center_x + x_vector * small_half_rect_width, center_y - half_r_h_s_no_radius,

        center_x + x_vector * small_half_rect_width, center_y - half_r_h_s_no_radius - offset_bezier_ctrl_p_s,
        center_x + x_vector * (half_r_w_s_no_radius + offset_angle_45_s), center_y - half_r_h_s_no_radius - offset_angle_45_s,

        center_x + x_vector * (half_r_w_s_no_radius + offset_bezier_ctrl_p_s), center_y - small_half_rect_height,
        center_x + x_vector * half_r_w_s_no_radius, center_y - small_half_rect_height,

        center_x, center_y - small_half_rect_height
    };

    vg_lite_path_append(vg_lite_path, path_cmds, tmp_path_data, sizeof(path_cmds));
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

        lv_gpu_path_data_t path_data;

        if (draw_rect_type == RECT_TYPE_DEFAULT) {
            fill_rect_border_half(vg_lite_path, &path_data, x_vector, center_x,
                                  center_y, half_rect_width, half_rect_height,
                                  small_half_rect_width,
                                  small_half_rect_height);
        } else {
            fill_rounded_rect_border_half(
                vg_lite_path, &path_data, x_vector, center_x, center_y, radius,
                small_radius, half_rect_width, half_rect_height,
                small_half_rect_width, small_half_rect_height);
        }

        fill_path_clip_area(vg_lite_path, clip_area);

        vg_lite_draw(vg_buf, vg_lite_path, VG_LITE_FILL_NON_ZERO, &path_matrix,
                     blend, color);

        vg_lite_clear_path(vg_lite_path);

        free_float_path_data(&path_data);
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
                                        uint8_t* path_cmds,
                                        uint8_t path_cmds_size, float radius,
                                        float rect_width, float rect_height,
                                        float center_x, float center_y)
{
    const float offset_bezier_ctrl_p = BEZIER_CTRL_P_45_P_R * radius;
    const float offset_angle_45 = ANGLE_45_P_R * radius;

    float half_rect_width = rect_width / 2;
    float half_rect_height = rect_height / 2;

    float half_r_w_no_radius = half_rect_width - radius;
    float half_r_h_no_radius = half_rect_height - radius;

    float tmp_path_data[] = {
        center_x - half_r_w_no_radius, center_y - half_rect_height,

        center_x + half_r_w_no_radius, center_y - half_rect_height,

        center_x + half_r_w_no_radius + offset_bezier_ctrl_p, center_y - half_rect_height,
        center_x + half_r_w_no_radius + offset_angle_45, center_y - half_r_h_no_radius - offset_angle_45,

        center_x + half_rect_width, center_y - half_r_h_no_radius - offset_bezier_ctrl_p,
        center_x + half_rect_width, center_y - half_r_h_no_radius,

        center_x + half_rect_width, center_y + half_r_h_no_radius,

        center_x + half_rect_width, center_y + half_r_h_no_radius + offset_bezier_ctrl_p,
        center_x + half_r_w_no_radius + offset_angle_45, center_y + half_r_h_no_radius + offset_angle_45,

        center_x + half_r_w_no_radius + offset_bezier_ctrl_p, center_y + half_rect_height,
        center_x + half_r_w_no_radius, center_y + half_rect_height,

        center_x - half_r_w_no_radius, center_y + half_rect_height,

        center_x - half_r_w_no_radius - offset_bezier_ctrl_p, center_y + half_rect_height,
        center_x - half_r_w_no_radius - offset_angle_45, center_y + half_r_h_no_radius + offset_angle_45,

        center_x - half_rect_width, center_y + half_r_h_no_radius + offset_bezier_ctrl_p,
        center_x - half_rect_width, center_y + half_r_h_no_radius,

        center_x - half_rect_width, center_y - half_r_h_no_radius,

        center_x - half_rect_width, center_y - half_r_h_no_radius - offset_bezier_ctrl_p,
        center_x - half_r_w_no_radius - offset_angle_45, center_y - half_r_h_no_radius - offset_angle_45,

        center_x - half_r_w_no_radius - offset_bezier_ctrl_p, center_y - half_rect_height,
        center_x - half_r_w_no_radius, center_y - half_rect_height
    };

    vg_lite_path_append(vg_lite_path, path_cmds, tmp_path_data, path_cmds_size);
}

static void draw_bg_path(vg_lite_buffer_t* vg_buf, vg_lite_path_t* vg_lite_path,
                         rect_type draw_rect_type, const void* img_src,
                         const lv_area_t* coords, const lv_area_t* clip_area,
                         const lv_draw_rect_dsc_t* dsc, bool draw_border,
                         vg_lite_blend_t blend, lv_grad_dir_t grad_dir)
{
    if(dsc->bg_opa <= LV_OPA_MIN) return;

    lv_area_t draw_coords;
    lv_area_copy(&draw_coords, coords);

    lv_coord_t rect_width = draw_coords.x2 - draw_coords.x1 + 1;
    lv_coord_t rect_height = draw_coords.y2 - draw_coords.y1 + 1;

    lv_opa_t opa = dsc->bg_opa >= LV_OPA_MAX ? LV_OPA_COVER : dsc->bg_opa;

    vg_lite_matrix_t path_matrix;
    vg_lite_identity(&path_matrix);

    lv_color_t lv_bg_color = grad_dir == LV_GRAD_DIR_NONE ? dsc->bg_color : dsc->bg_grad.stops[0].color;

    uint32_t bg_color_argb8888 = lv_color_to32(lv_bg_color);
    vg_lite_color_t bg_color = get_vg_lite_color_lvgl_mix(bg_color_argb8888,
                                                          opa);

    lv_gpu_path_data_t path_data;

    if (draw_rect_type == RECT_TYPE_DEFAULT) {
        malloc_float_path_data(vg_lite_path, &path_data, default_rect_path_cmds,
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

        malloc_float_path_data(vg_lite_path, &path_data, rounded_rect_path_cmds,
                                     sizeof(rounded_rect_path_cmds));

        fill_rounded_rect_path_data(vg_lite_path, rounded_rect_path_cmds,
                                    sizeof(rounded_rect_path_cmds), draw_radius,
                                    rect_width, rect_height, center_x,
                                    center_y);
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

    vg_lite_clear_path(vg_lite_path);

    free_float_path_data(&path_data);
}

bool draw_rect_path(vg_lite_buffer_t* vg_buf, vg_lite_path_t* vg_lite_path,
                    const lv_area_t* coords, const lv_area_t* clip,
                    const lv_draw_rect_dsc_t* dsc, lv_grad_dir_t grad_dir)
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
                     &draw_clip, dsc, draw_border, vg_lite_blend, grad_dir);

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


LV_ATTRIBUTE_FAST_MEM lv_res_t lv_draw_rect_gpu(struct _lv_draw_ctx_t* draw_ctx,
    const lv_draw_rect_dsc_t* dsc,
    const lv_area_t* coords)
{
  if (dsc->radius <= 0) {
    return LV_RES_INV;
  }

  lv_coord_t area_height = lv_area_get_height(coords);
  lv_coord_t area_width = lv_area_get_width(coords);
  if (area_height < 1 || area_width < 1) {
    return LV_RES_OK;
  }

  lv_grad_dir_t grad_dir = dsc->bg_grad.dir;
  lv_color_t lv_bg_color = grad_dir == LV_GRAD_DIR_NONE ? dsc->bg_color : dsc->bg_grad.stops[0].color;
  if(lv_bg_color.full == dsc->bg_grad.stops[1].color.full) grad_dir = LV_GRAD_DIR_NONE;

  if (grad_dir == LV_GRAD_DIR_NONE) {
    if (area_height + area_width < 80) {
      return LV_RES_INV;
    }
  } else {
    if (area_height + area_width < 160) {
      return LV_RES_INV;
    }
  }

  bool draw_shadow = true;
  if (dsc->shadow_width == 0)
    draw_shadow = false;
  if (dsc->shadow_opa <= LV_OPA_MIN)
    draw_shadow = false;

  if (dsc->shadow_width == 1 && dsc->shadow_spread <= 0
      && dsc->shadow_ofs_x == 0 && dsc->shadow_ofs_y == 0) {
    draw_shadow = false;
  }
  // Not support temporary.
  if (draw_shadow == true)
    return LV_RES_INV;

  // Not support temporary.
  if (dsc->bg_img_src != NULL && dsc->bg_img_opa > LV_OPA_MIN)
    return LV_RES_INV;

  if (dsc->border_side != LV_BORDER_SIDE_NONE) {
    // Not support temporary.
    if (!(dsc->border_side & LV_BORDER_SIDE_LEFT)
        || !(dsc->border_side & LV_BORDER_SIDE_TOP)
        || !(dsc->border_side & LV_BORDER_SIDE_RIGHT)
        || !(dsc->border_side & LV_BORDER_SIDE_BOTTOM)) {
      return LV_RES_INV;
    }
  }

  // Not support temporary.
  if (lv_draw_mask_is_any(draw_ctx->clip_area) == true)
    return LV_RES_INV;

  vg_lite_buffer_t dst_vgbuf;
  size_t buf_size = init_vg_lite_buffer_use_lv_buffer(draw_ctx, &dst_vgbuf);

  vg_lite_path_t vg_lite_path;
  memset(&vg_lite_path, 0, sizeof(vg_lite_path_t));

  lv_area_t _coords, _clip_area;
  lv_area_copy(&_coords, coords);
  lv_area_copy(&_clip_area, draw_ctx->clip_area);

  lv_area_move(&_coords, -draw_ctx->buf_area->x1, -draw_ctx->buf_area->y1);
  lv_area_move(&_clip_area, -draw_ctx->buf_area->x1, -draw_ctx->buf_area->y1);

  if (draw_rect_path(&dst_vgbuf, &vg_lite_path, &_coords, &_clip_area,
          dsc, grad_dir)
      == false) {
    return LV_RES_OK;
  }

  vg_lite_finish();
  if (IS_CACHED(dst_vgbuf.memory)) {
    cpu_gpu_data_cache_invalid((uint32_t)dst_vgbuf.memory, buf_size);
  }

  LV_ASSERT_MEM_INTEGRITY();
  return LV_RES_OK;
}
