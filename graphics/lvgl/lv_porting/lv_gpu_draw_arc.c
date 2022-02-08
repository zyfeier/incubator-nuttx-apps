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
                                vg_lite_buffer_t* vg_buf, int32_t data_size,
                                vg_lite_blend_t blend, vg_lite_color_t color,
                                const void* img_src, const lv_area_t* clip_area)
{
    vg_lite_init_arc_path(vg_lite_path, VG_LITE_FP32, VG_LITE_HIGH, data_size,
                          vg_lite_path->path, 0, 0, 0, 0);

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
    static uint8_t circular_path_cmds[] = {
        VLC_OP_MOVE,
        VLC_OP_LCCWARC,
        VLC_OP_LCCWARC,
        VLC_OP_END
    };

    int32_t data_size = malloc_float_path_data(vg_lite_path, circular_path_cmds,
                                               sizeof(circular_path_cmds));

    char* pchar;
    float* pfloat;

    pchar = (char*)vg_lite_path->path;
    pfloat = (float*)vg_lite_path->path;
    *pchar = circular_path_cmds[0];
    pfloat++;
    *pfloat++ = center_x;
    *pfloat++ = center_y - radius;

    pchar = (char*)pfloat;
    *pchar = circular_path_cmds[1];
    pfloat++;
    *pfloat++ = radius;
    *pfloat++ = radius;
    *pfloat++ = 0;
    *pfloat++ = center_x;
    *pfloat++ = center_y + radius;

    pchar = (char*)pfloat;
    *pchar = circular_path_cmds[2];
    pfloat++;
    *pfloat++ = radius;
    *pfloat++ = radius;
    *pfloat++ = 0;
    *pfloat++ = center_x;
    *pfloat++ = center_y - radius;

    pchar = (char*)pfloat;
    *pchar = 0;

    draw_arc_float_path(vg_lite_path, vg_buf, data_size, blend, color, img_src,
                        clip_area);

    free(vg_lite_path->path);
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
                                   uint8_t* arc_path_cmds, float width,
                                   bool rounded, float start_angle,
                                   float end_angle, float center_x,
                                   float center_y, float radius)
{
    float inner_radius = radius - width;
    float rounded_radius = 0;

    if (rounded == true) {
        rounded_radius = width / 2;
    }

    float px = 0, py = 0, radius_h = 0, radius_v = 0;

    compute_x_y_by_angle(&px, &py, start_angle, center_x, center_y, radius);

    float start_px = px;
    float start_py = py;

    char* pchar;
    float* pfloat;

    pchar = (char*)vg_lite_path->path;
    pfloat = (float*)vg_lite_path->path;
    *pchar = arc_path_cmds[0];
    pfloat++;
    *pfloat++ = px;
    *pfloat++ = py;

    compute_x_y_by_angle(&px, &py, end_angle, center_x, center_y, radius);

    radius_h = radius;
    radius_v = radius;

    pchar = (char*)pfloat;
    *pchar = arc_path_cmds[1];
    pfloat++;
    *pfloat++ = radius_h;
    *pfloat++ = radius_v;
    *pfloat++ = 0;
    *pfloat++ = px;
    *pfloat++ = py;

    compute_x_y_by_angle(&px, &py, end_angle, center_x, center_y, inner_radius);

    radius_h = rounded_radius;
    radius_v = rounded_radius;

    if (rounded == true) {
        pchar = (char*)pfloat;
        *pchar = arc_path_cmds[2];
        pfloat++;
        *pfloat++ = radius_h;
        *pfloat++ = radius_v;
        *pfloat++ = 0;
        *pfloat++ = px;
        *pfloat++ = py;
    } else {
        pchar = (char*)pfloat;
        *pchar = arc_path_cmds[2];
        pfloat++;
        *pfloat++ = px;
        *pfloat++ = py;
    }

    compute_x_y_by_angle(&px, &py, start_angle, center_x, center_y,
                         inner_radius);

    radius_h = inner_radius;
    radius_v = inner_radius;

    pchar = (char*)pfloat;
    *pchar = arc_path_cmds[3];
    pfloat++;
    *pfloat++ = radius_h;
    *pfloat++ = radius_v;
    *pfloat++ = 0;
    *pfloat++ = px;
    *pfloat++ = py;

    px = start_px;
    py = start_py;
    radius_h = rounded_radius;
    radius_v = rounded_radius;

    if (rounded == true) {
        pchar = (char*)pfloat;
        *pchar = arc_path_cmds[4];
        pfloat++;
        *pfloat++ = radius_h;
        *pfloat++ = radius_v;
        *pfloat++ = 0;
        *pfloat++ = px;
        *pfloat++ = py;
    } else {
        pchar = (char*)pfloat;
        *pchar = arc_path_cmds[4];
        pfloat++;
        *pfloat++ = px;
        *pfloat++ = py;
    }

    pchar = (char*)pfloat;
    *pchar = 0;
}

void draw_arc_path(vg_lite_path_t* vg_lite_path, vg_lite_buffer_t* vg_buf,
                   float width, bool rounded, float start_angle,
                   float end_angle, float center_x, float center_y,
                   float radius, vg_lite_blend_t blend, vg_lite_color_t color,
                   const void* img_src, const lv_area_t* clip_area)
{
    uint8_t arc_path_cmds[] = {
        VLC_OP_MOVE,
        VLC_OP_SCCWARC,
        VLC_OP_LCCWARC,
        VLC_OP_SCWARC,
        VLC_OP_LCCWARC,
        VLC_OP_END
    };

    if (rounded == false) {
        arc_path_cmds[2] = VLC_OP_LINE;
        arc_path_cmds[4] = VLC_OP_LINE;
    }

    if (start_angle > end_angle) {
        if (360 - start_angle + end_angle >= 180) {
            arc_path_cmds[1] = VLC_OP_LCCWARC;
            arc_path_cmds[3] = VLC_OP_LCWARC;
        }
    } else {
        if (end_angle - start_angle >= 180) {
            arc_path_cmds[1] = VLC_OP_LCCWARC;
            arc_path_cmds[3] = VLC_OP_LCWARC;
        }
    }

    int32_t data_size = malloc_float_path_data(vg_lite_path, arc_path_cmds,
                                               sizeof(arc_path_cmds));

    fill_arc_path_by_angle(vg_lite_path, arc_path_cmds, width, rounded,
                           start_angle, end_angle, center_x, center_y, radius);

    lv_area_t draw_clip;
    lv_area_copy(&draw_clip, clip_area);
    draw_clip.x2 += 1;
    draw_clip.y2 += 1;

    draw_arc_float_path(vg_lite_path, vg_buf, data_size, blend, color, img_src,
                        &draw_clip);

    free(vg_lite_path->path);
}