/****************************************************************************
 * apps/graphics/lvgl/lv_porting/gpu/lv_gpu_draw.h
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

#ifndef __LV_GPU_DRAW_H__
#define __LV_GPU_DRAW_H__

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <lvgl/lvgl.h>
#include <nuttx/config.h>

#include <vg_lite.h>

#define DEC (M_PI / 180)

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

typedef struct {
    void* data;
    size_t data_size;
} lv_gpu_path_data_t;

vg_lite_color_t get_vg_lite_color_lvgl_mix(uint32_t color_argb888,
                                           lv_opa_t opa);

vg_lite_blend_t get_vg_lite_blend(lv_blend_mode_t blend_mode);

void malloc_float_path_data(vg_lite_path_t* vg_lite_path,
                            lv_gpu_path_data_t* path_data,
                            const uint8_t* path_cmds,
                            size_t cmds_size);

void free_float_path_data(lv_gpu_path_data_t* path_data);

void fill_path_clip_area(vg_lite_path_t* vg_lite_path,
                         const lv_area_t* clip_area);

bool simple_check_intersect_or_inclue(float center_x, float center_y,
                                      float width, float radius,
                                      const lv_area_t* clip_area);

void draw_circular_path(vg_lite_path_t* vg_lite_path, vg_lite_buffer_t* vg_buf,
                        float center_x, float center_y, float radius,
                        vg_lite_blend_t blend, vg_lite_color_t color,
                        const void* img_src, const lv_area_t* clip_area);

size_t init_vg_lite_buffer_use_lv_buffer(struct _lv_draw_ctx_t* draw_ctx,
                                         vg_lite_buffer_t* vg_buf);

void draw_arc_path(vg_lite_path_t* vg_lite_path, vg_lite_buffer_t* vg_buf,
                   float width, bool rounded, float start_angle,
                   float end_angle, float center_x, float center_y,
                   float radius, vg_lite_blend_t blend, vg_lite_color_t color,
                   const void* img_src, const lv_area_t* clip_area);

vg_lite_error_t vg_lite_update_grad_as_lvgl(vg_lite_linear_gradient_t* grad,
                                            uint8_t global_alpha);

bool draw_rect_path(vg_lite_buffer_t* vg_buf, vg_lite_path_t* vg_lite_path,
                    const lv_area_t* coords, const lv_area_t* clip,
                    const lv_draw_rect_dsc_t* dsc, lv_grad_dir_t grad_dir);

#endif /* __LV_GPU_DRAW_H__ */