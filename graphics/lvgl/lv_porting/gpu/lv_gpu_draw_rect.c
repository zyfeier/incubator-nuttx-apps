/****************************************************************************
 * apps/graphics/lvgl/lv_porting/gpu/lv_gpu_draw_rect.c
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

/****************************************************************************
 * Preprocessor Definitions
 ****************************************************************************/

/****************************************************************************
 * Macros
 ****************************************************************************/

/****************************************************************************
 * Private Data
 ****************************************************************************/

static float rect_path[GPU_POINT_PATH_SIZE << 1];

/****************************************************************************
 * Private Functions
 ****************************************************************************/

LV_ATTRIBUTE_FAST_MEM static bool draw_shadow(lv_draw_ctx_t* draw_ctx,
    const lv_draw_rect_dsc_t* dsc, const lv_area_t* coords)
{
  /*Check whether the shadow is visible*/
  if (dsc->shadow_width == 0)
    return false;
  if (dsc->shadow_opa <= LV_OPA_MIN)
    return false;

  if (dsc->shadow_width == 1 && dsc->shadow_spread <= 0
      && dsc->shadow_ofs_x == 0 && dsc->shadow_ofs_y == 0)
    return false;

  GPU_WARN("Draw shadow unsupported, fallback to SW");
  return true;
}

LV_ATTRIBUTE_FAST_MEM static void draw_bg(lv_draw_ctx_t* draw_ctx,
    const lv_draw_rect_dsc_t* dsc, const lv_area_t* coords)
{
  if (dsc->bg_opa <= LV_OPA_MIN)
    return;

  lv_area_t draw_area;
  if (!_lv_area_intersect(&draw_area, coords, draw_ctx->clip_area))
    return;

  uint16_t len = gpu_fill_path(rect_path, GPU_RECT_PATH,
      (const lv_point_t*)coords, dsc);
  lv_gpu_grad_dsc_t grad = {
    .coords = (lv_area_t*)coords,
    .grad_dsc = (lv_grad_dsc_t*)&dsc->bg_grad
  };
  lv_gpu_curve_fill_t fill = {
    .color = dsc->bg_color,
    .opa = dsc->bg_opa,
    .grad = &grad,
    .type = dsc->bg_grad.dir == LV_GRAD_DIR_NONE
        ? CURVE_FILL_COLOR
        : CURVE_FILL_LINEAR_GRADIENT
  };
  lv_gpu_buffer_t gpu_buf = {
    .buf = draw_ctx->buf,
    .buf_area = draw_ctx->buf_area,
    .clip_area = (lv_area_t*)draw_ctx->clip_area,
    .w = lv_area_get_width(draw_ctx->buf_area),
    .h = lv_area_get_height(draw_ctx->buf_area),
    .cf = LV_IMG_CF_TRUE_COLOR_ALPHA
  };
  vg_lite_path_t vpath = {
    .path = rect_path,
    .path_length = len * sizeof(float)
  };
  bool masked = lv_gpu_draw_mask_apply_path(&vpath, coords);
  gpu_draw_path(vpath.path, vpath.path_length, &fill, &gpu_buf);
  if (masked) {
    lv_mem_buf_release(vpath.path);
  }
}
LV_ATTRIBUTE_FAST_MEM static void draw_bg_img(lv_draw_ctx_t* draw_ctx,
    const lv_draw_rect_dsc_t* dsc, const lv_area_t* coords)
{
  if (dsc->bg_img_src == NULL)
    return;
  if (dsc->bg_img_opa <= LV_OPA_MIN)
    return;

  lv_area_t draw_area;
  if (!_lv_area_intersect(&draw_area, coords, draw_ctx->clip_area))
    return;

  lv_img_src_t src_type = lv_img_src_get_type(dsc->bg_img_src);
  if (src_type == LV_IMG_SRC_SYMBOL) {
    lv_point_t size;
    lv_txt_get_size(&size, dsc->bg_img_src, dsc->bg_img_symbol_font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    lv_area_t a;
    a.x1 = coords->x1 + lv_area_get_width(coords) / 2 - size.x / 2;
    a.x2 = a.x1 + size.x - 1;
    a.y1 = coords->y1 + lv_area_get_height(coords) / 2 - size.y / 2;
    a.y2 = a.y1 + size.y - 1;

    lv_draw_label_dsc_t label_draw_dsc;
    lv_draw_label_dsc_init(&label_draw_dsc);
    label_draw_dsc.font = dsc->bg_img_symbol_font;
    label_draw_dsc.color = dsc->bg_img_recolor;
    label_draw_dsc.opa = dsc->bg_img_opa;
    lv_draw_label(draw_ctx, &label_draw_dsc, &a, dsc->bg_img_src, NULL);
  } else {
    lv_img_header_t header;
    lv_res_t res = lv_img_decoder_get_info(dsc->bg_img_src, &header);
    if (res != LV_RES_OK) {
      LV_LOG_WARN("Couldn't read the background image");
      return;
    }

    lv_draw_img_dsc_t img_dsc;
    lv_draw_img_dsc_init(&img_dsc);
    img_dsc.blend_mode = dsc->blend_mode;
    img_dsc.recolor = dsc->bg_img_recolor;
    img_dsc.recolor_opa = dsc->bg_img_recolor_opa;
    img_dsc.opa = dsc->bg_img_opa;

    /*Center align*/
    if (dsc->bg_img_tiled == false) {
      lv_area_t area;
      area.x1 = coords->x1 + lv_area_get_width(coords) / 2 - header.w / 2;
      area.y1 = coords->y1 + lv_area_get_height(coords) / 2 - header.h / 2;
      area.x2 = area.x1 + header.w - 1;
      area.y2 = area.y1 + header.h - 1;

      lv_draw_img(draw_ctx, &img_dsc, &area, dsc->bg_img_src);
    } else {
      lv_area_t area;
      area.y1 = coords->y1;
      area.y2 = area.y1 + header.h - 1;

      for (; area.y1 <= coords->y2; area.y1 += header.h, area.y2 += header.h) {

        area.x1 = coords->x1;
        area.x2 = area.x1 + header.w - 1;
        for (; area.x1 <= coords->x2; area.x1 += header.w, area.x2 += header.w) {
          lv_draw_img(draw_ctx, &img_dsc, &area, dsc->bg_img_src);
        }
      }
    }
  }
}

LV_ATTRIBUTE_FAST_MEM static void draw_border(lv_draw_ctx_t* draw_ctx,
    const lv_draw_rect_dsc_t* dsc, const lv_area_t* coords)
{
  if (dsc->border_opa <= LV_OPA_MIN)
    return;
  if (dsc->border_width == 0)
    return;
  if (dsc->border_side == LV_BORDER_SIDE_NONE)
    return;
  if (dsc->border_post)
    return;

  lv_coord_t coords_w = lv_area_get_width(coords);
  lv_coord_t coords_h = lv_area_get_height(coords);
  lv_coord_t r_short = LV_MIN(coords_w, coords_h) >> 1;
  lv_coord_t rout = LV_MIN(dsc->radius, r_short);
  lv_coord_t rin = LV_MAX(0, rout - dsc->border_width);

  lv_area_t draw_area;
  if (!_lv_area_intersect(&draw_area, coords, draw_ctx->clip_area))
    return;
  /*Get the inner area*/
  lv_area_t area_inner;
  lv_area_copy(&area_inner, coords);
  area_inner.x1 += ((dsc->border_side & LV_BORDER_SIDE_LEFT) ? dsc->border_width : -(dsc->border_width + rout));
  area_inner.x2 -= ((dsc->border_side & LV_BORDER_SIDE_RIGHT) ? dsc->border_width : -(dsc->border_width + rout));
  area_inner.y1 += ((dsc->border_side & LV_BORDER_SIDE_TOP) ? dsc->border_width : -(dsc->border_width + rout));
  area_inner.y2 -= ((dsc->border_side & LV_BORDER_SIDE_BOTTOM) ? dsc->border_width : -(dsc->border_width + rout));

  uint16_t len = gpu_fill_path(rect_path, GPU_RECT_PATH,
      (const lv_point_t*)coords, dsc);
  lv_draw_rect_dsc_t inner_dsc = { .radius = rin };
  len += gpu_fill_path(rect_path + len, GPU_RECT_PATH,
      (const lv_point_t*)&area_inner, &inner_dsc);
  lv_gpu_curve_fill_t fill = {
    .color = dsc->border_color,
    .opa = dsc->border_opa,
    .type = CURVE_FILL_COLOR | CURVE_FILL_RULE_EVENODD
  };
  lv_gpu_buffer_t gpu_buf = {
    .buf = draw_ctx->buf,
    .buf_area = draw_ctx->buf_area,
    .clip_area = (lv_area_t*)draw_ctx->clip_area,
    .w = lv_area_get_width(draw_ctx->buf_area),
    .h = lv_area_get_height(draw_ctx->buf_area),
    .cf = LV_IMG_CF_TRUE_COLOR_ALPHA
  };
#ifdef CONFIG_LV_GPU_USE_LOG
  if (lv_draw_mask_is_any(coords)) {
    GPU_WARN("Mask detected, output may be wrong");
  }
#endif
  gpu_draw_path(rect_path, len * sizeof(float), &fill, &gpu_buf);
}

LV_ATTRIBUTE_FAST_MEM static void draw_outline(lv_draw_ctx_t* draw_ctx,
    const lv_draw_rect_dsc_t* dsc, const lv_area_t* coords)
{
  if (dsc->outline_opa <= LV_OPA_MIN)
    return;
  if (dsc->outline_width == 0)
    return;

  /*Bring the outline closer to make sure there is no color bleeding with pad=0*/
  lv_coord_t pad = dsc->outline_pad - 1;

  lv_area_t area_inner = {
    .x1 = coords->x1 - pad,
    .y1 = coords->y1 - pad,
    .x2 = coords->x2 + pad,
    .y2 = coords->y2 + pad
  };
  lv_area_t area_outer = {
    .x1 = area_inner.x1 - dsc->outline_width,
    .y1 = area_inner.y1 - dsc->outline_width,
    .x2 = area_inner.x2 + dsc->outline_width,
    .y2 = area_inner.y2 + dsc->outline_width
  };
  lv_area_t draw_area;
  if (!_lv_area_intersect(&draw_area, &area_outer, draw_ctx->clip_area))
    return;

  int32_t inner_w = lv_area_get_width(&area_inner);
  int32_t inner_h = lv_area_get_height(&area_inner);
  lv_coord_t r_short = LV_MIN(inner_w, inner_h) >> 1;
  int32_t rin = LV_MIN(dsc->radius + pad, r_short);
  lv_draw_rect_dsc_t inner_dsc = { .radius = rin };
  uint16_t len = gpu_fill_path(rect_path, GPU_RECT_PATH,
      (const lv_point_t*)&area_inner, &inner_dsc);
  lv_coord_t rout = rin + dsc->outline_width;
  lv_draw_rect_dsc_t outer_dsc = { .radius = rout };
  len += gpu_fill_path(rect_path + len, GPU_RECT_PATH,
      (const lv_point_t*)&area_outer, &outer_dsc);
  lv_gpu_curve_fill_t fill = {
    .color = dsc->outline_color,
    .opa = dsc->outline_opa,
    .type = CURVE_FILL_COLOR | CURVE_FILL_RULE_EVENODD
  };
  lv_gpu_buffer_t gpu_buf = {
    .buf = draw_ctx->buf,
    .buf_area = draw_ctx->buf_area,
    .clip_area = (lv_area_t*)draw_ctx->clip_area,
    .w = lv_area_get_width(draw_ctx->buf_area),
    .h = lv_area_get_height(draw_ctx->buf_area),
    .cf = LV_IMG_CF_TRUE_COLOR_ALPHA
  };
#ifdef CONFIG_LV_GPU_USE_LOG
  if (lv_draw_mask_is_any(coords)) {
    GPU_WARN("Mask detected, output may be wrong");
  }
#endif
  gpu_draw_path(rect_path, len * sizeof(float), &fill, &gpu_buf);
}

/****************************************************************************
 * Global Functions
 ****************************************************************************/

/****************************************************************************
 * Name: lv_draw_rect_gpu
 *
 * Description:
 *   Draw arc with GPU
 *
 * Input Parameters:
 * @param draw_ctx draw context (refer to LVGL 8.2 changelog)
 * @param dsc draw rectangle description
 * @param coords rectangle area
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/
LV_ATTRIBUTE_FAST_MEM lv_res_t lv_draw_rect_gpu(
    struct _lv_draw_ctx_t* draw_ctx, const lv_draw_rect_dsc_t* dsc,
    const lv_area_t* coords)
{
  if (draw_shadow(draw_ctx, dsc, coords))
    return LV_RES_INV;

  draw_bg(draw_ctx, dsc, coords);
  draw_bg_img(draw_ctx, dsc, coords);

  draw_border(draw_ctx, dsc, coords);

  draw_outline(draw_ctx, dsc, coords);

  return LV_RES_OK;
}