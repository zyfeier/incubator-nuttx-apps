/****************************************************************************
 * apps/graphics/lvgl/lv_porting/gpu/lv_gpu_draw_line.c
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

#include "lv_color.h"
#include "lv_gpu_draw_utils.h"
#include "lv_porting/lv_gpu_interface.h"
#include "src/lv_conf_internal.h"
#include "vg_lite.h"
#include <math.h>
#include <nuttx/cache.h>
#include <stdio.h>
#include <stdlib.h>

/****************************************************************************
 * Preprocessor Definitions
 ****************************************************************************/

#define PLAIN_LINE_NUM 10

/****************************************************************************
 * Macros
 ****************************************************************************/

#define P(p) ((lv_fpoint_t) { (p)->x, (p)->y })
#define PR(p) ((lv_fpoint_t) { (p)->x + w2_dx, (p)->y - w2_dy })
#define PL(p) ((lv_fpoint_t) { (p)->x - w2_dx, (p)->y + w2_dy })
#define PB(p) ((lv_fpoint_t) { (p)->x - w2_dy, (p)->y - w2_dx })
#define PT(p) ((lv_fpoint_t) { (p)->x + w2_dy, (p)->y + w2_dx })

/****************************************************************************
 * Private Data
 ****************************************************************************/

static lv_fpoint_t g_points[PLAIN_LINE_NUM];
static lv_gpu_curve_op_t g_op[PLAIN_LINE_NUM];

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Global Functions
 ****************************************************************************/

/****************************************************************************
 * Name: lv_draw_line_gpu
 *
 * Description:
 *   Draw a skew line with GPU.
 *
 * Input Parameters:
 * @param draw_ctx draw context (refer to LVGL 8.2 changelog)
 * @param dsc draw line description
 * @param point1 coordinate 1
 * @param point2 coordinate 2
 *
 * Returned Value:
 * @return LV_RES_OK on success, LV_RES_INV on failure.
 *
 ****************************************************************************/
LV_ATTRIBUTE_FAST_MEM lv_res_t lv_draw_line_gpu(
    lv_draw_ctx_t* draw_ctx,
    const lv_draw_line_dsc_t* dsc,
    const lv_point_t* point1,
    const lv_point_t* point2)
{
  if (point1->x == point2->x || point1->y == point2->y) {
    GPU_INFO("horizontal/vertical line skipped");
    return LV_RES_INV;
  }
  const lv_area_t* disp_area = draw_ctx->buf_area;
  lv_coord_t w = dsc->width;
  lv_coord_t dash_width = dsc->dash_width;
  lv_coord_t dash_gap = dsc->dash_gap;
  lv_coord_t dash_l = dash_width + dash_gap;

  float dx = point2->x - point1->x;
  float dy = point2->y - point1->y;
  float dl = sqrtf(dx * dx + dy * dy);
  float w_dx = w * dy / dl;
  float w_dy = w * dx / dl;
  float w2_dx = w_dx / 2;
  float w2_dy = w_dy / 2;
  lv_coord_t num = 4;
  if (dsc->round_end)
    num += 3;
  if (dsc->round_start)
    num += 3;
  lv_coord_t ndash = 0;
  if (dash_width && dash_l < dl) {
    ndash = (dl + dash_l - 1) / dash_l;
    num += ndash * 4;
  }
  lv_fpoint_t* points = g_points;
  lv_gpu_curve_op_t* op = g_op;
  if (num > PLAIN_LINE_NUM) {
    points = lv_mem_alloc(sizeof(lv_fpoint_t) * num);
    op = lv_mem_alloc(sizeof(lv_gpu_curve_op_t) * num);
  }
  points[0] = PR(point1);
  op[0] = CURVE_LINE;
  lv_coord_t i = 1;
  if (dsc->round_start) {
    op[0] = CURVE_ARC_90;
    op[i] = CURVE_NOP;
    points[i++] = P(point1);
    op[i] = CURVE_ARC_90;
    points[i++] = PB(point1);
    op[i] = CURVE_NOP;
    points[i++] = P(point1);
  }
  op[i] = CURVE_LINE;
  points[i++] = PL(point1);
  for (int_fast16_t j = 0; j < ndash; j++) {
    op[i] = CURVE_LINE;
    points[i++] = (lv_fpoint_t) {
      point1->x - w2_dx + dx * (j * dash_l + dash_width) / dl,
      point1->y + w2_dy + dy * (j * dash_l + dash_width) / dl
    };
    op[i] = CURVE_CLOSE;
    points[i++] = (lv_fpoint_t) {
      point1->x + w2_dx + dx * (j * dash_l + dash_width) / dl,
      point1->y - w2_dy + dy * (j * dash_l + dash_width) / dl
    };
    op[i] = CURVE_LINE;
    points[i++] = (lv_fpoint_t) {
      point1->x + w2_dx + dx * (j + 1) * dash_l / dl,
      point1->y - w2_dy + dy * (j + 1) * dash_l / dl
    };
    op[i] = CURVE_LINE;
    points[i++] = (lv_fpoint_t) {
      point1->x - w2_dx + dx * (j + 1) * dash_l / dl,
      point1->y + w2_dy + dy * (j + 1) * dash_l / dl
    };
  }
  op[i] = CURVE_LINE;
  points[i++] = PL(point2);
  if (dsc->round_end) {
    op[i - 1] = CURVE_ARC_90;
    op[i] = CURVE_NOP;
    points[i++] = P(point2);
    op[i] = CURVE_ARC_90;
    points[i++] = PT(point2);
    op[i] = CURVE_NOP;
    points[i++] = P(point2);
  }
  op[i] = CURVE_CLOSE;
  points[i] = PR(point2);

  lv_gpu_curve_fill_t fill = {
    .color = dsc->color,
    .opa = dsc->opa,
    .type = CURVE_FILL_COLOR
  };
  lv_gpu_curve_t curve = {
    .fpoints = points,
    .num = num,
    .op = op,
    .fill = &fill
  };
  lv_gpu_buffer_t gpu_buf = {
    .buf = draw_ctx->buf,
    .clip_area = (lv_area_t*)draw_ctx->clip_area,
    .w = lv_area_get_width(draw_ctx->buf_area),
    .h = lv_area_get_height(draw_ctx->buf_area),
    .cf = BPP_TO_LV_FMT(LV_COLOR_DEPTH)
  };
  gpu_draw_curve(&curve, &gpu_buf);
  return LV_RES_OK;
}