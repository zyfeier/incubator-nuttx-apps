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

bool simple_check_intersect_or_inclue(float center_x, float center_y,
                                      float width, float radius,
                                      const lv_area_t* clip_area)
{
    if (clip_area->x1 <= center_x - radius && clip_area->y1 <= center_y - radius
        && clip_area->x2 >= center_x + radius
        && clip_area->y2 >= center_y + radius) {
        return true;
    }

    if (clip_area->x1 < center_x - radius
        && clip_area->x2 < center_x - radius) {
        return false;
    }

    if (clip_area->x1 > center_x + radius
        && clip_area->x2 > center_x + radius) {
        return false;
    }

    if (clip_area->y1 < center_y - radius
        && clip_area->y2 < center_y - radius) {
        return false;
    }

    if (clip_area->y1 > center_y + radius
        && clip_area->y2 > center_y + radius) {
        return false;
    }

    float diff_x1 = center_x - clip_area->x1;
    float diff_x2 = center_x - clip_area->x2;
    float diff_y1 = center_y - clip_area->y1;
    float diff_y2 = center_y - clip_area->y2;

    float distance_1 = sqrt((diff_x1 * diff_x1) + (diff_y1 * diff_y1));
    float distance_2 = sqrt((diff_x1 * diff_x1) + (diff_y2 * diff_y2));
    float distance_3 = sqrt((diff_x2 * diff_x2) + (diff_y1 * diff_y1));
    float distance_4 = sqrt((diff_x2 * diff_x2) + (diff_y2 * diff_y2));

    float small_radius = radius - width;
    if (distance_1 < small_radius && distance_2 < small_radius
        && distance_3 < small_radius && distance_4 < small_radius) {
        return false;
    }
    return true;
}

static void draw_arc_float_path(vg_lite_path_t* vg_lite_path,
                                vg_lite_buffer_t* vg_buf, vg_lite_blend_t blend,
                                vg_lite_color_t color, const void* img_src,
                                const lv_area_t* clip_area)
{
    fill_path_clip_area(vg_lite_path, clip_area);

    vg_lite_matrix_t path_matrix;
    vg_lite_identity(&path_matrix);

    if (img_src) {
        vg_lite_buffer_t img_buf;
        //TODO: fill image info to vg_lite_buffer_t
        // init_vg_buf(&img_buf, img_w, img_h, img_stride, img_buf_data,
        //             VG_LITE_BGRA8888, 1);
        vg_lite_matrix_t img_matrix;
        vg_lite_identity(&img_matrix);
        vg_lite_draw_pattern(vg_buf, vg_lite_path, VG_LITE_FILL_NON_ZERO,
                             &path_matrix, &img_buf, &img_matrix, blend,
                             VG_LITE_PATTERN_COLOR, color,
                             VG_LITE_FILTER_BI_LINEAR);
    } else {
        vg_lite_draw(vg_buf, vg_lite_path, VG_LITE_FILL_NON_ZERO, &path_matrix,
                     blend, color);
    }
}

void draw_circular_path(vg_lite_path_t* vg_lite_path, vg_lite_buffer_t* vg_buf,
                        float center_x, float center_y, float radius,
                        vg_lite_blend_t blend, vg_lite_color_t color,
                        const void* img_src, const lv_area_t* clip_area)
{
    const uint8_t circular_path_cmds[] = {
        VLC_OP_MOVE,
        VLC_OP_LCWARC,
        VLC_OP_LCWARC,
        VLC_OP_END
    };

    lv_gpu_path_data_t path_data;
    malloc_float_path_data(vg_lite_path, &path_data, circular_path_cmds,
                           sizeof(circular_path_cmds));

    float tmp_path_data[] = {
        center_x, center_y - radius,
        radius, radius, 0, center_x, center_y + radius,
        radius, radius, 0, center_x, center_y - radius
    };

    lv_area_t draw_clip;
    lv_area_copy(&draw_clip, clip_area);
    draw_clip.x2 += 1;
    draw_clip.y2 += 1;

    vg_lite_path_append(vg_lite_path, circular_path_cmds, tmp_path_data,
                        sizeof(circular_path_cmds));

    draw_arc_float_path(vg_lite_path, vg_buf, blend, color, img_src, clip_area);

    vg_lite_clear_path(vg_lite_path);

    free_float_path_data(&path_data);
}

static void compute_x_y_by_angle(float* x, float* y, float angle,
                                 float center_x, float center_y, float radius)
{
    if (angle == 0 || angle == 360) {
        *x = center_x + radius;
        *y = center_y;
    } else if (angle > 0 && angle < 90) {
        double v_sin = sin(angle * DEC);
        double v_cos = cos(angle * DEC);
        *x = radius * v_cos + center_x;
        *y = radius * v_sin + center_y;
    } else if (angle == 90) {
        *x = center_x;
        *y = center_y + radius;
    } else if (angle > 90 && angle < 180) {
        double v_sin = sin((angle - 90) * DEC);
        double v_cos = cos((angle - 90) * DEC);
        *x = center_x - (radius * v_sin);
        *y = radius * v_cos + center_y;
    } else if (angle == 180) {
        *x = center_x - radius;
        *y = center_y;
    } else if (angle > 180 && angle < 270) {
        double v_sin = sin((angle - 180) * DEC);
        double v_cos = cos((angle - 180) * DEC);
        *x = center_x - (radius * v_cos);
        *y = center_y - (radius * v_sin);
    } else if (angle == 270) {
        *x = center_x;
        *y = center_y - radius;
    } else if (angle > 270 && angle < 360) {
        double v_sin = sin((angle - 270) * DEC);
        double v_cos = cos((angle - 270) * DEC);
        *x = radius * v_sin + center_x;
        *y = center_y - (radius * v_cos);
    }
}

static void fill_arc_path_by_angle(vg_lite_path_t* vg_lite_path,
                                   uint8_t* arc_path_cmds,
                                   uint8_t path_cmds_size, float width,
                                   bool rounded, float start_angle,
                                   float end_angle, float center_x,
                                   float center_y, float radius)
{
    float inner_radius = radius - width;
    float rounded_radius = 0;

    if (rounded == true) {
        rounded_radius = width / 2;
    }

    float px = 0, py = 0;

    compute_x_y_by_angle(&px, &py, start_angle, center_x, center_y, radius);

    float start_px = px;
    float start_py = py;

    float tmp_path_data[24];

    uint8_t index = 0;
    tmp_path_data[index] = px;
    tmp_path_data[++index] = py;

    compute_x_y_by_angle(&px, &py, end_angle, center_x, center_y, radius);

    tmp_path_data[++index] = radius;
    tmp_path_data[++index] = radius;
    tmp_path_data[++index] = 0;
    tmp_path_data[++index] = px;
    tmp_path_data[++index] = py;

    compute_x_y_by_angle(&px, &py, end_angle, center_x, center_y, inner_radius);

    if (rounded == true) {
        tmp_path_data[++index] = rounded_radius;
        tmp_path_data[++index] = rounded_radius;
        tmp_path_data[++index] = 0;
        tmp_path_data[++index] = px;
        tmp_path_data[++index] = py;
    } else {
        tmp_path_data[++index] = px;
        tmp_path_data[++index] = py;
    }

    compute_x_y_by_angle(&px, &py, start_angle, center_x, center_y,
                         inner_radius);

    tmp_path_data[++index] = inner_radius;
    tmp_path_data[++index] = inner_radius;
    tmp_path_data[++index] = 0;
    tmp_path_data[++index] = px;
    tmp_path_data[++index] = py;

    px = start_px;
    py = start_py;

    if (rounded == true) {
        tmp_path_data[++index] = rounded_radius;
        tmp_path_data[++index] = rounded_radius;
        tmp_path_data[++index] = 0;
        tmp_path_data[++index] = px;
        tmp_path_data[++index] = py;
    } else {
        tmp_path_data[++index] = px;
        tmp_path_data[++index] = py;
    }

    vg_lite_path_append(vg_lite_path, arc_path_cmds, tmp_path_data,
                        path_cmds_size);
}

void draw_arc_path(vg_lite_path_t* vg_lite_path, vg_lite_buffer_t* vg_buf,
                   float width, bool rounded, float start_angle,
                   float end_angle, float center_x, float center_y,
                   float radius, vg_lite_blend_t blend, vg_lite_color_t color,
                   const void* img_src, const lv_area_t* clip_area)
{
    uint8_t arc_path_cmds[] = {
        VLC_OP_MOVE,
        VLC_OP_SCWARC,
        VLC_OP_LCWARC,
        VLC_OP_SCCWARC,
        VLC_OP_LCWARC,
        VLC_OP_END
    };

    if (rounded == false) {
        arc_path_cmds[2] = VLC_OP_LINE;
        arc_path_cmds[4] = VLC_OP_LINE;
    }

    if (start_angle > end_angle) {
        if (360 - start_angle + end_angle >= 180) {
            arc_path_cmds[1] = VLC_OP_LCWARC;
            arc_path_cmds[3] = VLC_OP_LCCWARC;
        }
    } else {
        if (end_angle - start_angle >= 180) {
            arc_path_cmds[1] = VLC_OP_LCWARC;
            arc_path_cmds[3] = VLC_OP_LCCWARC;
        }
    }

    lv_gpu_path_data_t path_data;
    malloc_float_path_data(vg_lite_path, &path_data, arc_path_cmds,
                           sizeof(arc_path_cmds));

    fill_arc_path_by_angle(vg_lite_path, arc_path_cmds, sizeof(arc_path_cmds),
                           width, rounded, start_angle, end_angle, center_x,
                           center_y, radius);

    lv_area_t draw_clip;
    lv_area_copy(&draw_clip, clip_area);
    draw_clip.x2 += 1;
    draw_clip.y2 += 1;

    draw_arc_float_path(vg_lite_path, vg_buf, blend, color, img_src,
                        &draw_clip);

    vg_lite_clear_path(vg_lite_path);

    free_float_path_data(&path_data);
}

LV_ATTRIBUTE_FAST_MEM lv_res_t lv_draw_arc_gpu(struct _lv_draw_ctx_t* draw_ctx,
    const lv_draw_arc_dsc_t* dsc,
    const lv_point_t* center,
    uint16_t radius,
    uint16_t start_angle,
    uint16_t end_angle)
{
#if LV_DRAW_COMPLEX
  if (dsc->opa <= LV_OPA_MIN)
    return LV_RES_INV;
  if (dsc->width == 0)
    return LV_RES_INV;
  if (start_angle == end_angle)
    return LV_RES_INV;

  // Not support temporary.
  if (dsc->img_src != NULL)
    return LV_RES_INV;
  // Not support temporary.
  if (lv_draw_mask_is_any(draw_ctx->clip_area) == true)
    return LV_RES_INV;

  lv_coord_t width = dsc->width;
  if (width > radius)
    width = radius;

  if (simple_check_intersect_or_inclue(center->x, center->y, width, radius,
          draw_ctx->clip_area)
      == false) {
    return LV_RES_OK;
  }

  vg_lite_path_t vg_lite_path;
  memset(&vg_lite_path, 0, sizeof(vg_lite_path_t));

  uint32_t color_argb888 = lv_color_to32(dsc->color);
  vg_lite_color_t color = get_vg_lite_color_lvgl_mix(color_argb888, dsc->opa);

  vg_lite_blend_t vg_lite_blend = get_vg_lite_blend(dsc->blend_mode);

  vg_lite_buffer_t dst_vgbuf;
  size_t buf_size = init_vg_lite_buffer_use_lv_buffer(draw_ctx, &dst_vgbuf);

  /*Draw two semicircle*/
  if (start_angle + 360 == end_angle || start_angle == end_angle + 360) {
    draw_arc_path(&vg_lite_path, &dst_vgbuf, width, false, 0, 180,
        center->x, center->y, radius, vg_lite_blend, color,
        dsc->img_src, draw_ctx->clip_area);

    draw_arc_path(&vg_lite_path, &dst_vgbuf, width, false, 180, 360,
        center->x, center->y, radius, vg_lite_blend, color,
        dsc->img_src, draw_ctx->clip_area);

    vg_lite_finish();
    if (IS_CACHED(dst_vgbuf.memory)) {
      cpu_gpu_data_cache_invalid((uint32_t)dst_vgbuf.memory, buf_size);
    }
    return LV_RES_OK;
  }

  while (start_angle >= 360)
    start_angle -= 360;
  while (end_angle >= 360)
    end_angle -= 360;

  draw_arc_path(&vg_lite_path, &dst_vgbuf, width, dsc->rounded, start_angle,
      end_angle, center->x, center->y, radius, vg_lite_blend, color,
      dsc->img_src, draw_ctx->clip_area);

  vg_lite_finish();
  if (IS_CACHED(dst_vgbuf.memory)) {
    cpu_gpu_data_cache_invalid((uint32_t)dst_vgbuf.memory, buf_size);
  }
#else
  LV_LOG_WARN("Can't draw arc with LV_DRAW_COMPLEX == 0");
  LV_UNUSED(center_x);
  LV_UNUSED(center_y);
  LV_UNUSED(radius);
  LV_UNUSED(start_angle);
  LV_UNUSED(end_angle);
  LV_UNUSED(clip_area);
  LV_UNUSED(dsc);
#endif /*LV_DRAW_COMPLEX*/
  return LV_RES_OK;
}