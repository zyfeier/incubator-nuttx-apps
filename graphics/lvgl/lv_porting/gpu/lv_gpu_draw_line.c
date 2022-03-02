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

#include "../lvgl/src/misc/lv_color.h"
#include "gpu_port.h"
#include "../lv_gpu_interface.h"
#include "vg_lite.h"
#include <lvgl/src/lv_conf_internal.h>
#include <math.h>
#include <nuttx/cache.h>
#include <stdio.h>
#include <stdlib.h>

/****************************************************************************
 * Preprocessor Definitions
 ****************************************************************************/

#ifndef M_PI
#define M_PI 3.1415926535
#endif

/****************************************************************************
 * Macros
 ****************************************************************************/

/****************************************************************************
 * Private Data
 ****************************************************************************/

/****************************************************************************
 * Private Functions
 ****************************************************************************/
LV_ATTRIBUTE_FAST_MEM static inline void fill_line_path(int16_t* path,
    lv_coord_t length, lv_coord_t width)
{
  path[7] = path[10] = width + 1;
  path[5] = path[8] = length + 1;
}
/****************************************************************************
 * Global Functions
 ****************************************************************************/

/****************************************************************************
 * Name: lv_draw_line_gpu
 *
 * Description:
 *   Draw a skew line with GPU. (buggy)
 *
 * Input Parameters:
 * @param draw_ctx draw context (refer to LVGL 8.2 changelog)
 * @param dsc draw line description
 * @param point1 coordinate 1
 * @param point2 coordinate 2
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/
LV_ATTRIBUTE_FAST_MEM lv_res_t lv_draw_line_gpu(
    struct _lv_draw_ctx_t* draw_ctx,
    const lv_draw_line_dsc_t* dsc,
    const lv_point_t* point1,
    const lv_point_t* point2)
{
  if (point1->x == point2->x || point1->y == point2->y) {
    GPU_INFO("horizontal/vertical line skipped");
    return LV_RES_INV;
  }
  if (dsc->dash_gap || dsc->dash_width
      || dsc->opa || dsc->round_start || dsc->round_end) {
    GPU_INFO("unsupported feature");
    return LV_RES_INV;
  }
  const lv_area_t* disp_area = draw_ctx->buf_area;
  lv_color_t* disp_buf = draw_ctx->buf;
  int32_t disp_w = lv_area_get_width(disp_area);
  int32_t disp_h = lv_area_get_height(disp_area);
  lv_area_t blend_area;
  lv_coord_t w_2_min = dsc->width >> 1;
  lv_coord_t w_2_max = w_2_min | 1;
  blend_area.x1 = LV_MIN(point1->x, point2->x) - w_2_max;
  blend_area.x2 = LV_MAX(point1->x, point2->x) + w_2_max - 1;
  blend_area.y1 = LV_MIN(point1->y, point2->y) - w_2_max;
  blend_area.y2 = LV_MAX(point1->y, point2->y) + w_2_max - 1;
  if (lv_draw_mask_is_any(&blend_area)) {
    GPU_WARN("masked line skipped: ([%d,%d], [%d,%d])",
        point1->x, point1->y, point2->x, point2->y);
    return LV_RES_INV;
  }

  vg_lite_buffer_t dst_vgbuf;
  LV_ASSERT(init_vg_buf(&dst_vgbuf, disp_w, disp_h,
                disp_w * sizeof(lv_color_t), disp_buf, VGLITE_PX_FMT, false)
      == LV_RES_OK);

  lv_coord_t x_diff = point2->x - point1->x;
  lv_coord_t y_diff = point2->y - point1->y;
  lv_coord_t length = (lv_coord_t)sqrtf(x_diff * x_diff + y_diff * y_diff);
  static int16_t line_path[13] = {
    VLC_OP_MOVE, 0, 0,
    VLC_OP_LINE, 0, 0,
    VLC_OP_LINE, 0, 0,
    VLC_OP_LINE, 0, 0,
    VLC_OP_END
  };
  fill_line_path(line_path, length, dsc->width);

  vg_lite_path_t vpath;
  lv_memset_00(&vpath, sizeof(vg_lite_path_t));
  vg_lite_init_path(&vpath, VG_LITE_S16, VG_LITE_HIGH, sizeof(line_path),
      line_path, 0, 0, disp_w - 1, disp_h - 1);

  bool flat = LV_ABS(x_diff) > LV_ABS(y_diff) ? true : false;

  vg_lite_matrix_t matrix;
  vg_lite_identity(&matrix);
  if (flat) {
    vg_lite_translate(point1->x, point1->y + w_2_max, &matrix);
  } else if (y_diff > 0) {
    vg_lite_translate(point1->x - w_2_min, point1->y, &matrix);
  } else {
    vg_lite_translate(point1->x + w_2_max, point1->y, &matrix);
  }
  float angle = atan2f(y_diff, x_diff) * 180.0 / M_PI - 90.0f;
  vg_lite_rotate(angle, &matrix);
  GPU_INFO("len:%d w:%d ([%d,%d]->[%d,%d])angle:%.2f",
      length, dsc->width, point1->x, point1->y, point2->x, point2->y, angle);
  vg_lite_color_t color = BGRA_TO_RGBA(lv_color_to32(dsc->color));

  vg_lite_draw(&dst_vgbuf, &vpath, VG_LITE_FILL_NON_ZERO, &matrix,
      VG_LITE_BLEND_SRC_OVER, color);
  vg_lite_error_t vgerr = VG_LITE_SUCCESS;
  CHECK_ERROR(vg_lite_finish());
  if (IS_CACHED(dst_vgbuf.memory)) {
    up_invalidate_dcache((uintptr_t)dst_vgbuf.memory,
        (uintptr_t)dst_vgbuf.memory + dst_vgbuf.height * dst_vgbuf.stride);
  }
  return LV_RES_OK;
}