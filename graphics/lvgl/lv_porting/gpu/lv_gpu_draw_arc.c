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

#define ARC_MAX_NUM 25

/****************************************************************************
 * Macros
 ****************************************************************************/

#define P(x, y) ((lv_fpoint_t) { (x), (y) })
#define PR(p, r, c, s) P((p)->x + (r) * (c), (p)->y + (r) * (s))
#define P_OFS(p, dx, dy) P((p)->x + dx, (p)->y + dy)
#define SINF(deg) ((deg) == 90       ? 1  \
        : (deg) == 0 || (deg) == 180 ? 0  \
        : (deg) == 270               ? -1 \
                                     : sinf((deg)*M_PI / 180))
#define COSF(deg) ((deg) == 0         ? 1  \
        : (deg) == 90 || (deg) == 270 ? 0  \
        : (deg) == 180                ? -1 \
                                      : cosf((deg)*M_PI / 180))

/****************************************************************************
 * Private Data
 ****************************************************************************/

static lv_fpoint_t g_points[ARC_MAX_NUM];
static lv_gpu_curve_op_t g_op[ARC_MAX_NUM];

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static inline lv_fpoint_t get_rotated(const lv_point_t* center,
    uint16_t radius, float cos, float sin, uint8_t j)
{
  switch (j & 3) {
  case 3:
    return PR(center, radius, sin, -cos);
  case 2:
    return PR(center, radius, -cos, -sin);
  case 1:
    return PR(center, radius, -sin, cos);
  case 0:
  default:
    return PR(center, radius, cos, sin);
  }
}

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
  lv_area_t coords;
  coords.x1 = center->x - radius;
  coords.y1 = center->y - radius;
  coords.x2 = center->x + radius - 1; /*-1 because the center already belongs to the left/bottom part*/
  coords.y2 = center->y + radius - 1;
  lv_area_t clip_area;
  if (_lv_area_intersect(&clip_area, &coords, draw_ctx->clip_area) == false) {
    return LV_RES_OK;
  }

  lv_fpoint_t* points = g_points;
  lv_gpu_curve_op_t* op = g_op;
  lv_memset_00(g_op, sizeof(g_op));
  uint32_t num;

  end_angle %= 360;
  start_angle %= 360;
  if (end_angle == start_angle) {
    op[0] = CURVE_ARC_90;
    points[0] = P(center->x + radius, center->y);
    points[1] = P(center->x, center->y);
    op[2] = CURVE_ARC_90;
    points[2] = P(center->x, center->y + radius);
    points[3] = points[1];
    op[4] = CURVE_ARC_90;
    points[4] = P(center->x - radius, center->y);
    points[5] = points[1];
    op[6] = CURVE_ARC_90;
    points[6] = P(center->x, center->y - radius);
    points[7] = points[1];
    op[8] = CURVE_CLOSE;
    points[8] = points[0];
    num = 8;
    if (dsc->width < radius) {
      lv_coord_t inner_radius = radius - dsc->width;
      op[9] = CURVE_ARC_90;
      points[9] = P(center->x + inner_radius, center->y);
      points[10] = points[1];
      op[11] = CURVE_ARC_90;
      points[11] = P(center->x, center->y + inner_radius);
      points[12] = points[1];
      op[13] = CURVE_ARC_90;
      points[13] = P(center->x - inner_radius, center->y);
      points[14] = points[1];
      op[15] = CURVE_ARC_90;
      points[15] = P(center->x, center->y - inner_radius);
      points[16] = points[1];
      op[17] = CURVE_CLOSE;
      points[17] = points[9];
      num = 18;
    }
  } else {
    if (end_angle < start_angle) {
      end_angle += 360;
    }

    float st_sin = SINF(start_angle);
    float st_cos = COSF(start_angle);
    float ed_sin = SINF(end_angle);
    float ed_cos = COSF(end_angle);
    float width = LV_MIN(dsc->width, radius);
    points[0] = PR(center, radius - width, st_cos, st_sin);
    op[0] = CURVE_LINE;
    lv_coord_t i = 1;
    if (dsc->rounded) {
      op[i - 1] = CURVE_ARC_90;
      points[i++] = PR(center, radius - width / 2, st_cos, st_sin);
      lv_fpoint_t* mid = &points[i - 1];
      op[i] = CURVE_ARC_90;
      points[i++] = P_OFS(mid, width / 2 * st_sin, -width / 2 * st_cos);
      points[i++] = *mid;
    }
    int16_t angle = end_angle - start_angle;
    uint8_t j = 0;
    while (angle > 0) {
      op[i] = angle < 90 ? CURVE_ARC_ACUTE : CURVE_ARC_90;
      points[i++] = get_rotated(center, radius, st_cos, st_sin, j++);
      points[i++] = P(center->x, center->y);
      angle -= 90;
    }
    op[i] = CURVE_LINE;
    points[i++] = PR(center, radius, ed_cos, ed_sin);
    if (dsc->rounded) {
      op[i - 1] = CURVE_ARC_90;
      points[i++] = PR(center, radius - width / 2, ed_cos, ed_sin);
      lv_fpoint_t* mid = &points[i - 1];
      op[i] = CURVE_ARC_90;
      points[i++] = P_OFS(mid, -width / 2 * ed_sin, width / 2 * ed_cos);
      points[i++] = *mid;
    }
    if (dsc->width < radius) {
      angle = end_angle - start_angle;
      op[i] = angle < 90 ? CURVE_ARC_ACUTE : CURVE_ARC_90;
      j = 4;
      while (angle > 0) {
        op[i] = angle < 90 ? CURVE_ARC_ACUTE : CURVE_ARC_90;
        points[i++] = get_rotated(center, radius - width, ed_cos, ed_sin, j--);
        points[i++] = P(center->x, center->y);
        angle -= 90;
      }
    }
    op[i] = CURVE_CLOSE;
    points[i++] = PR(center, radius - width, st_cos, st_sin);
    num = i;
  }

  lv_gpu_curve_fill_t fill = {
    .color = dsc->color,
    .opa = dsc->opa,
    .type = CURVE_FILL_RULE_EVENODD
  };
  lv_gpu_curve_t curve = {
    .fpoints = points,
    .num = num,
    .op = op,
    .fill = &fill
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
  gpu_img_dsc.coords = &coords;
  gpu_img_dsc.draw_dsc = NULL;
  _lv_img_cache_entry_t* cdsc = NULL;
  if (dsc->img_src) {
    cdsc = _lv_img_cache_open(dsc->img_src, dsc->color, 0);
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
  gpu_draw_curve(&curve, &gpu_buf);
  return LV_RES_OK;
}