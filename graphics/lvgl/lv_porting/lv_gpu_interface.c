/****************************************************************************
 * apps/graphics/lvgl/lv_porting/lv_gpu_interface.c
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
#include "lv_gpu_interface.h"
#include "lv_gpu_draw.h"
#include "../lvgl/src/draw/sw/lv_draw_sw.h"
#include "../lvgl/src/misc/lv_color.h"
#include "gpu_port.h"
#include "lv_gpu_decoder.h"
#include "lv_gpu_draw.h"
#include "vg_lite.h"
#include <debug.h>
#include <lvgl/src/lv_conf_internal.h>
#include <nuttx/cache.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#ifdef CONFIG_ARM_HAVE_MVE
#include "arm_mve.h"
#endif

/****************************************************************************
 * Preprocessor Definitions
 ****************************************************************************/

/****************************************************************************
 * Macros
 ****************************************************************************/
#define __func__ __FUNCTION__
POSSIBLY_UNUSED static char* error_type[] = {
  "VG_LITE_SUCCESS",
  "VG_LITE_INVALID_ARGUMENT",
  "VG_LITE_OUT_OF_MEMORY",
  "VG_LITE_NO_CONTEXT",
  "VG_LITE_TIMEOUT",
  "VG_LITE_OUT_OF_RESOURCES",
  "VG_LITE_GENERIC_IO",
  "VG_LITE_NOT_SUPPORT",
};

#define IS_ERROR(status) (status > 0)
#define CHECK_ERROR(Function)                                                               \
  vgerr = Function;                                                                         \
  if (IS_ERROR(vgerr)) {                                                                    \
    GPU_ERROR("[%s: %d] failed.error type is %s\n", __func__, __LINE__, error_type[vgerr]); \
  }

/****************************************************************************
 * Private Data
 ****************************************************************************/

static lv_gpu_mode_t power_mode;
static vg_lite_path_t vpath;
static int16_t rect_path[13] = {
  VLC_OP_MOVE, 0, 0,
  VLC_OP_LINE, 0, 0,
  VLC_OP_LINE, 0, 0,
  VLC_OP_LINE, 0, 0,
  VLC_OP_END
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

LV_ATTRIBUTE_FAST_MEM static lv_res_t lv_draw_arc_gpu(lv_coord_t center_x,
    lv_coord_t center_y,
    uint16_t radius,
    uint16_t start_angle,
    uint16_t end_angle,
    const lv_area_t* clip_area,
    const lv_draw_arc_dsc_t* dsc)
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
  if (lv_draw_mask_is_any(clip_area) == true)
    return LV_RES_INV;

  lv_coord_t width = dsc->width;
  if (width > radius)
    width = radius;

  if (simple_check_intersect_or_inclue(center_x, center_y, width, radius,
          clip_area)
      == false) {
    return LV_RES_OK;
  }

  vg_lite_path_t vg_lite_path;
  memset(&vg_lite_path, 0, sizeof(vg_lite_path_t));

  uint32_t color_argb888 = lv_color_to32(dsc->color);
  vg_lite_color_t color = get_vg_lite_color_lvgl_mix(color_argb888, dsc->opa);

  vg_lite_blend_t vg_lite_blend = get_vg_lite_blend(dsc->blend_mode);

  vg_lite_buffer_t dst_vgbuf;
  size_t buffer_size = init_vg_lite_buffer_use_lv_buffer(&dst_vgbuf);

  /*Draw two semicircle*/
  if (start_angle + 360 == end_angle || start_angle == end_angle + 360) {
    draw_arc_path(&vg_lite_path, &dst_vgbuf, width, false, 0, 180, center_x,
        center_y, radius, vg_lite_blend, color, dsc->img_src,
        clip_area);

    draw_arc_path(&vg_lite_path, &dst_vgbuf, width, false, 180, 360,
        center_x, center_y, radius, vg_lite_blend, color,
        dsc->img_src, clip_area);

    vg_lite_finish();
    if (IS_CACHED(dst_vgbuf.memory)) {
      cpu_gpu_data_cache_invalid((uint32_t)dst_vgbuf.memory, buffer_size);
    }
    return LV_RES_OK;
  }

  while (start_angle >= 360)
    start_angle -= 360;
  while (end_angle >= 360)
    end_angle -= 360;

  draw_arc_path(&vg_lite_path, &dst_vgbuf, width, dsc->rounded, start_angle,
      end_angle, center_x, center_y, radius, vg_lite_blend, color,
      dsc->img_src, clip_area);

  vg_lite_finish();
  if (IS_CACHED(dst_vgbuf.memory)) {
    cpu_gpu_data_cache_invalid((uint32_t)dst_vgbuf.memory, buffer_size);
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

LV_ATTRIBUTE_FAST_MEM static void lv_draw_hw_arc(lv_coord_t center_x,
    lv_coord_t center_y,
    uint16_t radius,
    uint16_t start_angle,
    uint16_t end_angle,
    const lv_area_t* clip_area,
    const lv_draw_arc_dsc_t* dsc)
{
  if (lv_draw_arc_gpu(center_x, center_y, radius, start_angle, end_angle,
          clip_area, dsc)
      != LV_RES_OK) {
    lv_draw_sw_arc(center_x, center_y, radius, start_angle, end_angle,
        clip_area, dsc);
  }
}

LV_ATTRIBUTE_FAST_MEM static lv_res_t lv_draw_rect_gpu(const lv_area_t * coords,
                                                       const lv_area_t * clip,
                                                       const lv_draw_rect_dsc_t * dsc)
{
    if(lv_area_get_height(coords) < 1 || lv_area_get_width(coords) < 1) {
        return LV_RES_OK;
    }

    // Not support temporary.
    if (lv_draw_mask_is_any(clip) == true) return LV_RES_INV;

    bool draw_shadow = true;
    if(dsc->shadow_width == 0) draw_shadow = false;
    if(dsc->shadow_opa <= LV_OPA_MIN) draw_shadow = false;

    if(dsc->shadow_width == 1 && dsc->shadow_spread <= 0 &&
       dsc->shadow_ofs_x == 0 && dsc->shadow_ofs_y == 0) {
        draw_shadow = false;
    }
    // Not support temporary.
    if (draw_shadow == true) return LV_RES_INV;

    // Not support temporary.
    if (dsc->bg_img_src != NULL && dsc->bg_img_opa > LV_OPA_MIN) return LV_RES_INV;

    if (dsc->border_side != LV_BORDER_SIDE_NONE) {
        // Not support temporary.
        if (!(dsc->border_side & LV_BORDER_SIDE_LEFT)
            || !(dsc->border_side & LV_BORDER_SIDE_TOP)
            || !(dsc->border_side & LV_BORDER_SIDE_RIGHT)
            || !(dsc->border_side & LV_BORDER_SIDE_BOTTOM)) {
            return LV_RES_INV;
        }
    }

    vg_lite_buffer_t dst_vgbuf;
    size_t buffer_size = init_vg_lite_buffer_use_lv_buffer(&dst_vgbuf);

    vg_lite_path_t vg_lite_path;
    memset(&vg_lite_path, 0, sizeof(vg_lite_path_t));

    if (draw_rect_path(&dst_vgbuf, &vg_lite_path, coords, clip, dsc) == false) {
        return LV_RES_OK;
    }

    vg_lite_finish();
    if (IS_CACHED(dst_vgbuf.memory)) {
        cpu_gpu_data_cache_invalid((uint32_t)dst_vgbuf.memory, buffer_size);
    }

    LV_ASSERT_MEM_INTEGRITY();
    return LV_RES_OK;
}

LV_ATTRIBUTE_FAST_MEM static void lv_draw_hw_rect(const lv_area_t * coords,
                                                  const lv_area_t * clip,
                                                  const lv_draw_rect_dsc_t * dsc)
{
    if (lv_draw_rect_gpu(coords, clip, dsc) != LV_RES_OK) {
        lv_draw_sw_rect(coords, clip, dsc);
    }
}

LV_ATTRIBUTE_FAST_MEM static void lv_draw_hw_img(const lv_area_t* map_area, const lv_area_t* clip_area,
    const uint8_t* map_p, const lv_draw_img_dsc_t* draw_dsc, bool chroma_key, bool alpha_byte)
{
  if (lv_draw_map_gpu(map_area, clip_area, map_p, draw_dsc, chroma_key, alpha_byte) != LV_RES_OK) {
    lv_draw_sw_img(map_area, clip_area, map_p + sizeof(gpu_data_header_t), draw_dsc, chroma_key, alpha_byte);
  }
}

LV_ATTRIBUTE_FAST_MEM static void recolor_palette(uint32_t* dst, const uint32_t* src, uint16_t size, uint32_t recolor, lv_opa_t opa)
{
  /* TODO: MVE acceleration */
  uint16_t recolor_premult[3] = { (recolor >> 16 & 0xFF) * opa, (recolor >> 8 & 0xFF) * opa, (recolor & 0xFF) * opa };
  lv_opa_t mix = 255 - opa;
  for (int_fast16_t i = 0; i < size; i++) {
    if (src != NULL) {
      /* index recolor */
      if (src[i] >> 24 == 0) {
        dst[i] = 0;
      } else {
        uint8_t src_coeff = (mix << 8) / (src[i] >> 24);
        dst[i] = 0xFF000000 | /* A */
            (((recolor_premult[0] << 8) + ((src[i] & 0xFF0000) * src_coeff >> 8)) & 0xFF0000) | /* R */
            ((recolor_premult[1] + ((src[i] & 0xFF00) * src_coeff >> 8)) & 0xFF00) | /* G */
            (recolor_premult[2] + (src[i] & 0xFF) * src_coeff) >> 8; /* B */
      }
    } else {
      /* fill alpha palette */
      uint32_t opa_i = (size == 256) ? i : i * 0x11;
      dst[i] = opa_i << 24 | /* A */
          ((uint32_t)recolor_premult[0] * opa_i & 0xFF0000) | /* R */
          ((uint32_t)recolor_premult[1] * opa_i & 0xFF0000) >> 8 | /* G */
          ((uint32_t)recolor_premult[2] * opa_i & 0xFF0000) >> 16; /* B */
    }
  }
}

LV_ATTRIBUTE_FAST_MEM static void fill_rect_path(lv_area_t* area)
{
  rect_path[1] = rect_path[4] = area->x1;
  rect_path[7] = rect_path[10] = area->x2 + 1;
  rect_path[2] = rect_path[11] = area->y1;
  rect_path[5] = rect_path[8] = area->y2 + 1;
}

static void lv_gpu_backend_init(void)
{
  static lv_draw_backend_t backend;
  lv_draw_backend_init(&backend);

  backend.draw_arc = lv_draw_hw_arc;
  backend.draw_rect = lv_draw_hw_rect;
  backend.draw_letter = lv_draw_sw_letter;
  backend.draw_img = lv_draw_hw_img;
  backend.draw_line = lv_draw_sw_line;
  backend.draw_polygon = lv_draw_sw_polygon;
  backend.blend_fill = lv_blend_sw_fill;
  backend.blend_map = lv_blend_sw_map;

  lv_draw_backend_add(&backend);
}

/****************************************************************************
 * Global Functions
 ****************************************************************************/
/***
 * Fills vg_lite_buffer_t structure according given parameters.
 * @param[out] dst Buffer structure to be filled
 * @param[in] width Width of buffer in pixels
 * @param[in] height Height of buffer in pixels
 * @param[in] stride Stride of the buffer in bytes
 * @param[in] ptr Pointer to the buffer (must be aligned according VG-Lite requirements)
 */
LV_ATTRIBUTE_FAST_MEM lv_res_t init_vg_buf(void* vdst, uint32_t width, uint32_t height, uint32_t stride, void* ptr, uint8_t format, bool source)
{
  vg_lite_buffer_t* dst = vdst;
  if (source && (width & 0xF)) { /*Test for stride alignment*/
    GPU_WARN("Buffer width (%ld) not aligned to 16px.", width);
    return -1;
  }

  dst->format = format;
  dst->tiled = VG_LITE_LINEAR;
  dst->image_mode = VG_LITE_NORMAL_IMAGE_MODE;
  dst->transparency_mode = VG_LITE_IMAGE_OPAQUE;

  dst->width = width;
  dst->height = height;
  dst->stride = stride;

  memset(&dst->yuv, 0, sizeof(dst->yuv));

  dst->memory = ptr;
  dst->address = (uint32_t)dst->memory;
  dst->handle = NULL;

  return LV_RES_OK;
}

/****************************************************************************
 * Name: lv_draw_map_gpu
 *
 * Description:
 *   Copy a transformed map (image) to a display buffer.
 *
 * Input Parameters:
 * @param map_area area of the image  (absolute coordinates)
 * @param clip_area clip the map to this area (absolute coordinates)
 * @param map_buf a pixels of the map (image)
 * @param opa overall opacity in 0x00..0xff range
 * @param chroma chroma key color
 * @param angle rotation angle (= degree*10)
 * @param zoom image scale in 0..65535 range, where 256 is 1.0x scale
 * @param mode blend mode from `lv_blend_mode_t`
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

LV_ATTRIBUTE_FAST_MEM lv_res_t lv_draw_map_gpu(const lv_area_t* map_area, const lv_area_t* clip_area,
    const uint8_t* map_buf, const lv_draw_img_dsc_t* draw_dsc, bool chroma_key, bool alpha_byte)
{
  lv_opa_t opa = draw_dsc->opa;
  if (opa < LV_OPA_MIN) {
    return LV_RES_OK;
  }

  uint16_t angle = draw_dsc->angle;
  uint16_t zoom = draw_dsc->zoom;
  lv_point_t pivot = draw_dsc->pivot;
  lv_blend_mode_t blend_mode = draw_dsc->blend_mode;
  lv_color_t recolor = draw_dsc->recolor;
  lv_opa_t recolor_opa = draw_dsc->recolor_opa;
  vg_lite_buffer_t src_vgbuf;
  bool transformed = (angle != 0) || (zoom != LV_IMG_ZOOM_NONE);
  lv_disp_t* disp = _lv_refr_get_disp_refreshing();
  lv_disp_draw_buf_t* draw_buf = lv_disp_get_draw_buf(disp);
  const lv_area_t* disp_area = &draw_buf->area;
  lv_color_t* disp_buf = draw_buf->buf_act;
  int32_t disp_w = lv_area_get_width(disp_area);
  int32_t disp_h = lv_area_get_height(disp_area);
  int32_t map_w = lv_area_get_width(map_area);
  int32_t map_h = lv_area_get_height(map_area);
  vg_lite_buffer_t dst_vgbuf;
  vg_lite_buffer_t* vgbuf;
  vg_lite_error_t vgerr = VG_LITE_SUCCESS;

  LV_ASSERT(init_vg_buf(&dst_vgbuf, disp_w, disp_h, disp_w * sizeof(lv_color_t), disp_buf, VGLITE_PX_FMT, false) == LV_RES_OK);
  uint32_t rect[4] = { 0, 0 };
  lv_area_t map_tf, draw_area;
  lv_area_copy(&draw_area, clip_area);
  lv_area_move(&draw_area, -disp_area->x1, -disp_area->y1);
  _lv_img_buf_get_transformed_area(&map_tf, map_w, map_h, angle, zoom, &pivot);
  lv_area_move(&map_tf, map_area->x1, map_area->y1);
  lv_area_move(&map_tf, -disp_area->x1, -disp_area->y1);
  if (_lv_area_intersect(&draw_area, &draw_area, &map_tf) == false) {
    return LV_RES_OK;
  }

  GPU_INFO("map_tf: (%d %d)[%d,%d] draw_area:(%d %d)[%d,%d] zoom:%d\n", map_tf.x1, map_tf.y1, lv_area_get_width(&map_tf),
      lv_area_get_height(&map_tf), draw_area.x1, draw_area.y1, lv_area_get_width(&draw_area), lv_area_get_height(&draw_area), zoom);
  bool indexed = false, alpha = false;
  bool allocated_src = false;
  vgbuf = lv_gpu_get_vgbuf((void*)map_buf);
  if (vgbuf) {
    indexed = (vgbuf->format >= VG_LITE_INDEX_1) && (vgbuf->format <= VG_LITE_INDEX_8);
    alpha = (vgbuf->format == VG_LITE_A4) || (vgbuf->format == VG_LITE_A8);
    lv_memcpy_small(&src_vgbuf, vgbuf, sizeof(src_vgbuf));
  } else {
    vgbuf = NULL;
    GPU_WARN("allocating new vgbuf:(%ld,%ld)", map_w, map_h);
    lv_img_header_t header;
    header.w = map_w;
    header.h = map_h;
    header.cf = alpha_byte ? LV_IMG_CF_TRUE_COLOR_ALPHA : chroma_key ? LV_IMG_CF_TRUE_COLOR_CHROMA_KEYED
                                                                     : LV_IMG_CF_TRUE_COLOR;
    if (lv_gpu_load_vgbuf(map_buf, &header, &src_vgbuf, NULL) != LV_RES_OK) {
      GPU_ERROR("load failed");
      goto Fallback;
    }
    allocated_src = true;
  }

  const uint32_t* palette = (const uint32_t*)(map_buf + sizeof(gpu_data_header_t) + src_vgbuf.stride * src_vgbuf.height);

  vg_lite_matrix_t matrix;
  vg_lite_identity(&matrix);
  vg_lite_translate(map_area->x1 - disp_area->x1, map_area->y1 - disp_area->y1, &matrix);
  rect[2] = map_w;
  rect[3] = map_h;
  if (transformed) {
    vg_lite_translate(pivot.x, pivot.y, &matrix);
    vg_lite_float_t scale = zoom * 1.0f / LV_IMG_ZOOM_NONE;
    if (zoom != LV_IMG_ZOOM_NONE) {
      vg_lite_scale(scale, scale, &matrix);
    }
    if (angle != 0) {
      vg_lite_rotate(angle / 10.0f, &matrix);
    }
    vg_lite_translate(-pivot.x, -pivot.y, &matrix);
  }
  lv_color32_t color;
  vg_lite_blend_t blend = (blend_mode == LV_BLEND_MODE_NORMAL) ? VG_LITE_BLEND_SRC_OVER : (blend_mode == LV_BLEND_MODE_ADDITIVE) ? VG_LITE_BLEND_ADDITIVE
      : (blend_mode == LV_BLEND_MODE_SUBTRACTIVE)                                                                                ? VG_LITE_BLEND_SUBTRACT
                                                                                                                                 : VG_LITE_BLEND_NONE;
  if (opa >= LV_OPA_MAX) {
    color.full = 0x0;
    src_vgbuf.image_mode = VG_LITE_NORMAL_IMAGE_MODE;
  } else {
    color.full = (opa << 24) | (opa << 16) | (opa << 8) | opa;
    src_vgbuf.image_mode = VG_LITE_MULTIPLY_IMAGE_MODE;
  }

  vg_lite_filter_t filter = transformed ? VG_LITE_FILTER_BI_LINEAR : VG_LITE_FILTER_POINT;
  vg_lite_matrix_t matrix0;
  vg_lite_identity(&matrix0);

  fill_rect_path(&draw_area);

  CHECK_ERROR(vg_lite_init_path(&vpath, VG_LITE_S16, VG_LITE_LOW, sizeof(rect_path), rect_path,
      0, 0, 479, 479));
  if (indexed || alpha) {
    uint8_t px_size = VG_FMT_TO_BPP(src_vgbuf.format);
    uint16_t palette_size = 1 << px_size;
    src_vgbuf.format = BPP_TO_VG_FMT(px_size);
    if (alpha || recolor_opa != LV_OPA_TRANSP) {
      uint32_t* palette_r = lv_mem_alloc(palette_size * sizeof(lv_color32_t));
      if (palette_r == NULL) {
        goto Error_handler;
      }
      recolor_palette(palette_r, alpha ? NULL : palette, palette_size,
          recolor_opa != LV_OPA_TRANSP ? lv_color_to32(recolor) : *palette, recolor_opa);
      vg_lite_set_CLUT(palette_size, palette_r);
      lv_mem_free(palette_r);
    } else {
      vg_lite_set_CLUT(palette_size, (uint32_t*)palette);
    }
  } else if (recolor_opa != LV_OPA_TRANSP) {
    /* ARGB recolor, unsupported by GPU */
    return LV_RES_INV;
  }
  if (_lv_area_is_in(&map_tf, &draw_area, 0)) {
    /* No clipping, simply blit */
    CHECK_ERROR(vg_lite_blit_rect(&dst_vgbuf, &src_vgbuf, rect, &matrix, blend, color.full, filter));
  } else if (!transformed && map_tf.x1 == draw_area.x1 && map_tf.y1 == draw_area.y1) {
    /* Clipped from left top, use good old blit_rect */
    rect[2] = lv_area_get_width(&draw_area);
    rect[3] = lv_area_get_height(&draw_area);

    CHECK_ERROR(vg_lite_blit_rect(&dst_vgbuf, &src_vgbuf, rect, &matrix, blend, color.full, filter));
  } else {
    /* arbitrarily clipped, have to use draw_pattern */
    CHECK_ERROR(vg_lite_set_multiply_color(color.full));
    CHECK_ERROR(vg_lite_draw_pattern(&dst_vgbuf, &vpath, VG_LITE_FILL_NON_ZERO, &matrix0, &src_vgbuf, &matrix, blend, VG_LITE_PATTERN_COLOR, 0, filter));
  }

  TC_INIT
  TC_START
  CHECK_ERROR(vg_lite_finish());
  TC_END
  TC_REP(vg_lite_finish)

Error_handler:
  if (allocated_src) {
    GPU_WARN("freeing allocated vgbuf:(%ld,%ld)@%p", src_vgbuf.width, src_vgbuf.height, src_vgbuf.memory);
    free(src_vgbuf.memory);
  }
  if (vgerr != VG_LITE_SUCCESS) {
    goto Fallback;
  }
  if (IS_CACHED(dst_vgbuf.memory)) {
    TC_START
    up_invalidate_dcache((uintptr_t)dst_vgbuf.memory, (uintptr_t)dst_vgbuf.memory + dst_vgbuf.height * dst_vgbuf.stride);
    TC_END
    TC_REP(draw_map_cache_invalid)
  }
  return LV_RES_OK;
Fallback:
  if (IS_ERROR(vgerr)) {
    GPU_ERROR("[%s: %d] failed.error type is %s\n", __func__, __LINE__, error_type[vgerr]);
  }
  /*Fall back to SW render in case of error*/
  GPU_ERROR("GPU blit failed. Fallback to SW.\n");
  return LV_RES_INV;
}

/****************************************************************************
 * Name: lv_gpu_interface_init
 *
 * Description:
 *   GPU interface initialization.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   LV_RES_OK on success; LV_RES_INV on failure.
 *
 ****************************************************************************/

lv_res_t lv_gpu_interface_init(void)
{
  gpu_init();
  lv_gpu_backend_init();
  lv_gpu_decoder_init();
  return lv_gpu_setmode(LV_GPU_DEFAULT_MODE);
}

/****************************************************************************
 * Name: lv_gpu_getmode
 *
 * Description:
 *   Get GPU power mode at runtime.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   power mode from lv_gpu_mode_t
 *
 ****************************************************************************/

lv_gpu_mode_t lv_gpu_getmode(void)
{
  return power_mode;
}

/****************************************************************************
 * Name: lv_gpu_setmode
 *
 * Description:
 *   Set GPU power mode at runtime.
 *
 * Input Parameters:
 *   mode - power mode from lv_gpu_mode_t
 *
 * Returned Value:
 *   LV_RES_OK on success; LV_RES_INV on failure.
 *
 ****************************************************************************/

lv_res_t lv_gpu_setmode(lv_gpu_mode_t mode)
{
  power_mode = mode;
  /* TODO: set driver power*/
  return LV_RES_OK;
}

/****************************************************************************
 * Name: lv_gpu_color_fmt_convert
 *
 * Description:
 *   Use GPU to convert color formats (16 to/from 32).
 *
 * Input Parameters:
 * @param[in] dsc descriptor of destination and source
 *   (see lv_gpu_color_fmt_convert_dsc_t)
 *
 * Returned Value:
 * @return LV_RES_OK on success, LV_RES_INV on failure.
 *
 ****************************************************************************/

LV_ATTRIBUTE_FAST_MEM lv_res_t lv_gpu_color_fmt_convert(const lv_gpu_color_fmt_convert_dsc_t* dsc)
{
  vg_lite_buffer_t src;
  vg_lite_buffer_t dst;
  lv_coord_t w = dsc->width;
  lv_coord_t h = dsc->height;
  vg_lite_error_t vgerr;

  if (init_vg_buf(&src, w, h, w * dsc->src_bpp >> 3, (void*)dsc->src, BPP_TO_VG_FMT(dsc->src_bpp), 1) != LV_RES_OK
      || init_vg_buf(&dst, w, h, w * dsc->dst_bpp >> 3, dsc->dst, BPP_TO_VG_FMT(dsc->dst_bpp), 0) != LV_RES_OK) {
    return LV_RES_INV;
  }
  CHECK_ERROR(vg_lite_blit(&dst, &src, NULL, 0, 0, 0));
  CHECK_ERROR(vg_lite_finish());
  if (vgerr != VG_LITE_SUCCESS) {
    GPU_ERROR("GPU convert failed.");
    return LV_RES_INV;
  }
  TC_INIT
  if (IS_CACHED(dst.memory)) {
    TC_START
    up_invalidate_dcache((uintptr_t)dst.memory, (uintptr_t)dst.memory + h * dst.stride);
    TC_END
    TC_REP(dst_cache_invalid)
  }
  return LV_RES_OK;
}
