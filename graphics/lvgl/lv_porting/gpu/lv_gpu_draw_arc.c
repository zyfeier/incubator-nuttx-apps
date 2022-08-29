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

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "lv_gpu_draw_utils.h"
#include "lv_porting/lv_gpu_interface.h"
#include "vg_lite.h"
#include <math.h>

/****************************************************************************
 * Preprocessor Definitions
 ****************************************************************************/

#define ARC_MAX_PATH_LEN 89

/****************************************************************************
 * Private Data
 ****************************************************************************/

static float g_path[ARC_MAX_PATH_LEN];

/****************************************************************************
 * Global Functions
 ****************************************************************************/

/****************************************************************************
 * Name: lv_draw_arc_gpu
 *
 * Description:
 *   Draw an arc with GPU.
 *
 * Input Parameters:
 * @param draw_ctx draw context (refer to LVGL 8.2 changelog)
 * @param dsc draw arc description
 * @param radius arc radius
 * @param start_angle arc start angle in degrees
 * @param end_angle arc end angle in degrees
 *
 * Returned Value:
 * @return LV_RES_OK on success, LV_RES_INV on failure.
 *
 ****************************************************************************/
LV_ATTRIBUTE_FAST_MEM lv_res_t lv_draw_arc_gpu(
    lv_draw_ctx_t* draw_ctx,
    const lv_draw_arc_dsc_t* dsc,
    const lv_point_t* center,
    uint16_t radius,
    uint16_t start_angle,
    uint16_t end_angle)
{
  if (dsc->opa <= LV_OPA_MIN || dsc->width == 0 || start_angle == end_angle) {
    return LV_RES_OK;
  }
  return lv_draw_arc_f(draw_ctx, dsc, center, radius, start_angle, end_angle);
}

/****************************************************************************
 * Name: lv_draw_arc_f
 *
 * Description:
 *   Draw an arc with GPU with more precise parameters.
 *
 * Input Parameters:
 * @param draw_ctx draw context (refer to LVGL 8.2 changelog)
 * @param dsc draw arc description
 * @param radius arc radius
 * @param start_angle arc start angle in degrees
 * @param end_angle arc end angle in degrees
 *
 * Returned Value:
 * @return LV_RES_OK on success, LV_RES_INV on failure.
 *
 ****************************************************************************/
LV_ATTRIBUTE_FAST_MEM lv_res_t lv_draw_arc_f(
    lv_draw_ctx_t* draw_ctx,
    const lv_draw_arc_dsc_t* dsc,
    const lv_point_t* center,
    float radius,
    float start_angle,
    float end_angle)
{
  if (dsc->opa <= LV_OPA_MIN || dsc->width == 0) {
    return LV_RES_OK;
  }
  lv_area_t coords;
  lv_coord_t radius16 = (lv_coord_t)radius;
  coords.x1 = center->x - radius16;
  coords.y1 = center->y - radius16;
  /* -1 because the center already belongs to the left/bottom part */
  coords.x2 = center->x + radius16 - 1;
  coords.y2 = center->y + radius16 - 1;
  lv_area_t clip_area;
  if (_lv_area_intersect(&clip_area, &coords, draw_ctx->clip_area) == false) {
    return LV_RES_OK;
  }

  gpu_arc_dsc_t arc_dsc;
  lv_memcpy_small(&arc_dsc.dsc, dsc, sizeof(lv_draw_arc_dsc_t));
  arc_dsc.radius = radius;
  while (start_angle > 360.0f) {
    start_angle -= 360.0f;
  }
  while (start_angle < -ANGLE_RES) {
    start_angle += 360.0f;
  }
  while (end_angle > 360.0f) {
    end_angle -= 360.0f;
  }
  while (end_angle < start_angle - ANGLE_RES) {
    end_angle += 360.0f;
  }
  arc_dsc.start_angle = start_angle;
  arc_dsc.end_angle = end_angle;
  uint16_t path_length = gpu_fill_path(g_path, GPU_ARC_PATH, center, &arc_dsc);
  path_length *= sizeof(float);

  lv_gpu_curve_fill_t fill = {
    .color = dsc->color,
    .opa = dsc->opa,
    .type = fabs(end_angle - start_angle) < ANGLE_RES ? CURVE_FILL_RULE_EVENODD
                                                      : CURVE_FILL_RULE_NONZERO
  };
  lv_gpu_buffer_t gpu_buf = {
    .buf = draw_ctx->buf,
    .buf_area = draw_ctx->buf_area,
    .clip_area = &clip_area,
    .w = lv_area_get_width(draw_ctx->buf_area),
    .h = lv_area_get_height(draw_ctx->buf_area),
    .cf = BPP_TO_LV_FMT(LV_COLOR_DEPTH)
  };
  lv_gpu_image_dsc_t gpu_img_dsc;
  lv_img_dsc_t img_dsc;
  lv_draw_img_dsc_t draw_dsc = {
    .angle = 0,
    .zoom = LV_IMG_ZOOM_NONE,
    .recolor_opa = 0
  };
  gpu_img_dsc.coords = &coords;
  gpu_img_dsc.draw_dsc = &draw_dsc;
  _lv_img_cache_entry_t* cdsc = NULL;
  if (dsc->img_src) {
    lv_color32_t color = (lv_color32_t)lv_color_to32(dsc->color);
    LV_COLOR_SET_A32(color, 0);
    cdsc = _lv_img_cache_open(dsc->img_src, color, 0);
  }
  if (cdsc) {
    img_dsc.header = cdsc->dec_dsc.header;
    img_dsc.data = cdsc->dec_dsc.img_data;
    fill.type |= CURVE_FILL_IMAGE;
    fill.img = &gpu_img_dsc;
    gpu_img_dsc.img_dsc = &img_dsc;
  } else {
    fill.type |= CURVE_FILL_COLOR;
  }

  return gpu_draw_path(g_path, path_length, &fill, &gpu_buf);
}