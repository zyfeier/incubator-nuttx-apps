/****************************************************************************
 * apps/graphics/lvgl/lv_porting/gpu/lv_gpu_draw_polygon.c
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
#include "lv_porting/lv_gpu_interface.h"
#include "vg_lite.h"
#include <nuttx/cache.h>

/****************************************************************************
 * Preprocessor Definitions
 ****************************************************************************/

/****************************************************************************
 * Macros
 ****************************************************************************/

/****************************************************************************
 * Private Data
 ****************************************************************************/

static uint32_t grad_mem[VLC_GRADBUFFER_WIDTH];

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Global Functions
 ****************************************************************************/

/****************************************************************************
 * Name: lv_draw_polygon_gpu
 *
 * Description:
 *   Draw a polygon with GPU
 *
 * Input Parameters:
 * @param draw_ctx draw context (refer to LVGL 8.2 changelog)
 * @param dsc draw rectangle description
 * @param points array of vertices' coordinates
 * @param point_cnt number of vertices
 *
 * Returned Value:
 * @return LV_RES_OK on success, LV_RES_INV on failure.
 *
 ****************************************************************************/
LV_ATTRIBUTE_FAST_MEM lv_res_t lv_draw_polygon_gpu(
    lv_draw_ctx_t* draw_ctx, const lv_draw_rect_dsc_t* dsc,
    const lv_point_t* points, uint16_t point_cnt)
{
  /* Init destination vglite buffer */
  const lv_area_t* disp_area = draw_ctx->buf_area;
  lv_color_t* disp_buf = draw_ctx->buf;
  int32_t disp_w = lv_area_get_width(disp_area);
  int32_t disp_h = lv_area_get_height(disp_area);
  vg_lite_buffer_t dst_vgbuf;
  LV_ASSERT(init_vg_buf(&dst_vgbuf, disp_w, disp_h,
                disp_w * sizeof(lv_color_t), disp_buf, VGLITE_PX_FMT, false)
      == LV_RES_OK);
  /* Convert to vglite path */
  uint32_t path_size = (point_cnt * 3 + 1) * sizeof(int16_t);
  int16_t* poly_path = lv_mem_buf_get(path_size);
  if (!poly_path) {
    GPU_WARN("out of memory");
    return LV_RES_INV;
  }
  lv_area_t poly_coords = { points->x, points->y, points->x, points->y };
  poly_path[0] = VLC_OP_MOVE;
  poly_path[1] = points->x;
  poly_path[2] = points->y;
  for (int_fast16_t i = 1; i < point_cnt; i++) {
    poly_path[i * 3] = VLC_OP_LINE;
    poly_path[i * 3 + 1] = points[i].x;
    poly_path[i * 3 + 2] = points[i].y;
    poly_coords.x1 = LV_MIN(poly_coords.x1, points[i].x);
    poly_coords.y1 = LV_MIN(poly_coords.y1, points[i].y);
    poly_coords.x2 = LV_MAX(poly_coords.x2, points[i].x);
    poly_coords.y2 = LV_MAX(poly_coords.y2, points[i].y);
  }
  poly_path[point_cnt * 3] = VLC_OP_END;
  /* If no intersection then draw complete */
  bool is_common;
  lv_area_t clip_area;
  is_common = _lv_area_intersect(&clip_area, &poly_coords, draw_ctx->clip_area);
  if (!is_common) {
    goto cleanup;
  }
  /* Calculate some parameters */
  lv_coord_t poly_w = lv_area_get_width(&poly_coords);
  lv_coord_t poly_h = lv_area_get_height(&poly_coords);
  vg_lite_path_t vpath;
  lv_memset_00(&vpath, sizeof(vg_lite_path_t));
  vg_lite_init_path(&vpath, VG_LITE_S16, VG_LITE_HIGH, path_size,
      poly_path, clip_area.x1, clip_area.y1, clip_area.x2 + 1, clip_area.y2 + 1);
  vg_lite_matrix_t matrix;
  vg_lite_identity(&matrix);
  vg_lite_blend_t blend = LV_BLEND_MODE_TO_VG(dsc->blend_mode);
  vg_lite_fill_t fill = VG_LITE_FILL_NON_ZERO;
  vg_lite_linear_gradient_t grad;
  vg_lite_error_t vgerr = VG_LITE_SUCCESS;

  if (dsc->bg_grad.dir != LV_GRAD_DIR_NONE) {
    /* Linear gradient fill */
    init_vg_buf(&grad.image, VLC_GRADBUFFER_WIDTH, 1,
        VLC_GRADBUFFER_WIDTH * sizeof(uint32_t), grad_mem, VG_LITE_BGRA8888, 0);
    grad.count = dsc->bg_grad.stops_count;
    for (int_fast16_t i = 0; i < grad.count; i++) {
      lv_color_t color = dsc->bg_grad.stops[i].color;
      if (dsc->bg_opa != LV_OPA_COVER) {
        color = lv_color_mix(color, lv_color_black(), dsc->bg_opa);
        LV_COLOR_SET_A(color, dsc->bg_opa);
      }
      grad.colors[i] = lv_color_to32(color);
      grad.stops[i] = dsc->bg_grad.stops[i].frac;
    }
    vg_lite_update_grad(&grad);

    vg_lite_identity(&grad.matrix);
    vg_lite_translate(poly_coords.x1 - disp_area->x1,
        poly_coords.y1 - disp_area->y1, &grad.matrix);
    if (dsc->bg_grad.dir == LV_GRAD_DIR_VER) {
      vg_lite_scale(1.0f, poly_h / 256.0f, &grad.matrix);
      vg_lite_rotate(90.0f, &grad.matrix);
    } else {
      vg_lite_scale(poly_w / 256.0f, 1.0f, &grad.matrix);
    }
    vg_lite_draw_gradient(&dst_vgbuf, &vpath, fill, &matrix, &grad, blend);
  } else {
    /* Regular fill */
    lv_color_t bg_color = dsc->bg_color;
    if (dsc->bg_opa < LV_OPA_MAX) {
      bg_color = lv_color_mix(bg_color, lv_color_black(), dsc->bg_opa);
      LV_COLOR_SET_A(bg_color, dsc->bg_opa);
    }
    vg_lite_color_t color = BGRA_TO_RGBA(lv_color_to32(bg_color));
    vg_lite_draw(&dst_vgbuf, &vpath, fill, &matrix, blend, color);
  }
  CHECK_ERROR(vg_lite_finish());

cleanup:
  lv_mem_buf_release(poly_path);
  return LV_RES_OK;
}