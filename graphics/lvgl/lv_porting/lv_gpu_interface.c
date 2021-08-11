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
#include "../lvgl/src/misc/lv_color.h"
#include "../lvgl/src/misc/lv_gc.h"
#include "vg_lite.h"
#include <debug.h>
#include <lvgl/src/lv_conf_internal.h>
#include <stdio.h>
#include <time.h>
#ifdef CONFIG_ARM_HAVE_MVE
#include "arm_mve.h"
#endif

/****************************************************************************
 * Preprocessor Definitions
 ****************************************************************************/
#if LV_COLOR_DEPTH == 16
#define VGLITE_PX_FMT VG_LITE_BGR565
#elif LV_COLOR_DEPTH == 32
#define VGLITE_PX_FMT VG_LITE_BGRA8888
#endif

#define SHADOW_UPSCALE_SHIFT 6
#define SHADOW_ENHANCE 1
#define SPLIT_LIMIT 50
#define GPU_SIZE_LIMIT 240
#define GPU_SPLIT_SIZE (480 * 100)
/****************************************************************************
 * Macros
 ****************************************************************************/
#if LV_COLOR_SCREEN_TRANSP == 0
#define FILL_NORMAL_MASK_PX(color)                                 \
  if (*mask == LV_OPA_COVER)                                       \
    *disp_buf_first = color;                                       \
  else                                                             \
    *disp_buf_first = lv_color_mix(color, *disp_buf_first, *mask); \
  mask++;                                                          \
  disp_buf_first++;

#else
#define FILL_NORMAL_MASK_PX(color)                                                                                               \
  if (*mask == LV_OPA_COVER)                                                                                                     \
    *disp_buf_first = color;                                                                                                     \
  else if (disp->driver->screen_transp)                                                                                          \
    lv_color_mix_with_alpha(*disp_buf_first, disp_buf_first->ch.alpha, color, *mask, disp_buf_first, &disp_buf_first->ch.alpha); \
  else                                                                                                                           \
    *disp_buf_first = lv_color_mix(color, *disp_buf_first, *mask);                                                               \
  mask++;                                                                                                                        \
  disp_buf_first++;
#endif

#define MAP_NORMAL_MASK_PX(x)                                                             \
  if (*mask_tmp_x) {                                                                      \
    if (*mask_tmp_x == LV_OPA_COVER)                                                      \
      disp_buf_first[x] = map_buf_first[x];                                               \
    else                                                                                  \
      disp_buf_first[x] = lv_color_mix(map_buf_first[x], disp_buf_first[x], *mask_tmp_x); \
  }                                                                                       \
  mask_tmp_x++;

#define MAP_NORMAL_MASK_PX_SCR_TRANSP(x)                                                   \
  if (*mask_tmp_x) {                                                                       \
    if (*mask_tmp_x == LV_OPA_COVER)                                                       \
      disp_buf_first[x] = map_buf_first[x];                                                \
    else if (disp->driver->screen_transp)                                                  \
      lv_color_mix_with_alpha(disp_buf_first[x], disp_buf_first[x].ch.alpha,               \
          map_buf_first[x], *mask_tmp_x, &disp_buf_first[x], &disp_buf_first[x].ch.alpha); \
    else                                                                                   \
      disp_buf_first[x] = lv_color_mix(map_buf_first[x], disp_buf_first[x], *mask_tmp_x);  \
  }                                                                                        \
  mask_tmp_x++;

#define __func__ __FUNCTION__
static char* error_type[] = {
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
#define CHECK_ERROR(Function)                                                                  \
  error = Function;                                                                            \
  if (IS_ERROR(error)) {                                                                       \
    LV_LOG_ERROR("[%s: %d] failed.error type is %s\n", __func__, __LINE__, error_type[error]); \
  }

#ifndef ALIGN_UP
#define ALIGN_UP(num, align) (((num) + ((align)-1)) & ~((align)-1))
#endif

#ifndef IS_ALIGNED
#define IS_ALIGNED(num, align) (((uint32_t)(num) & ((align)-1)) == 0)
#endif

#ifndef MAX
#define MAX(a,b) ((a)>(b)?(a):(b))
#endif

extern void cpu_cache_flush(uint32_t start, uint32_t length);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static lv_gpu_mode_t power_mode;

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/
#if LV_USE_EXTERNAL_RENDERER

/* Copied from lv_draw_blend.c */

static void fill_set_px(const lv_area_t* disp_area, lv_color_t* disp_buf, const lv_area_t* draw_area,
    lv_color_t color, lv_opa_t opa,
    const lv_opa_t* mask, lv_draw_mask_res_t mask_res);

LV_ATTRIBUTE_FAST_MEM static void fill_normal(const lv_area_t* disp_area, lv_color_t* disp_buf,
    const lv_area_t* draw_area,
    lv_color_t color, lv_opa_t opa,
    const lv_opa_t* mask, lv_draw_mask_res_t mask_res);

#if LV_DRAW_COMPLEX
static void fill_blended(const lv_area_t* disp_area, lv_color_t* disp_buf, const lv_area_t* draw_area,
    lv_color_t color, lv_opa_t opa,
    const lv_opa_t* mask, lv_draw_mask_res_t mask_res, lv_blend_mode_t mode);
#endif

static void map_set_px(const lv_area_t* disp_area, lv_color_t* disp_buf, const lv_area_t* draw_area,
    const lv_area_t* map_area, const lv_color_t* map_buf, lv_opa_t opa,
    const lv_opa_t* mask, lv_draw_mask_res_t mask_res);

LV_ATTRIBUTE_FAST_MEM static void map_normal(const lv_area_t* disp_area, lv_color_t* disp_buf,
    const lv_area_t* draw_area,
    const lv_area_t* map_area, const lv_color_t* map_buf, lv_opa_t opa,
    const lv_opa_t* mask, lv_draw_mask_res_t mask_res);

#if LV_DRAW_COMPLEX
static void map_blended(const lv_area_t* disp_area, lv_color_t* disp_buf, const lv_area_t* draw_area,
    const lv_area_t* map_area, const lv_color_t* map_buf, lv_opa_t opa,
    const lv_opa_t* mask, lv_draw_mask_res_t mask_res, lv_blend_mode_t mode);

static inline lv_color_t color_blend_true_color_additive(lv_color_t fg, lv_color_t bg, lv_opa_t opa);
static inline lv_color_t color_blend_true_color_subtractive(lv_color_t fg, lv_color_t bg, lv_opa_t opa);
#endif

/* Copied from lv_draw_rect.c */

LV_ATTRIBUTE_FAST_MEM static void draw_bg(const lv_area_t* coords, const lv_area_t* clip_area,
    const lv_draw_rect_dsc_t* dsc);
LV_ATTRIBUTE_FAST_MEM static void draw_bg_img(const lv_area_t* coords, const lv_area_t* clip,
    const lv_draw_rect_dsc_t* dsc);
LV_ATTRIBUTE_FAST_MEM static void draw_border(const lv_area_t* coords, const lv_area_t* clip,
    const lv_draw_rect_dsc_t* dsc);

static void draw_outline(const lv_area_t* coords, const lv_area_t* clip, const lv_draw_rect_dsc_t* dsc);

#if LV_DRAW_COMPLEX
LV_ATTRIBUTE_FAST_MEM static void draw_shadow(const lv_area_t* coords, const lv_area_t* clip,
    const lv_draw_rect_dsc_t* dsc);
LV_ATTRIBUTE_FAST_MEM static void shadow_draw_corner_buf(const lv_area_t* coords, uint16_t* sh_buf, lv_coord_t s,
    lv_coord_t r);
LV_ATTRIBUTE_FAST_MEM static void shadow_blur_corner(lv_coord_t size, lv_coord_t sw, uint16_t* sh_ups_buf);
#endif

static void draw_border_generic(const lv_area_t* clip_area, const lv_area_t* outer_area, const lv_area_t* inner_area,
    lv_coord_t rout, lv_coord_t rin, lv_color_t color, lv_opa_t opa, lv_blend_mode_t blend_mode);

static void draw_border_simple(const lv_area_t* clip, const lv_area_t* outer_area, const lv_area_t* inner_area,
    lv_color_t color, lv_opa_t opa);

#if LV_DRAW_COMPLEX
LV_ATTRIBUTE_FAST_MEM static inline lv_color_t grad_get(const lv_draw_rect_dsc_t* dsc, lv_coord_t s, lv_coord_t i);
#endif

/* Copied from lv_draw_label.c */

LV_ATTRIBUTE_FAST_MEM static void draw_letter_normal(lv_coord_t pos_x, lv_coord_t pos_y, lv_font_glyph_dsc_t* g,
    const lv_area_t* clip_area,
    const uint8_t* map_p, lv_color_t color, lv_opa_t opa, lv_blend_mode_t blend_mode);

#if LV_DRAW_COMPLEX && LV_USE_FONT_SUBPX
static void draw_letter_subpx(lv_coord_t pos_x, lv_coord_t pos_y, lv_font_glyph_dsc_t* g, const lv_area_t* clip_area,
    const uint8_t* map_p, lv_color_t color, lv_opa_t opa, lv_blend_mode_t blend_mode);
#endif /*LV_DRAW_COMPLEX && LV_USE_FONT_SUBPX*/

/* Copied from lv_draw_img.c */

LV_ATTRIBUTE_FAST_MEM static lv_res_t lv_img_draw_core(const lv_area_t* coords, const lv_area_t* clip_area,
    const void* src,
    const lv_draw_img_dsc_t* draw_dsc);

LV_ATTRIBUTE_FAST_MEM static void lv_draw_map(const lv_area_t* map_area, const lv_area_t* clip_area,
    const uint8_t* map_p,
    const lv_draw_img_dsc_t* draw_dsc,
    bool chroma_key, bool alpha_byte);

static void show_error(const lv_area_t* coords, const lv_area_t* clip_area, const char* msg);
static void draw_cleanup(_lv_img_cache_entry_t* cache);

#endif /* LV_USE_EXTERNAL_RENDERER */

/****************************************************************************
 * Private Functions
 ****************************************************************************/
#if LV_USE_EXTERNAL_RENDERER
LV_ATTRIBUTE_FAST_MEM static inline void bgra5658_to_8888(const uint8_t* src, uint32_t* dst)
{
  lv_color32_t* c32 = dst;
  lv_color16_t* c16 = (const lv_color16_t*)src;
  c32->ch.red = c16->ch.red << 3 | c16->ch.red >> 2;
  c32->ch.green = c16->ch.green << 2 | c16->ch.green >> 4;
  c32->ch.blue = c16->ch.blue << 3 | c16->ch.blue >> 2;
  c32->ch.alpha = src[LV_IMG_PX_SIZE_ALPHA_BYTE - 1];
}

/* Copied from lv_draw_blend.c */
static void fill_set_px(const lv_area_t* disp_area, lv_color_t* disp_buf, const lv_area_t* draw_area,
    lv_color_t color, lv_opa_t opa,
    const lv_opa_t* mask, lv_draw_mask_res_t mask_res)
{

  lv_disp_t* disp = _lv_refr_get_disp_refreshing();

  /*Get the width of the `disp_area` it will be used to go to the next line*/
  int32_t disp_w = lv_area_get_width(disp_area);

  int32_t x;
  int32_t y;

  if (mask_res == LV_DRAW_MASK_RES_FULL_COVER) {
    for (y = draw_area->y1; y <= draw_area->y2; y++) {
      for (x = draw_area->x1; x <= draw_area->x2; x++) {
        disp->driver->set_px_cb(disp->driver, (void*)disp_buf, disp_w, x, y, color, opa);
      }
    }
  } else {
    /*The mask is relative to the clipped area.
         *In the cycles below mask will be indexed from `draw_area.x1`
         *but it corresponds to zero index. So prepare `mask_tmp` accordingly.*/
    const lv_opa_t* mask_tmp = mask - draw_area->x1;

    /*Get the width of the `draw_area` it will be used to go to the next line of the mask*/
    int32_t draw_area_w = lv_area_get_width(draw_area);

    for (y = draw_area->y1; y <= draw_area->y2; y++) {
      for (x = draw_area->x1; x <= draw_area->x2; x++) {
        if (mask_tmp[x]) {
          disp->driver->set_px_cb(disp->driver, (void*)disp_buf, disp_w, x, y, color,
              (uint32_t)((uint32_t)opa * mask_tmp[x]) >> 8);
        }
      }
      mask_tmp += draw_area_w;
    }
  }
}

/**
 * Fill an area with a color
 * @param disp_area the current display area (destination area)
 * @param disp_buf destination buffer
 * @param draw_area fill this area (relative to `disp_area`)
 * @param color fill color
 * @param opa overall opacity in 0x00..0xff range
 * @param mask a mask to apply on every pixel (uint8_t array with 0x00..0xff values).
 *                It fits into draw_area.
 * @param mask_res LV_MASK_RES_COVER: the mask has only 0xff values (no mask),
 *                 LV_MASK_RES_TRANSP: the mask has only 0x00 values (full transparent),
 *                 LV_MASK_RES_CHANGED: the mask has mixed values
 */
LV_ATTRIBUTE_FAST_MEM static void fill_normal(const lv_area_t* disp_area, lv_color_t* disp_buf,
    const lv_area_t* draw_area,
    lv_color_t color, lv_opa_t opa,
    const lv_opa_t* mask, lv_draw_mask_res_t mask_res)
{

  lv_disp_t* disp = _lv_refr_get_disp_refreshing();

  /*Get the width of the `disp_area` it will be used to go to the next line*/
  int32_t disp_w = lv_area_get_width(disp_area);

  int32_t draw_area_w = lv_area_get_width(draw_area);
  int32_t draw_area_h = lv_area_get_height(draw_area);

  /*Create a temp. disp_buf which always point to the first pixel of the destination area*/
  lv_color_t* disp_buf_first = disp_buf + disp_w * draw_area->y1 + draw_area->x1;

  int32_t x;
  int32_t y;

  /*Simple fill (maybe with opacity), no masking*/
  if (mask_res == LV_DRAW_MASK_RES_FULL_COVER) {
    if (opa > LV_OPA_MAX) {
#if LV_USE_GPU_NXP_PXP
      if (lv_area_get_size(draw_area) >= LV_GPU_NXP_PXP_FILL_SIZE_LIMIT) {
        lv_gpu_nxp_pxp_fill(disp_buf, disp_w, draw_area, color, opa);
        return;
      }
#elif LV_USE_GPU_NXP_VG_LITE
      if (lv_area_get_size(draw_area) >= LV_GPU_NXP_VG_LITE_FILL_SIZE_LIMIT) {
        if (lv_gpu_nxp_vglite_fill(disp_buf, disp_w, lv_area_get_height(disp_area), draw_area, color, opa) == LV_RES_OK) {
          return;
        }
      }
#elif LV_USE_GPU_STM32_DMA2D
      if (lv_area_get_size(draw_area) >= 240) {
        lv_gpu_stm32_dma2d_fill(disp_buf_first, disp_w, color, draw_area_w, draw_area_h);
        return;
      }
#endif

      if (disp->driver->gpu_fill_cb && lv_area_get_size(draw_area) > GPU_SIZE_LIMIT) {
        disp->driver->gpu_fill_cb(disp->driver, disp_buf, disp_w, draw_area, color);
        return;
      }

      /*Software rendering*/
      for (y = 0; y < draw_area_h; y++) {
        lv_color_fill(disp_buf_first, color, draw_area_w);
        disp_buf_first += disp_w;
      }
    }
    /*No mask with opacity*/
    else {
#if LV_USE_GPU_NXP_PXP
      if (lv_area_get_size(draw_area) >= LV_GPU_NXP_PXP_FILL_OPA_SIZE_LIMIT) {
        lv_gpu_nxp_pxp_fill(disp_buf, disp_w, draw_area, color, opa);
        return;
      }
#elif LV_USE_GPU_NXP_VG_LITE
      if (lv_area_get_size(draw_area) >= LV_GPU_NXP_VG_LITE_FILL_OPA_SIZE_LIMIT) {
        if (lv_gpu_nxp_vglite_fill(disp_buf, disp_w, lv_area_get_height(disp_area), draw_area, color, opa) == LV_RES_OK) {
          return;
        }
        /*Fall down to SW render in case of error*/
      }
#endif
      lv_color_t last_dest_color = lv_color_black();
      lv_color_t last_res_color = lv_color_mix(color, last_dest_color, opa);

      uint16_t color_premult[3];
      lv_color_premult(color, opa, color_premult);
      lv_opa_t opa_inv = 255 - opa;

      for (y = 0; y < draw_area_h; y++) {
        for (x = 0; x < draw_area_w; x++) {
          if (last_dest_color.full != disp_buf_first[x].full) {
            last_dest_color = disp_buf_first[x];

#if LV_COLOR_SCREEN_TRANSP
            if (disp->driver->screen_transp) {
              lv_color_mix_with_alpha(disp_buf_first[x], disp_buf_first[x].ch.alpha, color, opa, &last_res_color,
                  &last_res_color.ch.alpha);
            } else
#endif
            {
              last_res_color = lv_color_mix_premult(color_premult, disp_buf_first[x], opa_inv);
            }
          }
          disp_buf_first[x] = last_res_color;
        }
        disp_buf_first += disp_w;
      }
    }
  }
  /*Masked*/
  else {
    int32_t x_end4 = draw_area_w - 4;

#if LV_COLOR_DEPTH == 16
    uint32_t c32 = color.full + ((uint32_t)color.full << 16);
#endif

    /*Only the mask matters*/
    if (opa > LV_OPA_MAX) {
      for (y = 0; y < draw_area_h; y++) {
        for (x = 0; x < draw_area_w && ((lv_uintptr_t)(mask)&0x3); x++) {
          FILL_NORMAL_MASK_PX(color)
        }

        for (; x <= x_end4; x += 4) {
          uint32_t mask32 = *((uint32_t*)mask);
          if (mask32 == 0xFFFFFFFF) {
#if LV_COLOR_DEPTH == 16
            if ((lv_uintptr_t)disp_buf_first & 0x3) {
              *(disp_buf_first + 0) = color;
              uint32_t* d = (uint32_t*)(disp_buf_first + 1);
              *d = c32;
              *(disp_buf_first + 3) = color;
            } else {
              uint32_t* d = (uint32_t*)disp_buf_first;
              *d = c32;
              *(d + 1) = c32;
            }
#else
            disp_buf_first[0] = color;
            disp_buf_first[1] = color;
            disp_buf_first[2] = color;
            disp_buf_first[3] = color;
#endif
            disp_buf_first += 4;
            mask += 4;
          } else if (mask32) {
            FILL_NORMAL_MASK_PX(color)
            FILL_NORMAL_MASK_PX(color)
            FILL_NORMAL_MASK_PX(color)
            FILL_NORMAL_MASK_PX(color)
          } else {
            mask += 4;
            disp_buf_first += 4;
          }
        }

        for (; x < draw_area_w; x++) {
          FILL_NORMAL_MASK_PX(color)
        }
        disp_buf_first += (disp_w - draw_area_w);
      }
    }
    /*Handle opa and mask values too*/
    else {
      /*Buffer the result color to avoid recalculating the same color*/
      lv_color_t last_dest_color;
      lv_color_t last_res_color;
      lv_opa_t last_mask = LV_OPA_TRANSP;
      last_dest_color.full = disp_buf_first[0].full;
      last_res_color.full = disp_buf_first[0].full;
      lv_opa_t opa_tmp = LV_OPA_TRANSP;

      for (y = draw_area->y1; y <= draw_area->y2; y++) {
        const lv_opa_t* mask_tmp_x = mask;
        for (x = 0; x < draw_area_w; x++) {
          if (*mask_tmp_x) {
            if (*mask_tmp_x != last_mask)
              opa_tmp = *mask_tmp_x == LV_OPA_COVER ? opa : (uint32_t)((uint32_t)(*mask_tmp_x) * opa) >> 8;
            if (*mask_tmp_x != last_mask || last_dest_color.full != disp_buf_first[x].full) {
#if LV_COLOR_SCREEN_TRANSP
              if (disp->driver->screen_transp) {
                lv_color_mix_with_alpha(disp_buf_first[x], disp_buf_first[x].ch.alpha, color, opa_tmp, &last_res_color,
                    &last_res_color.ch.alpha);
              } else
#endif
              {
                if (opa_tmp == LV_OPA_COVER)
                  last_res_color = color;
                else
                  last_res_color = lv_color_mix(color, disp_buf_first[x], opa_tmp);
              }
              last_mask = *mask_tmp_x;
              last_dest_color.full = disp_buf_first[x].full;
            }
            disp_buf_first[x] = last_res_color;
          }
          mask_tmp_x++;
        }
        disp_buf_first += disp_w;
        mask += draw_area_w;
      }
    }
  }
}

#if LV_DRAW_COMPLEX
/**
 * Fill an area with a color but apply blending algorithms
 * @param disp_area the current display area (destination area)
 * @param disp_buf destination buffer
 * @param draw_area fill this area (relative to `disp_area`)
 * @param color fill color
 * @param opa overall opacity in 0x00..0xff range
 * @param mask a mask to apply on every pixel (uint8_t array with 0x00..0xff values).
 *                It fits into draw_area.
 * @param mask_res LV_MASK_RES_COVER: the mask has only 0xff values (no mask),
 *                 LV_MASK_RES_TRANSP: the mask has only 0x00 values (full transparent),
 *                 LV_MASK_RES_CHANGED: the mask has mixed values
 * @param mode blend mode from `lv_blend_mode_t`
 */
static void fill_blended(const lv_area_t* disp_area, lv_color_t* disp_buf, const lv_area_t* draw_area,
    lv_color_t color, lv_opa_t opa,
    const lv_opa_t* mask, lv_draw_mask_res_t mask_res, lv_blend_mode_t mode)
{
  /*Get the width of the `disp_area` it will be used to go to the next line*/
  int32_t disp_w = lv_area_get_width(disp_area);

  /*Create a temp. disp_buf which always point to current line to draw*/
  lv_color_t* disp_buf_tmp = disp_buf + disp_w * draw_area->y1;

  lv_color_t (*blend_fp)(lv_color_t, lv_color_t, lv_opa_t);
  switch (mode) {
  case LV_BLEND_MODE_ADDITIVE:
    blend_fp = color_blend_true_color_additive;
    break;
  case LV_BLEND_MODE_SUBTRACTIVE:
    blend_fp = color_blend_true_color_subtractive;
    break;
  default:
    LV_LOG_WARN("fill_blended: unsupported blend mode");
    return;
  }

  int32_t x;
  int32_t y;

  /*Simple fill (maybe with opacity), no masking*/
  if (mask_res == LV_DRAW_MASK_RES_FULL_COVER) {
    lv_color_t last_dest_color = lv_color_black();
    lv_color_t last_res_color = lv_color_mix(color, last_dest_color, opa);
    for (y = draw_area->y1; y <= draw_area->y2; y++) {
      for (x = draw_area->x1; x <= draw_area->x2; x++) {
        if (last_dest_color.full != disp_buf_tmp[x].full) {
          last_dest_color = disp_buf_tmp[x];
          last_res_color = blend_fp(color, disp_buf_tmp[x], opa);
        }
        disp_buf_tmp[x] = last_res_color;
      }
      disp_buf_tmp += disp_w;
    }
  }
  /*Masked*/
  else {
    /*Get the width of the `draw_area` it will be used to go to the next line of the mask*/
    int32_t draw_area_w = lv_area_get_width(draw_area);

    /*The mask is relative to the clipped area.
         *In the cycles below mask will be indexed from `draw_area.x1`
         *but it corresponds to zero index. So prepare `mask_tmp` accordingly.*/
    const lv_opa_t* mask_tmp = mask - draw_area->x1;

    /*Buffer the result color to avoid recalculating the same color*/
    lv_color_t last_dest_color;
    lv_color_t last_res_color;
    lv_opa_t last_mask = LV_OPA_TRANSP;
    last_dest_color.full = disp_buf_tmp[0].full;
    last_res_color.full = disp_buf_tmp[0].full;

    for (y = draw_area->y1; y <= draw_area->y2; y++) {
      for (x = draw_area->x1; x <= draw_area->x2; x++) {
        if (mask_tmp[x] == 0)
          continue;
        if (mask_tmp[x] != last_mask || last_dest_color.full != disp_buf_tmp[x].full) {
          lv_opa_t opa_tmp = mask_tmp[x] >= LV_OPA_MAX ? opa : (uint32_t)((uint32_t)mask_tmp[x] * opa) >> 8;

          last_res_color = blend_fp(color, disp_buf_tmp[x], opa_tmp);
          last_mask = mask_tmp[x];
          last_dest_color.full = disp_buf_tmp[x].full;
        }
        disp_buf_tmp[x] = last_res_color;
      }
      disp_buf_tmp += disp_w;
      mask_tmp += draw_area_w;
    }
  }
}
#endif

static void map_set_px(const lv_area_t* disp_area, lv_color_t* disp_buf, const lv_area_t* draw_area,
    const lv_area_t* map_area, const lv_color_t* map_buf, lv_opa_t opa,
    const lv_opa_t* mask, lv_draw_mask_res_t mask_res)

{
  lv_disp_t* disp = _lv_refr_get_disp_refreshing();

  /*Get the width of the `disp_area` it will be used to go to the next line*/
  int32_t disp_w = lv_area_get_width(disp_area);

  /*Get the width of the `draw_area` it will be used to go to the next line of the mask*/
  int32_t draw_area_w = lv_area_get_width(draw_area);

  /*Get the width of the `mask_area` it will be used to go to the next line*/
  int32_t map_w = lv_area_get_width(map_area);

  /*Create a temp. map_buf which always point to current line to draw*/
  const lv_color_t* map_buf_tmp = map_buf + map_w * (draw_area->y1 - (map_area->y1 - disp_area->y1));

  map_buf_tmp += (draw_area->x1 - (map_area->x1 - disp_area->x1));
  map_buf_tmp -= draw_area->x1;
  int32_t x;
  int32_t y;

  if (mask_res == LV_DRAW_MASK_RES_FULL_COVER) {
    for (y = draw_area->y1; y <= draw_area->y2; y++) {
      for (x = draw_area->x1; x <= draw_area->x2; x++) {
        disp->driver->set_px_cb(disp->driver, (void*)disp_buf, disp_w, x, y, map_buf_tmp[x], opa);
      }
      map_buf_tmp += map_w;
    }
  } else {
    /*The mask is relative to the clipped area.
         *In the cycles below mask will be indexed from `draw_area.x1`
         *but it corresponds to zero index. So prepare `mask_tmp` accordingly.*/
    const lv_opa_t* mask_tmp = mask - draw_area->x1;

    for (y = draw_area->y1; y <= draw_area->y2; y++) {
      for (x = draw_area->x1; x <= draw_area->x2; x++) {
        if (mask_tmp[x]) {
          disp->driver->set_px_cb(disp->driver, (void*)disp_buf, disp_w, x, y, map_buf_tmp[x],
              (uint32_t)((uint32_t)opa * mask_tmp[x]) >> 8);
        }
      }
      mask_tmp += draw_area_w;
      map_buf_tmp += map_w;
    }
  }
}

/**
 * Copy an image to an area
 * @param disp_area the current display area (destination area)
 * @param disp_buf destination buffer
 * @param map_area coordinates of the map (image) to copy. (absolute coordinates)
 * @param map_buf the pixel of the image
 * @param opa overall opacity in 0x00..0xff range
 * @param mask a mask to apply on every pixel (uint8_t array with 0x00..0xff values).
 *                It fits into draw_area.
 * @param mask_res LV_MASK_RES_COVER: the mask has only 0xff values (no mask),
 *                 LV_MASK_RES_TRANSP: the mask has only 0x00 values (full transparent),
 *                 LV_MASK_RES_CHANGED: the mask has mixed values
 */
LV_ATTRIBUTE_FAST_MEM static void map_normal(const lv_area_t* disp_area, lv_color_t* disp_buf,
    const lv_area_t* draw_area,
    const lv_area_t* map_area, const lv_color_t* map_buf, lv_opa_t opa,
    const lv_opa_t* mask, lv_draw_mask_res_t mask_res)
{

  /*Get the width of the `disp_area` it will be used to go to the next line*/
  int32_t disp_w = lv_area_get_width(disp_area);

  int32_t draw_area_w = lv_area_get_width(draw_area);
  int32_t draw_area_h = lv_area_get_height(draw_area);

  /*Get the width of the `mask_area` it will be used to go to the next line*/
  int32_t map_w = lv_area_get_width(map_area);

  /*Create a temp. disp_buf which always point to first pixel to draw*/
  lv_color_t* disp_buf_first = disp_buf + disp_w * draw_area->y1 + draw_area->x1;

  /*Create a temp. map_buf which always point to first pixel to draw from the map*/
  const lv_color_t* map_buf_first = map_buf + map_w * (draw_area->y1 - (map_area->y1 - disp_area->y1));
  map_buf_first += (draw_area->x1 - (map_area->x1 - disp_area->x1));

#if LV_COLOR_SCREEN_TRANSP
  lv_disp_t* disp = _lv_refr_get_disp_refreshing();
#endif

  int32_t x;
  int32_t y;

  /*Simple fill (maybe with opacity), no masking*/
  if (mask_res == LV_DRAW_MASK_RES_FULL_COVER) {
    if (opa > LV_OPA_MAX) {
#if LV_USE_GPU_NXP_PXP
      if (lv_area_get_size(draw_area) >= LV_GPU_NXP_PXP_BLIT_SIZE_LIMIT) {
        lv_gpu_nxp_pxp_blit(disp_buf_first, disp_w, map_buf_first, map_w, draw_area_w, draw_area_h, opa);
        return;
      }
#elif (LV_USE_GPU_NXP_VG_LITE)
      if (lv_area_get_size(draw_area) >= LV_GPU_NXP_VG_LITE_BLIT_SIZE_LIMIT) {

        lv_gpu_nxp_vglite_blit_info_t blit;

        blit.src = map_buf;
        blit.src_width = draw_area_w;
        blit.src_height = draw_area_h;
        blit.src_stride = lv_area_get_width(map_area) * sizeof(lv_color_t);
        blit.src_area.x1 = (draw_area->x1 - (map_area->x1 - disp_area->x1));
        blit.src_area.y1 = (draw_area->y1 - (map_area->y1 - disp_area->y1));
        blit.src_area.x2 = blit.src_area.x1 + draw_area_w;
        blit.src_area.y2 = blit.src_area.y1 + draw_area_h;

        blit.dst = disp_buf;
        blit.dst_width = lv_area_get_width(disp_area);
        blit.dst_height = lv_area_get_height(disp_area);
        blit.dst_stride = lv_area_get_width(disp_area) * sizeof(lv_color_t);
        blit.dst_area.x1 = draw_area->x1;
        blit.dst_area.y1 = draw_area->y1;
        blit.dst_area.x2 = blit.dst_area.x1 + draw_area_w;
        blit.dst_area.y2 = blit.dst_area.y1 + draw_area_h;

        blit.opa = opa;

        if (lv_gpu_nxp_vglite_blit(&blit) == LV_RES_OK) {
          return;
        }
        /*Fall down to SW render in case of error*/
      }
#elif LV_USE_GPU_STM32_DMA2D
      if (lv_area_get_size(draw_area) >= 240) {
        lv_gpu_stm32_dma2d_copy(disp_buf_first, disp_w, map_buf_first, map_w, draw_area_w, draw_area_h);
        return;
      }
#endif

      /*Software rendering*/
      for (y = 0; y < draw_area_h; y++) {
        lv_memcpy(disp_buf_first, map_buf_first, draw_area_w * sizeof(lv_color_t));
        disp_buf_first += disp_w;
        map_buf_first += map_w;
      }
    } else {
#if LV_USE_GPU_NXP_PXP
      if (lv_area_get_size(draw_area) >= LV_GPU_NXP_PXP_BLIT_OPA_SIZE_LIMIT) {
        lv_gpu_nxp_pxp_blit(disp_buf_first, disp_w, map_buf_first, map_w, draw_area_w, draw_area_h, opa);
        return;
      }
#elif (LV_USE_GPU_NXP_VG_LITE)
      if (lv_area_get_size(draw_area) >= LV_GPU_NXP_VG_LITE_BLIT_OPA_SIZE_LIMIT) {

        lv_gpu_nxp_vglite_blit_info_t blit;

        blit.src = map_buf;
        blit.src_width = lv_area_get_width(map_area);
        blit.src_height = lv_area_get_height(map_area);
        blit.src_stride = lv_area_get_width(map_area) * sizeof(lv_color_t);
        blit.src_area.x1 = (draw_area->x1 - (map_area->x1 - disp_area->x1));
        blit.src_area.y1 = (draw_area->y1 - (map_area->y1 - disp_area->y1));
        blit.src_area.x2 = blit.src_area.x1 + draw_area_w;
        blit.src_area.y2 = blit.src_area.y1 + draw_area_h;

        blit.dst = disp_buf;
        blit.dst_width = lv_area_get_width(disp_area);
        blit.dst_height = lv_area_get_height(disp_area);
        blit.dst_stride = lv_area_get_width(disp_area) * sizeof(lv_color_t);
        blit.dst_area.x1 = draw_area->x1;
        blit.dst_area.y1 = draw_area->y1;
        blit.dst_area.x2 = blit.dst_area.x1 + draw_area_w;
        blit.dst_area.y2 = blit.dst_area.y1 + draw_area_h;

        blit.opa = opa;

        if (lv_gpu_nxp_vglite_blit(&blit) == LV_RES_OK) {
          return;
        }
        /*Fall down to SW render in case of error*/
      }
#elif LV_USE_GPU_STM32_DMA2D
      if (lv_area_get_size(draw_area) >= 240) {
        lv_gpu_stm32_dma2d_blend(disp_buf_first, disp_w, map_buf_first, opa, map_w, draw_area_w, draw_area_h);
        return;
      }
#endif

      /*Software rendering*/

      for (y = 0; y < draw_area_h; y++) {
        for (x = 0; x < draw_area_w; x++) {
#if LV_COLOR_SCREEN_TRANSP
          if (disp->driver->screen_transp) {
            lv_color_mix_with_alpha(disp_buf_first[x], disp_buf_first[x].ch.alpha, map_buf_first[x], opa, &disp_buf_first[x],
                &disp_buf_first[x].ch.alpha);
          } else
#endif
          {
            disp_buf_first[x] = lv_color_mix(map_buf_first[x], disp_buf_first[x], opa);
          }
        }
        disp_buf_first += disp_w;
        map_buf_first += map_w;
      }
    }
  }
  /*Masked*/
  else {
    /*Only the mask matters*/
    if (opa > LV_OPA_MAX) {
      /*Go to the first pixel of the row*/

      int32_t x_end4 = draw_area_w - 4;

      for (y = 0; y < draw_area_h; y++) {
        const lv_opa_t* mask_tmp_x = mask;
#if 0
                for(x = 0; x < draw_area_w; x++) {
                    MAP_NORMAL_MASK_PX(x);
                }
#else
        for (x = 0; x < draw_area_w && ((lv_uintptr_t)mask_tmp_x & 0x3); x++) {
#if LV_COLOR_SCREEN_TRANSP
          MAP_NORMAL_MASK_PX_SCR_TRANSP(x)
#else
          MAP_NORMAL_MASK_PX(x)
#endif
        }

        uint32_t* mask32 = (uint32_t*)mask_tmp_x;
        for (; x < x_end4; x += 4) {
          if (*mask32) {
            if ((*mask32) == 0xFFFFFFFF) {
              disp_buf_first[x] = map_buf_first[x];
              disp_buf_first[x + 1] = map_buf_first[x + 1];
              disp_buf_first[x + 2] = map_buf_first[x + 2];
              disp_buf_first[x + 3] = map_buf_first[x + 3];
            } else {
              mask_tmp_x = (const lv_opa_t*)mask32;
#if LV_COLOR_SCREEN_TRANSP
              MAP_NORMAL_MASK_PX_SCR_TRANSP(x)
              MAP_NORMAL_MASK_PX_SCR_TRANSP(x + 1)
              MAP_NORMAL_MASK_PX_SCR_TRANSP(x + 2)
              MAP_NORMAL_MASK_PX_SCR_TRANSP(x + 3)
#else
              MAP_NORMAL_MASK_PX(x)
              MAP_NORMAL_MASK_PX(x + 1)
              MAP_NORMAL_MASK_PX(x + 2)
              MAP_NORMAL_MASK_PX(x + 3)
#endif
            }
          }
          mask32++;
        }

        mask_tmp_x = (const lv_opa_t*)mask32;
        for (; x < draw_area_w; x++) {
#if LV_COLOR_SCREEN_TRANSP
          MAP_NORMAL_MASK_PX_SCR_TRANSP(x)
#else
          MAP_NORMAL_MASK_PX(x)
#endif
        }
#endif
        disp_buf_first += disp_w;
        mask += draw_area_w;
        map_buf_first += map_w;
      }
    }
    /*Handle opa and mask values too*/
    else {
      for (y = 0; y < draw_area_h; y++) {
        for (x = 0; x < draw_area_w; x++) {
          if (mask[x]) {
            lv_opa_t opa_tmp = mask[x] >= LV_OPA_MAX ? opa : ((opa * mask[x]) >> 8);
#if LV_COLOR_SCREEN_TRANSP
            if (disp->driver->screen_transp) {
              lv_color_mix_with_alpha(disp_buf_first[x], disp_buf_first[x].ch.alpha, map_buf_first[x], opa_tmp, &disp_buf_first[x],
                  &disp_buf_first[x].ch.alpha);
            } else
#endif
            {
              disp_buf_first[x] = lv_color_mix(map_buf_first[x], disp_buf_first[x], opa_tmp);
            }
          }
        }
        disp_buf_first += disp_w;
        mask += draw_area_w;
        map_buf_first += map_w;
      }
    }
  }
}
#if LV_DRAW_COMPLEX
static void map_blended(const lv_area_t* disp_area, lv_color_t* disp_buf, const lv_area_t* draw_area,
    const lv_area_t* map_area, const lv_color_t* map_buf, lv_opa_t opa,
    const lv_opa_t* mask, lv_draw_mask_res_t mask_res, lv_blend_mode_t mode)
{

  /*Get the width of the `disp_area` it will be used to go to the next line*/
  int32_t disp_w = lv_area_get_width(disp_area);

  /*Get the width of the `draw_area` it will be used to go to the next line of the mask*/
  int32_t draw_area_w = lv_area_get_width(draw_area);

  /*Get the width of the `mask_area` it will be used to go to the next line*/
  int32_t map_w = lv_area_get_width(map_area);

  /*Create a temp. disp_buf which always point to current line to draw*/
  lv_color_t* disp_buf_tmp = disp_buf + disp_w * draw_area->y1;

  /*Create a temp. map_buf which always point to current line to draw*/
  const lv_color_t* map_buf_tmp = map_buf + map_w * (draw_area->y1 - (map_area->y1 - disp_area->y1));

  lv_color_t (*blend_fp)(lv_color_t, lv_color_t, lv_opa_t);
  switch (mode) {
  case LV_BLEND_MODE_ADDITIVE:
    blend_fp = color_blend_true_color_additive;
    break;
  case LV_BLEND_MODE_SUBTRACTIVE:
    blend_fp = color_blend_true_color_subtractive;
    break;
  default:
    LV_LOG_WARN("fill_blended: unsupported blend mode");
    return;
  }

  int32_t x;
  int32_t y;

  /*Simple fill (maybe with opacity), no masking*/
  if (mask_res == LV_DRAW_MASK_RES_FULL_COVER) {
    /*Go to the first px of the row*/
    map_buf_tmp += (draw_area->x1 - (map_area->x1 - disp_area->x1));

    /*The map will be indexed from `draw_area->x1` so compensate it.*/
    map_buf_tmp -= draw_area->x1;

    for (y = draw_area->y1; y <= draw_area->y2; y++) {
      for (x = draw_area->x1; x <= draw_area->x2; x++) {
        disp_buf_tmp[x] = blend_fp(map_buf_tmp[x], disp_buf_tmp[x], opa);
      }
      disp_buf_tmp += disp_w;
      map_buf_tmp += map_w;
    }
  }
  /*Masked*/
  else {
    /*The mask is relative to the clipped area.
         *In the cycles below mask will be indexed from `draw_area.x1`
         *but it corresponds to zero index. So prepare `mask_tmp` accordingly.*/
    const lv_opa_t* mask_tmp = mask - draw_area->x1;

    map_buf_tmp -= draw_area->x1;
    for (y = draw_area->y1; y <= draw_area->y2; y++) {
      for (x = draw_area->x1; x <= draw_area->x2; x++) {
        if (mask_tmp[x] == 0)
          continue;
        lv_opa_t opa_tmp = mask_tmp[x] >= LV_OPA_MAX ? opa : ((opa * mask_tmp[x]) >> 8);
        disp_buf_tmp[x] = blend_fp(map_buf_tmp[x], disp_buf_tmp[x], opa_tmp);
      }
      disp_buf_tmp += disp_w;
      mask_tmp += draw_area_w;
      map_buf_tmp += map_w;
    }
  }
}

static inline lv_color_t color_blend_true_color_additive(lv_color_t fg, lv_color_t bg, lv_opa_t opa)
{

  if (opa <= LV_OPA_MIN)
    return bg;

  uint32_t tmp;
#if LV_COLOR_DEPTH == 1
  tmp = bg.full + fg.full;
  fg.full = LV_MIN(tmp, 1);
#else
  tmp = bg.ch.red + fg.ch.red;
#if LV_COLOR_DEPTH == 8
  fg.ch.red = LV_MIN(tmp, 7);
#elif LV_COLOR_DEPTH == 16
  fg.ch.red = LV_MIN(tmp, 31);
#elif LV_COLOR_DEPTH == 32
  fg.ch.red = LV_MIN(tmp, 255);
#endif

#if LV_COLOR_DEPTH == 8
  tmp = bg.ch.green + fg.ch.green;
  fg.ch.green = LV_MIN(tmp, 7);
#elif LV_COLOR_DEPTH == 16
#if LV_COLOR_16_SWAP == 0
  tmp = bg.ch.green + fg.ch.green;
  fg.ch.green = LV_MIN(tmp, 63);
#else
  tmp = (bg.ch.green_h << 3) + bg.ch.green_l + (fg.ch.green_h << 3) + fg.ch.green_l;
  tmp = LV_MIN(tmp, 63);
  fg.ch.green_h = tmp >> 3;
  fg.ch.green_l = tmp & 0x7;
#endif

#elif LV_COLOR_DEPTH == 32
  tmp = bg.ch.green + fg.ch.green;
  fg.ch.green = LV_MIN(tmp, 255);
#endif

  tmp = bg.ch.blue + fg.ch.blue;
#if LV_COLOR_DEPTH == 8
  fg.ch.blue = LV_MIN(tmp, 4);
#elif LV_COLOR_DEPTH == 16
  fg.ch.blue = LV_MIN(tmp, 31);
#elif LV_COLOR_DEPTH == 32
  fg.ch.blue = LV_MIN(tmp, 255);
#endif
#endif

  if (opa == LV_OPA_COVER)
    return fg;

  return lv_color_mix(fg, bg, opa);
}

static inline lv_color_t color_blend_true_color_subtractive(lv_color_t fg, lv_color_t bg, lv_opa_t opa)
{

  if (opa <= LV_OPA_MIN)
    return bg;

  int32_t tmp;
  tmp = bg.ch.red - fg.ch.red;
  fg.ch.red = LV_MAX(tmp, 0);

#if LV_COLOR_16_SWAP == 0
  tmp = bg.ch.green - fg.ch.green;
  fg.ch.green = LV_MAX(tmp, 0);
#else
  tmp = (bg.ch.green_h << 3) + bg.ch.green_l + (fg.ch.green_h << 3) + fg.ch.green_l;
  tmp = LV_MAX(tmp, 0);
  fg.ch.green_h = tmp >> 3;
  fg.ch.green_l = tmp & 0x7;
#endif

  tmp = bg.ch.blue - fg.ch.blue;
  fg.ch.blue = LV_MAX(tmp, 0);

  if (opa == LV_OPA_COVER)
    return fg;

  return lv_color_mix(fg, bg, opa);
}
#endif

/* Copied from lv_draw_rect.c */

LV_ATTRIBUTE_FAST_MEM static void draw_bg(const lv_area_t* coords, const lv_area_t* clip_area,
    const lv_draw_rect_dsc_t* dsc)
{
  if (dsc->bg_opa <= LV_OPA_MIN)
    return;

  lv_area_t coords_bg;
  lv_area_copy(&coords_bg, coords);

  /*If the border fully covers make the bg area 1px smaller to avoid artifacts on the corners*/
  if (dsc->border_width > 1 && dsc->border_opa >= LV_OPA_MAX && dsc->radius != 0) {
    coords_bg.x1 += (dsc->border_side & LV_BORDER_SIDE_LEFT) ? 1 : 0;
    coords_bg.y1 += (dsc->border_side & LV_BORDER_SIDE_TOP) ? 1 : 0;
    coords_bg.x2 -= (dsc->border_side & LV_BORDER_SIDE_RIGHT) ? 1 : 0;
    coords_bg.y2 -= (dsc->border_side & LV_BORDER_SIDE_BOTTOM) ? 1 : 0;
  }

  lv_opa_t opa = dsc->bg_opa >= LV_OPA_MAX ? LV_OPA_COVER : dsc->bg_opa;
  lv_grad_dir_t grad_dir = dsc->bg_grad_dir;
  if (dsc->bg_color.full == dsc->bg_grad_color.full)
    grad_dir = LV_GRAD_DIR_NONE;

  bool mask_any = lv_draw_mask_is_any(&coords_bg);

  /*Most simple case: just a plain rectangle*/
  if (!mask_any && dsc->radius == 0 && (grad_dir == LV_GRAD_DIR_NONE)) {
    _lv_blend_fill(clip_area, &coords_bg, dsc->bg_color, NULL,
        LV_DRAW_MASK_RES_FULL_COVER, opa, dsc->blend_mode);
    return;
  }

  /*Complex case: there is gradient, mask, or radius*/
#if LV_DRAW_COMPLEX == 0
  LV_LOG_WARN("Can't draw complex rectangle because LV_DRAW_COMPLEX = 0");
#else
  /*Get clipped fill area which is the real draw area.
     *It is always the same or inside `fill_area`*/
  lv_area_t draw_area;
  if (!_lv_area_intersect(&draw_area, &coords_bg, clip_area))
    return;

  /*Get the real radius. Can't be larger than the half of the shortest side */
  lv_coord_t coords_w = lv_area_get_width(&coords_bg);
  lv_coord_t coords_h = lv_area_get_height(&coords_bg);
  int32_t short_side = LV_MIN(coords_w, coords_h);
  int32_t rout = LV_MIN(dsc->radius, short_side >> 1);

  /*Add a radius mask if there is radius*/
  int32_t draw_area_w = lv_area_get_width(&draw_area);
  int16_t mask_rout_id = LV_MASK_ID_INV;
  lv_opa_t* mask_buf = NULL;
  lv_draw_mask_radius_param_t mask_rout_param;
  if (rout > 0 || mask_any) {
    mask_buf = lv_mem_buf_get(draw_area_w);
    lv_draw_mask_radius_init(&mask_rout_param, &coords_bg, rout, false);
    mask_rout_id = lv_draw_mask_add(&mask_rout_param, NULL);
  }

  /*In case of horizontal gradient pre-compute a line with a gradient*/
  lv_color_t* grad_map = NULL;
  if (grad_dir == LV_GRAD_DIR_HOR) {
    grad_map = lv_mem_buf_get(coords_w * sizeof(lv_color_t));
    int32_t i;
    for (i = 0; i < coords_w; i++) {
      grad_map[i] = grad_get(dsc, coords_w, i - coords_bg.x1);
    }
  }

  int32_t h;
  lv_draw_mask_res_t mask_res;
  lv_area_t blend_area;
  blend_area.x1 = draw_area.x1;
  blend_area.x2 = draw_area.x2;

  /*There is another mask too. Draw line by line. */
  if (mask_any) {
    for (h = draw_area.y1; h <= draw_area.y2; h++) {
      blend_area.y1 = h;
      blend_area.y2 = h;

      /* Initialize the mask to opa instead of 0xFF and blend with LV_OPA_COVER.
             * It saves calculating the final opa in _lv_blend_fill*/
      lv_memset(mask_buf, opa, draw_area_w);
      mask_res = lv_draw_mask_apply(mask_buf, draw_area.x1, h, draw_area_w);
      if (mask_res == LV_DRAW_MASK_RES_FULL_COVER)
        mask_res = LV_DRAW_MASK_RES_CHANGED;

      if (grad_dir == LV_GRAD_DIR_NONE) {
        _lv_blend_fill(clip_area, &blend_area, dsc->bg_color, mask_buf, mask_res, LV_OPA_COVER, dsc->blend_mode);
      } else if (grad_dir == LV_GRAD_DIR_HOR) {
        _lv_blend_map(clip_area, &blend_area, grad_map, mask_buf, mask_res, LV_OPA_COVER, dsc->blend_mode);
      } else if (grad_dir == LV_GRAD_DIR_VER) {
        lv_color_t c = grad_get(dsc, coords_h, h - coords_bg.y1);
        _lv_blend_fill(clip_area, &blend_area, c, mask_buf, mask_res, LV_OPA_COVER, dsc->blend_mode);
      }
    }
    goto bg_clean_up;
  }

  /* Draw the top of the rectangle line by line and mirror it to the bottom.
     * If there is no radius this cycle won't run because `h` is always `>= h_end`*/
  blend_area.x1 = draw_area.x1;
  blend_area.x2 = draw_area.x2;
  for (h = 0; h < rout; h++) {
    lv_coord_t top_y = coords_bg.y1 + h;
    lv_coord_t bottom_y = coords_bg.y2 - h;
    if (top_y < draw_area.y1 && bottom_y > draw_area.y2)
      continue; /*This line is clipped now*/

    /* Initialize the mask to opa instead of 0xFF and blend with LV_OPA_COVER.
         * It saves calculating the final opa in _lv_blend_fill*/
    lv_memset(mask_buf, opa, draw_area_w);
    mask_res = lv_draw_mask_apply(mask_buf, blend_area.x1, top_y, draw_area_w);

    if (top_y >= draw_area.y1) {
      blend_area.y1 = top_y;
      blend_area.y2 = top_y;

      if (grad_dir == LV_GRAD_DIR_NONE) {
        _lv_blend_fill(clip_area, &blend_area, dsc->bg_color, mask_buf, mask_res, LV_OPA_COVER, dsc->blend_mode);
      } else if (grad_dir == LV_GRAD_DIR_HOR) {
        _lv_blend_map(clip_area, &blend_area, grad_map, mask_buf, mask_res, LV_OPA_COVER, dsc->blend_mode);
      } else if (grad_dir == LV_GRAD_DIR_VER) {
        lv_color_t c = grad_get(dsc, coords_h, top_y - coords_bg.y1);
        _lv_blend_fill(clip_area, &blend_area, c, mask_buf, mask_res, LV_OPA_COVER, dsc->blend_mode);
      }
    }

    if (bottom_y <= draw_area.y2) {
      blend_area.y1 = bottom_y;
      blend_area.y2 = bottom_y;

      if (grad_dir == LV_GRAD_DIR_NONE) {
        _lv_blend_fill(clip_area, &blend_area, dsc->bg_color, mask_buf, mask_res, LV_OPA_COVER, dsc->blend_mode);
      } else if (grad_dir == LV_GRAD_DIR_HOR) {
        _lv_blend_map(clip_area, &blend_area, grad_map, mask_buf, mask_res, LV_OPA_COVER, dsc->blend_mode);
      } else if (grad_dir == LV_GRAD_DIR_VER) {
        lv_color_t c = grad_get(dsc, coords_h, bottom_y - coords_bg.y1);
        _lv_blend_fill(clip_area, &blend_area, c, mask_buf, mask_res, LV_OPA_COVER, dsc->blend_mode);
      }
    }
  }

  /* Draw the center of the rectangle.*/

  /*If no other masks and no gradient, the center is a simple rectangle*/
  if (!mask_any && grad_dir == LV_GRAD_DIR_NONE) {
    blend_area.y1 = coords_bg.y1 + rout;
    blend_area.y2 = coords_bg.y2 - rout;
    _lv_blend_fill(clip_area, &blend_area, dsc->bg_color, mask_buf, LV_DRAW_MASK_RES_FULL_COVER, opa, dsc->blend_mode);
  }
  /*With gradient and/or mask draw line by line*/
  else {
    mask_res = LV_DRAW_MASK_RES_FULL_COVER;
    int32_t h_end = coords_bg.y2 - rout;
    for (h = coords_bg.y1 + rout; h <= h_end; h++) {
      /*If there is no other mask do not apply mask as in the center there is no radius to mask*/
      if (mask_any) {
        lv_memset_ff(mask_buf, draw_area_w);
        mask_res = lv_draw_mask_apply(mask_buf, draw_area.x1, h, draw_area_w);
      }

      blend_area.y1 = h;
      blend_area.y2 = h;
      if (grad_dir == LV_GRAD_DIR_NONE) {
        _lv_blend_fill(clip_area, &blend_area, dsc->bg_color, mask_buf, mask_res, opa, dsc->blend_mode);
      } else if (grad_dir == LV_GRAD_DIR_HOR) {
        _lv_blend_map(clip_area, &blend_area, grad_map, mask_buf, mask_res, opa, dsc->blend_mode);
      } else if (grad_dir == LV_GRAD_DIR_VER) {
        lv_color_t c = grad_get(dsc, coords_h, h - coords_bg.y1);
        _lv_blend_fill(clip_area, &blend_area, c, mask_buf, mask_res, opa, dsc->blend_mode);
      }
    }
  }

bg_clean_up:
  if (grad_map)
    lv_mem_buf_release(grad_map);
  if (mask_buf)
    lv_mem_buf_release(mask_buf);
  if (mask_rout_id != LV_MASK_ID_INV) {
    lv_draw_mask_remove_id(mask_rout_id);
    lv_draw_mask_free_param(&mask_rout_param);
  }

#endif
}

LV_ATTRIBUTE_FAST_MEM static void draw_bg_img(const lv_area_t* coords, const lv_area_t* clip,
    const lv_draw_rect_dsc_t* dsc)
{
  if (dsc->bg_img_src == NULL)
    return;
  if (dsc->bg_img_opa <= LV_OPA_MIN)
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
    lv_draw_label(&a, clip, &label_draw_dsc, dsc->bg_img_src, NULL);
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

      lv_draw_img(&area, clip, dsc->bg_img_src, &img_dsc);
    } else {
      lv_area_t area;
      area.y1 = coords->y1;
      area.y2 = area.y1 + header.h - 1;

      for (; area.y1 <= coords->y2; area.y1 += header.h, area.y2 += header.h) {

        area.x1 = coords->x1;
        area.x2 = area.x1 + header.w - 1;
        for (; area.x1 <= coords->x2; area.x1 += header.w, area.x2 += header.w) {
          lv_draw_img(&area, clip, dsc->bg_img_src, &img_dsc);
        }
      }
    }
  }
}

LV_ATTRIBUTE_FAST_MEM static void draw_border(const lv_area_t* coords, const lv_area_t* clip,
    const lv_draw_rect_dsc_t* dsc)
{
  if (dsc->border_opa <= LV_OPA_MIN)
    return;
  if (dsc->border_width == 0)
    return;
  if (dsc->border_side == LV_BORDER_SIDE_NONE)
    return;
  if (dsc->border_post)
    return;

  int32_t coords_w = lv_area_get_width(coords);
  int32_t coords_h = lv_area_get_height(coords);
  int32_t rout = dsc->radius;
  int32_t short_side = LV_MIN(coords_w, coords_h);
  if (rout > short_side >> 1)
    rout = short_side >> 1;

  /*Get the inner area*/
  lv_area_t area_inner;
  lv_area_copy(&area_inner, coords);
  area_inner.x1 += ((dsc->border_side & LV_BORDER_SIDE_LEFT) ? dsc->border_width : -(dsc->border_width + rout));
  area_inner.x2 -= ((dsc->border_side & LV_BORDER_SIDE_RIGHT) ? dsc->border_width : -(dsc->border_width + rout));
  area_inner.y1 += ((dsc->border_side & LV_BORDER_SIDE_TOP) ? dsc->border_width : -(dsc->border_width + rout));
  area_inner.y2 -= ((dsc->border_side & LV_BORDER_SIDE_BOTTOM) ? dsc->border_width : -(dsc->border_width + rout));

  lv_coord_t rin = rout - dsc->border_width;
  if (rin < 0)
    rin = 0;

  draw_border_generic(clip, coords, &area_inner, rout, rin, dsc->border_color, dsc->border_opa, dsc->blend_mode);
}

#if LV_DRAW_COMPLEX
LV_ATTRIBUTE_FAST_MEM static inline lv_color_t grad_get(const lv_draw_rect_dsc_t* dsc, lv_coord_t s, lv_coord_t i)
{
  int32_t min = (dsc->bg_main_color_stop * s) >> 8;
  if (i <= min)
    return dsc->bg_color;

  int32_t max = (dsc->bg_grad_color_stop * s) >> 8;
  if (i >= max)
    return dsc->bg_grad_color;

  int32_t d = dsc->bg_grad_color_stop - dsc->bg_main_color_stop;
  d = (s * d) >> 8;
  i -= min;
  lv_opa_t mix = (i * 255) / d;
  return lv_color_mix(dsc->bg_grad_color, dsc->bg_color, mix);
}

LV_ATTRIBUTE_FAST_MEM static void draw_shadow(const lv_area_t* coords, const lv_area_t* clip,
    const lv_draw_rect_dsc_t* dsc)
{
  /*Check whether the shadow is visible*/
  if (dsc->shadow_width == 0)
    return;
  if (dsc->shadow_opa <= LV_OPA_MIN)
    return;

  if (dsc->shadow_width == 1 && dsc->shadow_spread <= 0 && dsc->shadow_ofs_x == 0 && dsc->shadow_ofs_y == 0) {
    return;
  }

  /*Calculate the rectangle which is blurred to get the shadow in `shadow_area`*/
  lv_area_t core_area;
  core_area.x1 = coords->x1 + dsc->shadow_ofs_x - dsc->shadow_spread;
  core_area.x2 = coords->x2 + dsc->shadow_ofs_x + dsc->shadow_spread;
  core_area.y1 = coords->y1 + dsc->shadow_ofs_y - dsc->shadow_spread;
  core_area.y2 = coords->y2 + dsc->shadow_ofs_y + dsc->shadow_spread;

  /*Calculate the bounding box of the shadow*/
  lv_area_t shadow_area;
  shadow_area.x1 = core_area.x1 - dsc->shadow_width / 2 - 1;
  shadow_area.x2 = core_area.x2 + dsc->shadow_width / 2 + 1;
  shadow_area.y1 = core_area.y1 - dsc->shadow_width / 2 - 1;
  shadow_area.y2 = core_area.y2 + dsc->shadow_width / 2 + 1;

  lv_opa_t opa = dsc->shadow_opa;
  if (opa > LV_OPA_MAX)
    opa = LV_OPA_COVER;

  /*Get clipped draw area which is the real draw area.
     *It is always the same or inside `shadow_area`*/
  lv_area_t draw_area;
  if (!_lv_area_intersect(&draw_area, &shadow_area, clip))
    return;

  /*Consider 1 px smaller bg to be sure the edge will be covered by the shadow*/
  lv_area_t bg_area;
  lv_area_copy(&bg_area, coords);
  lv_area_increase(&bg_area, -1, -1);

  /*Get the clamped radius*/
  int32_t r_bg = dsc->radius;
  lv_coord_t short_side = LV_MIN(lv_area_get_width(&bg_area), lv_area_get_height(&bg_area));
  if (r_bg > short_side >> 1)
    r_bg = short_side >> 1;

  /*Get the clamped radius*/
  int32_t r_sh = dsc->radius;
  short_side = LV_MIN(lv_area_get_width(&core_area), lv_area_get_height(&core_area));
  if (r_sh > short_side >> 1)
    r_sh = short_side >> 1;

  /*Get how many pixels are affected by the blur on the corners*/
  int32_t corner_size = dsc->shadow_width + r_sh;

  lv_opa_t* sh_buf;

#if LV_SHADOW_CACHE_SIZE
  if (sh_cache_size == corner_size && sh_cache_r == r_sh) {
    /*Use the cache if available*/
    sh_buf = lv_mem_buf_get(corner_size * corner_size);
    lv_memcpy(sh_buf, sh_cache, corner_size * corner_size);
  } else {
    /*A larger buffer is required for calculation*/
    sh_buf = lv_mem_buf_get(corner_size * corner_size * sizeof(uint16_t));
    shadow_draw_corner_buf(&core_area, (uint16_t*)sh_buf, dsc->shadow_width, r_sh);

    /*Cache the corner if it fits into the cache size*/
    if ((uint32_t)corner_size * corner_size < sizeof(sh_cache)) {
      lv_memcpy(sh_cache, sh_buf, corner_size * corner_size);
      sh_cache_size = corner_size;
      sh_cache_r = r_sh;
    }
  }
#else
  sh_buf = lv_mem_buf_get(corner_size * corner_size * sizeof(uint16_t));
  shadow_draw_corner_buf(&core_area, (uint16_t*)sh_buf, dsc->shadow_width, r_sh);
#endif

  /*Skip a lot of masking if the background will cover the shadow that would be masked out*/
  bool mask_any = lv_draw_mask_is_any(&shadow_area);
  bool simple = true;
  if (mask_any || dsc->bg_opa < LV_OPA_COVER)
    simple = false;

  /*Create a radius mask to clip remove shadow on the bg area*/
  lv_draw_mask_res_t mask_res;

  lv_draw_mask_radius_param_t mask_rout_param;
  int16_t mask_rout_id = LV_MASK_ID_INV;
  if (!simple) {
    lv_draw_mask_radius_init(&mask_rout_param, &bg_area, r_bg, true);
    mask_rout_id = lv_draw_mask_add(&mask_rout_param, NULL);
  }
  lv_opa_t* mask_buf = lv_mem_buf_get(lv_area_get_width(&shadow_area));
  lv_area_t blend_area;
  lv_area_t clip_area_sub;
  lv_opa_t** mask_act;
  lv_opa_t* sh_buf_tmp;
  lv_coord_t y;
  bool simple_sub;

  lv_coord_t w_half = shadow_area.x1 + lv_area_get_width(&shadow_area) / 2;
  lv_coord_t h_half = shadow_area.y1 + lv_area_get_height(&shadow_area) / 2;

  /*Draw the corners if they are on the current clip area and not fully covered by the bg*/

  /*Top right corner*/
  blend_area.x2 = shadow_area.x2;
  blend_area.x1 = shadow_area.x2 - corner_size + 1;
  blend_area.y1 = shadow_area.y1;
  blend_area.y2 = shadow_area.y1 + corner_size - 1;
  /*Do not overdraw the top other corners*/
  blend_area.x1 = LV_MAX(blend_area.x1, w_half);
  blend_area.y2 = LV_MIN(blend_area.y2, h_half);

  if (_lv_area_intersect(&clip_area_sub, &blend_area, clip) && !_lv_area_is_in(&clip_area_sub, &bg_area, r_bg)) {
    lv_coord_t w = lv_area_get_width(&clip_area_sub);
    sh_buf_tmp = sh_buf;
    sh_buf_tmp += (clip_area_sub.y1 - shadow_area.y1) * corner_size;
    sh_buf_tmp += clip_area_sub.x1 - (shadow_area.x2 - corner_size + 1);

    /*Do not mask if out of the bg*/
    if (simple && _lv_area_is_out(&clip_area_sub, &bg_area, r_bg))
      simple_sub = true;
    else
      simple_sub = simple;
    mask_act = simple_sub ? &sh_buf_tmp : &mask_buf;
    if (w > 0) {
      mask_res = LV_DRAW_MASK_RES_CHANGED; /*In simple mode it won't be overwritten*/
      for (y = clip_area_sub.y1; y <= clip_area_sub.y2; y++) {
        blend_area.y1 = y;
        blend_area.y2 = y;

        if (!simple_sub) {
          lv_memcpy(mask_buf, sh_buf_tmp, corner_size);
          mask_res = lv_draw_mask_apply(mask_buf, clip_area_sub.x1, y, w);
          if (mask_res == LV_DRAW_MASK_RES_FULL_COVER)
            mask_res = LV_DRAW_MASK_RES_CHANGED;
        }
        _lv_blend_fill(&clip_area_sub, &blend_area, dsc->shadow_color, *mask_act, mask_res, dsc->shadow_opa, dsc->blend_mode);
        sh_buf_tmp += corner_size;
      }
    }
  }

  /*Bottom right corner.
     *Almost the same as top right just read the lines of `sh_buf` from then end*/
  blend_area.x2 = shadow_area.x2;
  blend_area.x1 = shadow_area.x2 - corner_size + 1;
  blend_area.y1 = shadow_area.y2 - corner_size + 1;
  blend_area.y2 = shadow_area.y2;
  /*Do not overdraw the other corners*/
  blend_area.x1 = LV_MAX(blend_area.x1, w_half);
  blend_area.y1 = LV_MAX(blend_area.y1, h_half + 1);

  if (_lv_area_intersect(&clip_area_sub, &blend_area, clip) && !_lv_area_is_in(&clip_area_sub, &bg_area, r_bg)) {
    lv_coord_t w = lv_area_get_width(&clip_area_sub);
    sh_buf_tmp = sh_buf;
    sh_buf_tmp += (blend_area.y2 - clip_area_sub.y2) * corner_size;
    sh_buf_tmp += clip_area_sub.x1 - (shadow_area.x2 - corner_size + 1);
    /*Do not mask if out of the bg*/
    if (simple && _lv_area_is_out(&clip_area_sub, &bg_area, r_bg))
      simple_sub = true;
    else
      simple_sub = simple;
    mask_act = simple_sub ? &sh_buf_tmp : &mask_buf;

    if (w > 0) {
      mask_res = LV_DRAW_MASK_RES_CHANGED; /*In simple mode it won't be overwritten*/
      for (y = clip_area_sub.y2; y >= clip_area_sub.y1; y--) {
        blend_area.y1 = y;
        blend_area.y2 = y;

        if (!simple_sub) {
          lv_memcpy(mask_buf, sh_buf_tmp, corner_size);
          mask_res = lv_draw_mask_apply(mask_buf, clip_area_sub.x1, y, w);
          if (mask_res == LV_DRAW_MASK_RES_FULL_COVER)
            mask_res = LV_DRAW_MASK_RES_CHANGED;
        }
        _lv_blend_fill(&clip_area_sub, &blend_area, dsc->shadow_color, *mask_act, mask_res, dsc->shadow_opa, dsc->blend_mode);
        sh_buf_tmp += corner_size;
      }
    }
  }

  /*Top side*/
  blend_area.x1 = shadow_area.x1 + corner_size;
  blend_area.x2 = shadow_area.x2 - corner_size;
  blend_area.y1 = shadow_area.y1;
  blend_area.y2 = shadow_area.y1 + corner_size - 1;
  blend_area.y2 = LV_MIN(blend_area.y2, h_half);

  if (_lv_area_intersect(&clip_area_sub, &blend_area, clip) && !_lv_area_is_in(&clip_area_sub, &bg_area, r_bg)) {
    lv_coord_t w = lv_area_get_width(&clip_area_sub);
    sh_buf_tmp = sh_buf;
    sh_buf_tmp += (clip_area_sub.y1 - blend_area.y1) * corner_size;

    /*Do not mask if out of the bg*/
    if (simple && _lv_area_is_out(&clip_area_sub, &bg_area, r_bg))
      simple_sub = true;
    else
      simple_sub = simple;

    if (w > 0) {
      mask_res = LV_DRAW_MASK_RES_CHANGED; /*In simple mode it won't be overwritten*/
      for (y = clip_area_sub.y1; y <= clip_area_sub.y2; y++) {
        blend_area.y1 = y;
        blend_area.y2 = y;

        if (!simple_sub) {
          lv_memset(mask_buf, sh_buf_tmp[0], w);
          mask_res = lv_draw_mask_apply(mask_buf, clip_area_sub.x1, y, w);
          if (mask_res == LV_DRAW_MASK_RES_FULL_COVER)
            mask_res = LV_DRAW_MASK_RES_CHANGED;
          _lv_blend_fill(&clip_area_sub, &blend_area, dsc->shadow_color, mask_buf, mask_res, dsc->shadow_opa, dsc->blend_mode);
        } else {
          lv_opa_t line_opa = opa == LV_OPA_COVER ? sh_buf_tmp[0] : (sh_buf_tmp[0] * dsc->shadow_opa) >> 8;
          _lv_blend_fill(&clip_area_sub, &blend_area, dsc->shadow_color, NULL, LV_DRAW_MASK_RES_FULL_COVER, line_opa,
              dsc->blend_mode);
        }
        sh_buf_tmp += corner_size;
      }
    }
  }

  /*Bottom side*/
  blend_area.x1 = shadow_area.x1 + corner_size;
  blend_area.x2 = shadow_area.x2 - corner_size;
  blend_area.y1 = shadow_area.y2 - corner_size + 1;
  blend_area.y2 = shadow_area.y2;
  blend_area.y1 = LV_MAX(blend_area.y1, h_half + 1);

  if (_lv_area_intersect(&clip_area_sub, &blend_area, clip) && !_lv_area_is_in(&clip_area_sub, &bg_area, r_bg)) {
    lv_coord_t w = lv_area_get_width(&clip_area_sub);
    sh_buf_tmp = sh_buf;
    sh_buf_tmp += (blend_area.y2 - clip_area_sub.y2) * corner_size;
    if (w > 0) {
      mask_res = LV_DRAW_MASK_RES_CHANGED; /*In simple mode it won't be overwritten*/
      for (y = clip_area_sub.y2; y >= clip_area_sub.y1; y--) {
        blend_area.y1 = y;
        blend_area.y2 = y;

        /*Do not mask if out of the bg*/
        if (simple && _lv_area_is_out(&clip_area_sub, &bg_area, r_bg))
          simple_sub = true;
        else
          simple_sub = simple;

        if (!simple_sub) {
          lv_memset(mask_buf, sh_buf_tmp[0], w);
          mask_res = lv_draw_mask_apply(mask_buf, clip_area_sub.x1, y, w);
          if (mask_res == LV_DRAW_MASK_RES_FULL_COVER)
            mask_res = LV_DRAW_MASK_RES_CHANGED;
          _lv_blend_fill(&clip_area_sub, &blend_area, dsc->shadow_color, mask_buf, mask_res, dsc->shadow_opa, dsc->blend_mode);
        } else {
          lv_opa_t line_opa = opa == LV_OPA_COVER ? sh_buf_tmp[0] : (sh_buf_tmp[0] * dsc->shadow_opa) >> 8;
          _lv_blend_fill(&clip_area_sub, &blend_area, dsc->shadow_color, NULL, LV_DRAW_MASK_RES_FULL_COVER, line_opa,
              dsc->blend_mode);
        }
        sh_buf_tmp += corner_size;
      }
    }
  }

  /*Right side*/
  blend_area.x1 = shadow_area.x2 - corner_size + 1;
  blend_area.x2 = shadow_area.x2;
  blend_area.y1 = shadow_area.y1 + corner_size;
  blend_area.y2 = shadow_area.y2 - corner_size;
  /*Do not overdraw the other corners*/
  blend_area.y1 = LV_MIN(blend_area.y1, h_half + 1);
  blend_area.y2 = LV_MAX(blend_area.y2, h_half);
  blend_area.x1 = LV_MAX(blend_area.x1, w_half);

  if (_lv_area_intersect(&clip_area_sub, &blend_area, clip) && !_lv_area_is_in(&clip_area_sub, &bg_area, r_bg)) {
    lv_coord_t w = lv_area_get_width(&clip_area_sub);
    sh_buf_tmp = sh_buf;
    sh_buf_tmp += (corner_size - 1) * corner_size;
    sh_buf_tmp += clip_area_sub.x1 - (shadow_area.x2 - corner_size + 1);

    /*Do not mask if out of the bg*/
    if (simple && _lv_area_is_out(&clip_area_sub, &bg_area, r_bg))
      simple_sub = true;
    else
      simple_sub = simple;
    mask_act = simple_sub ? &sh_buf_tmp : &mask_buf;

    if (w > 0) {
      mask_res = LV_DRAW_MASK_RES_CHANGED; /*In simple mode it won't be overwritten*/
      for (y = clip_area_sub.y1; y <= clip_area_sub.y2; y++) {
        blend_area.y1 = y;
        blend_area.y2 = y;

        if (!simple_sub) {
          lv_memcpy(mask_buf, sh_buf_tmp, w);
          mask_res = lv_draw_mask_apply(mask_buf, clip_area_sub.x1, y, w);
          if (mask_res == LV_DRAW_MASK_RES_FULL_COVER)
            mask_res = LV_DRAW_MASK_RES_CHANGED;
        }
        _lv_blend_fill(&clip_area_sub, &blend_area, dsc->shadow_color, *mask_act, mask_res, dsc->shadow_opa, dsc->blend_mode);
      }
    }
  }

  /*Mirror the shadow corner buffer horizontally*/
  sh_buf_tmp = sh_buf;
  for (y = 0; y < corner_size; y++) {
    int32_t x;
    lv_opa_t* start = sh_buf_tmp;
    lv_opa_t* end = sh_buf_tmp + corner_size - 1;
    for (x = 0; x < corner_size / 2; x++) {
      lv_opa_t tmp = *start;
      *start = *end;
      *end = tmp;

      start++;
      end--;
    }
    sh_buf_tmp += corner_size;
  }

  /*Left side*/
  blend_area.x1 = shadow_area.x1;
  blend_area.x2 = shadow_area.x1 + corner_size - 1;
  blend_area.y1 = shadow_area.y1 + corner_size;
  blend_area.y2 = shadow_area.y2 - corner_size;
  /*Do not overdraw the other corners*/
  blend_area.y1 = LV_MIN(blend_area.y1, h_half + 1);
  blend_area.y2 = LV_MAX(blend_area.y2, h_half);
  blend_area.x2 = LV_MIN(blend_area.x2, w_half - 1);

  if (_lv_area_intersect(&clip_area_sub, &blend_area, clip) && !_lv_area_is_in(&clip_area_sub, &bg_area, r_bg)) {
    lv_coord_t w = lv_area_get_width(&clip_area_sub);
    sh_buf_tmp = sh_buf;
    sh_buf_tmp += (corner_size - 1) * corner_size;
    sh_buf_tmp += clip_area_sub.x1 - blend_area.x1;

    /*Do not mask if out of the bg*/
    if (simple && _lv_area_is_out(&clip_area_sub, &bg_area, r_bg))
      simple_sub = true;
    else
      simple_sub = simple;
    mask_act = simple_sub ? &sh_buf_tmp : &mask_buf;
    if (w > 0) {
      mask_res = LV_DRAW_MASK_RES_CHANGED; /*In simple mode it won't be overwritten*/
      for (y = clip_area_sub.y1; y <= clip_area_sub.y2; y++) {
        blend_area.y1 = y;
        blend_area.y2 = y;

        if (!simple_sub) {
          lv_memcpy(mask_buf, sh_buf_tmp, w);
          mask_res = lv_draw_mask_apply(mask_buf, clip_area_sub.x1, y, w);
          if (mask_res == LV_DRAW_MASK_RES_FULL_COVER)
            mask_res = LV_DRAW_MASK_RES_CHANGED;
        }
        _lv_blend_fill(&clip_area_sub, &blend_area, dsc->shadow_color, *mask_act, mask_res, dsc->shadow_opa, dsc->blend_mode);
      }
    }
  }

  /*Top left corner*/
  blend_area.x1 = shadow_area.x1;
  blend_area.x2 = shadow_area.x1 + corner_size - 1;
  blend_area.y1 = shadow_area.y1;
  blend_area.y2 = shadow_area.y1 + corner_size - 1;
  /*Do not overdraw the other corners*/
  blend_area.x2 = LV_MIN(blend_area.x2, w_half - 1);
  blend_area.y2 = LV_MIN(blend_area.y2, h_half);

  if (_lv_area_intersect(&clip_area_sub, &blend_area, clip) && !_lv_area_is_in(&clip_area_sub, &bg_area, r_bg)) {
    lv_coord_t w = lv_area_get_width(&clip_area_sub);
    sh_buf_tmp = sh_buf;
    sh_buf_tmp += (clip_area_sub.y1 - blend_area.y1) * corner_size;
    sh_buf_tmp += clip_area_sub.x1 - blend_area.x1;

    /*Do not mask if out of the bg*/
    if (simple && _lv_area_is_out(&clip_area_sub, &bg_area, r_bg))
      simple_sub = true;
    else
      simple_sub = simple;
    mask_act = simple_sub ? &sh_buf_tmp : &mask_buf;

    if (w > 0) {
      mask_res = LV_DRAW_MASK_RES_CHANGED; /*In simple mode it won't be overwritten*/
      for (y = clip_area_sub.y1; y <= clip_area_sub.y2; y++) {
        blend_area.y1 = y;
        blend_area.y2 = y;

        if (!simple_sub) {
          lv_memcpy(mask_buf, sh_buf_tmp, corner_size);
          mask_res = lv_draw_mask_apply(mask_buf, clip_area_sub.x1, y, w);
          if (mask_res == LV_DRAW_MASK_RES_FULL_COVER)
            mask_res = LV_DRAW_MASK_RES_CHANGED;
        }
        _lv_blend_fill(&clip_area_sub, &blend_area, dsc->shadow_color, *mask_act, mask_res, dsc->shadow_opa, dsc->blend_mode);
        sh_buf_tmp += corner_size;
      }
    }
  }

  /*Bottom left corner.
     *Almost the same as bottom right just read the lines of `sh_buf` from then end*/
  blend_area.x1 = shadow_area.x1;
  blend_area.x2 = shadow_area.x1 + corner_size - 1;
  blend_area.y1 = shadow_area.y2 - corner_size + 1;
  blend_area.y2 = shadow_area.y2;
  /*Do not overdraw the other corners*/
  blend_area.y1 = LV_MAX(blend_area.y1, h_half + 1);
  blend_area.x2 = LV_MIN(blend_area.x2, w_half - 1);

  if (_lv_area_intersect(&clip_area_sub, &blend_area, clip) && !_lv_area_is_in(&clip_area_sub, &bg_area, r_bg)) {
    lv_coord_t w = lv_area_get_width(&clip_area_sub);
    sh_buf_tmp = sh_buf;
    sh_buf_tmp += (blend_area.y2 - clip_area_sub.y2) * corner_size;
    sh_buf_tmp += clip_area_sub.x1 - blend_area.x1;

    /*Do not mask if out of the bg*/
    if (simple && _lv_area_is_out(&clip_area_sub, &bg_area, r_bg))
      simple_sub = true;
    else
      simple_sub = simple;
    mask_act = simple_sub ? &sh_buf_tmp : &mask_buf;
    if (w > 0) {
      mask_res = LV_DRAW_MASK_RES_CHANGED; /*In simple mode it won't be overwritten*/
      for (y = clip_area_sub.y2; y >= clip_area_sub.y1; y--) {
        blend_area.y1 = y;
        blend_area.y2 = y;

        if (!simple_sub) {
          lv_memcpy(mask_buf, sh_buf_tmp, corner_size);
          mask_res = lv_draw_mask_apply(mask_buf, clip_area_sub.x1, y, w);
          if (mask_res == LV_DRAW_MASK_RES_FULL_COVER)
            mask_res = LV_DRAW_MASK_RES_CHANGED;
        }
        _lv_blend_fill(&clip_area_sub, &blend_area, dsc->shadow_color, *mask_act, mask_res, dsc->shadow_opa, dsc->blend_mode);
        sh_buf_tmp += corner_size;
      }
    }
  }

  /*Draw the center rectangle.*/
  blend_area.x1 = shadow_area.x1 + corner_size;
  blend_area.x2 = shadow_area.x2 - corner_size;
  blend_area.y1 = shadow_area.y1 + corner_size;
  blend_area.y2 = shadow_area.y2 - corner_size;

  if (_lv_area_intersect(&clip_area_sub, &blend_area, clip) && !_lv_area_is_in(&clip_area_sub, &bg_area, r_bg)) {
    lv_coord_t w = lv_area_get_width(&clip_area_sub);
    if (w > 0) {
      for (y = clip_area_sub.y1; y <= clip_area_sub.y2; y++) {
        blend_area.y1 = y;
        blend_area.y2 = y;

        lv_memset_ff(mask_buf, w);
        mask_res = lv_draw_mask_apply(mask_buf, clip_area_sub.x1, y, w);
        _lv_blend_fill(&clip_area_sub, &blend_area, dsc->shadow_color, mask_buf, mask_res, dsc->shadow_opa, dsc->blend_mode);
      }
    }
  }

  if (!simple) {
    lv_draw_mask_free_param(&mask_rout_param);
    lv_draw_mask_remove_id(mask_rout_id);
  }
  lv_mem_buf_release(sh_buf);
  lv_mem_buf_release(mask_buf);
}

/**
 * Calculate a blurred corner
 * @param coords Coordinates of the shadow
 * @param sh_buf a buffer to store the result. Its size should be `(sw + r)^2 * 2`
 * @param sw shadow width
 * @param r radius
 */
LV_ATTRIBUTE_FAST_MEM static void shadow_draw_corner_buf(const lv_area_t* coords, uint16_t* sh_buf, lv_coord_t sw,
    lv_coord_t r)
{
  int32_t sw_ori = sw;
  int32_t size = sw_ori + r;

  lv_area_t sh_area;
  lv_area_copy(&sh_area, coords);
  sh_area.x2 = sw / 2 + r - 1 - ((sw & 1) ? 0 : 1);
  sh_area.y1 = sw / 2 + 1;

  sh_area.x1 = sh_area.x2 - lv_area_get_width(coords);
  sh_area.y2 = sh_area.y1 + lv_area_get_height(coords);

  lv_draw_mask_radius_param_t mask_param;
  lv_draw_mask_radius_init(&mask_param, &sh_area, r, false);

#if SHADOW_ENHANCE
  /*Set half shadow width width because blur will be repeated*/
  if (sw_ori == 1)
    sw = 1;
  else
    sw = sw_ori >> 1;
#endif

  int32_t y;
  lv_opa_t* mask_line = lv_mem_buf_get(size);
  uint16_t* sh_ups_tmp_buf = (uint16_t*)sh_buf;
  for (y = 0; y < size; y++) {
    lv_memset_ff(mask_line, size);
    lv_draw_mask_res_t mask_res = mask_param.dsc.cb(mask_line, 0, y, size, &mask_param);
    if (mask_res == LV_DRAW_MASK_RES_TRANSP) {
      lv_memset_00(sh_ups_tmp_buf, size * sizeof(sh_ups_tmp_buf[0]));
    } else {
      int32_t i;
      sh_ups_tmp_buf[0] = (mask_line[0] << SHADOW_UPSCALE_SHIFT) / sw;
      for (i = 1; i < size; i++) {
        if (mask_line[i] == mask_line[i - 1])
          sh_ups_tmp_buf[i] = sh_ups_tmp_buf[i - 1];
        else
          sh_ups_tmp_buf[i] = (mask_line[i] << SHADOW_UPSCALE_SHIFT) / sw;
      }
    }

    sh_ups_tmp_buf += size;
  }
  lv_mem_buf_release(mask_line);

  lv_draw_mask_free_param(&mask_param);

  if (sw == 1) {
    int32_t i;
    lv_opa_t* res_buf = (lv_opa_t*)sh_buf;
    for (i = 0; i < size * size; i++) {
      res_buf[i] = (sh_buf[i] >> SHADOW_UPSCALE_SHIFT);
    }
    return;
  }

  shadow_blur_corner(size, sw, sh_buf);

#if SHADOW_ENHANCE == 0
  /*The result is required in lv_opa_t not uint16_t*/
  uint32_t x;
  lv_opa_t* res_buf = (lv_opa_t*)sh_buf;
  for (x = 0; x < size * size; x++) {
    res_buf[x] = sh_buf[x];
  }
#else
  sw += sw_ori & 1;
  if (sw > 1) {
    uint32_t i;
    uint32_t max_v_div = (LV_OPA_COVER << SHADOW_UPSCALE_SHIFT) / sw;
    for (i = 0; i < (uint32_t)size * size; i++) {
      if (sh_buf[i] == 0)
        continue;
      else if (sh_buf[i] == LV_OPA_COVER)
        sh_buf[i] = max_v_div;
      else
        sh_buf[i] = (sh_buf[i] << SHADOW_UPSCALE_SHIFT) / sw;
    }

    shadow_blur_corner(size, sw, sh_buf);
  }
  int32_t x;
  lv_opa_t* res_buf = (lv_opa_t*)sh_buf;
  for (x = 0; x < size * size; x++) {
    res_buf[x] = sh_buf[x];
  }
#endif
}

LV_ATTRIBUTE_FAST_MEM static void shadow_blur_corner(lv_coord_t size, lv_coord_t sw, uint16_t* sh_ups_buf)
{
  int32_t s_left = sw >> 1;
  int32_t s_right = (sw >> 1);
  if ((sw & 1) == 0)
    s_left--;

  /*Horizontal blur*/
  uint16_t* sh_ups_blur_buf = lv_mem_buf_get(size * sizeof(uint16_t));

  int32_t x;
  int32_t y;

  uint16_t* sh_ups_tmp_buf = sh_ups_buf;

  for (y = 0; y < size; y++) {
    int32_t v = sh_ups_tmp_buf[size - 1] * sw;
    for (x = size - 1; x >= 0; x--) {
      sh_ups_blur_buf[x] = v;

      /*Forget the right pixel*/
      uint32_t right_val = 0;
      if (x + s_right < size)
        right_val = sh_ups_tmp_buf[x + s_right];
      v -= right_val;

      /*Add the left pixel*/
      uint32_t left_val;
      if (x - s_left - 1 < 0)
        left_val = sh_ups_tmp_buf[0];
      else
        left_val = sh_ups_tmp_buf[x - s_left - 1];
      v += left_val;
    }
    lv_memcpy(sh_ups_tmp_buf, sh_ups_blur_buf, size * sizeof(uint16_t));
    sh_ups_tmp_buf += size;
  }

  /*Vertical blur*/
  uint32_t i;
  uint32_t max_v = LV_OPA_COVER << SHADOW_UPSCALE_SHIFT;
  uint32_t max_v_div = max_v / sw;
  for (i = 0; i < (uint32_t)size * size; i++) {
    if (sh_ups_buf[i] == 0)
      continue;
    else if (sh_ups_buf[i] == max_v)
      sh_ups_buf[i] = max_v_div;
    else
      sh_ups_buf[i] = sh_ups_buf[i] / sw;
  }

  for (x = 0; x < size; x++) {
    sh_ups_tmp_buf = &sh_ups_buf[x];
    int32_t v = sh_ups_tmp_buf[0] * sw;
    for (y = 0; y < size; y++, sh_ups_tmp_buf += size) {
      sh_ups_blur_buf[y] = v < 0 ? 0 : (v >> SHADOW_UPSCALE_SHIFT);

      /*Forget the top pixel*/
      uint32_t top_val;
      if (y - s_right <= 0)
        top_val = sh_ups_tmp_buf[0];
      else
        top_val = sh_ups_buf[(y - s_right) * size + x];
      v -= top_val;

      /*Add the bottom pixel*/
      uint32_t bottom_val;
      if (y + s_left + 1 < size)
        bottom_val = sh_ups_buf[(y + s_left + 1) * size + x];
      else
        bottom_val = sh_ups_buf[(size - 1) * size + x];
      v += bottom_val;
    }

    /*Write back the result into `sh_ups_buf`*/
    sh_ups_tmp_buf = &sh_ups_buf[x];
    for (y = 0; y < size; y++, sh_ups_tmp_buf += size) {
      (*sh_ups_tmp_buf) = sh_ups_blur_buf[y];
    }
  }

  lv_mem_buf_release(sh_ups_blur_buf);
}

#endif

static void draw_outline(const lv_area_t* coords, const lv_area_t* clip, const lv_draw_rect_dsc_t* dsc)
{
  if (dsc->outline_opa <= LV_OPA_MIN)
    return;
  if (dsc->outline_width == 0)
    return;

  lv_opa_t opa = dsc->outline_opa;

  if (opa > LV_OPA_MAX)
    opa = LV_OPA_COVER;

  /*Get the inner radius*/
  lv_area_t area_inner;
  lv_area_copy(&area_inner, coords);

  /*Extend the outline into the background area if it's overlapping the edge*/
  lv_coord_t pad = (dsc->outline_pad == 0 ? (dsc->outline_pad - 1) : dsc->outline_pad);
  area_inner.x1 -= pad;
  area_inner.y1 -= pad;
  area_inner.x2 += pad;
  area_inner.y2 += pad;

  lv_area_t area_outer;
  lv_area_copy(&area_outer, &area_inner);

  area_outer.x1 -= dsc->outline_width;
  area_outer.x2 += dsc->outline_width;
  area_outer.y1 -= dsc->outline_width;
  area_outer.y2 += dsc->outline_width;

  int32_t inner_w = lv_area_get_width(&area_inner);
  int32_t inner_h = lv_area_get_height(&area_inner);
  int32_t rin = dsc->radius;
  int32_t short_side = LV_MIN(inner_w, inner_h);
  if (rin > short_side >> 1)
    rin = short_side >> 1;

  lv_coord_t rout = rin + dsc->outline_width;

  draw_border_generic(clip, &area_outer, &area_inner, rout, rin, dsc->outline_color, dsc->outline_opa, dsc->blend_mode);
}

static void draw_border_generic(const lv_area_t* clip_area, const lv_area_t* outer_area, const lv_area_t* inner_area,
    lv_coord_t rout, lv_coord_t rin, lv_color_t color, lv_opa_t opa, lv_blend_mode_t blend_mode)
{
  opa = opa >= LV_OPA_COVER ? LV_OPA_COVER : opa;

  bool mask_any = lv_draw_mask_is_any(outer_area);

  if (!mask_any && rout == 0 && rin == 0) {
    draw_border_simple(clip_area, outer_area, inner_area, color, opa);
    return;
  }

#if LV_DRAW_COMPLEX
  /*Get clipped draw area which is the real draw area.
     *It is always the same or inside `coords`*/
  lv_area_t draw_area;
  if (!_lv_area_intersect(&draw_area, outer_area, clip_area))
    return;
  int32_t draw_area_w = lv_area_get_width(&draw_area);

  /*Create a mask if there is a radius*/
  lv_opa_t* mask_buf = lv_mem_buf_get(draw_area_w);

  /*Create mask for the outer area*/
  int16_t mask_rout_id = LV_MASK_ID_INV;
  lv_draw_mask_radius_param_t mask_rout_param;
  if (rout > 0) {
    lv_draw_mask_radius_init(&mask_rout_param, outer_area, rout, false);
    mask_rout_id = lv_draw_mask_add(&mask_rout_param, NULL);
  }

  /*Create mask for the inner mask*/
  lv_draw_mask_radius_param_t mask_rin_param;
  lv_draw_mask_radius_init(&mask_rin_param, inner_area, rin, true);
  int16_t mask_rin_id = lv_draw_mask_add(&mask_rin_param, NULL);

  int32_t h;
  lv_draw_mask_res_t mask_res;
  lv_area_t blend_area;

  /*Calculate the x and y coordinates where the straight parts area*/
  lv_area_t core_area;
  core_area.x1 = LV_MAX(outer_area->x1 + rout, inner_area->x1);
  core_area.x2 = LV_MIN(outer_area->x2 - rout, inner_area->x2);
  core_area.y1 = LV_MAX(outer_area->y1 + rout, inner_area->y1);
  core_area.y2 = LV_MIN(outer_area->y2 - rout, inner_area->y2);
  lv_coord_t core_w = lv_area_get_width(&core_area);

  bool top_side = outer_area->y1 <= inner_area->y1 ? true : false;
  bool bottom_side = outer_area->y2 >= inner_area->y2 ? true : false;

  /*If there is other masks, need to draw line by line*/
  if (mask_any) {
    blend_area.x1 = draw_area.x1;
    blend_area.x2 = draw_area.x2;
    for (h = draw_area.y1; h <= draw_area.y2; h++) {
      if (!top_side && h < core_area.y1)
        continue;
      if (!bottom_side && h > core_area.y2)
        break;

      blend_area.y1 = h;
      blend_area.y2 = h;

      lv_memset_ff(mask_buf, draw_area_w);
      mask_res = lv_draw_mask_apply(mask_buf, draw_area.x1, h, draw_area_w);
      _lv_blend_fill(clip_area, &blend_area, color, mask_buf, mask_res, opa, blend_mode);
    }

    lv_draw_mask_free_param(&mask_rin_param);
    lv_draw_mask_remove_id(mask_rin_id);
    lv_draw_mask_free_param(&mask_rout_param);
    lv_draw_mask_remove_id(mask_rout_id);
    lv_mem_buf_release(mask_buf);
    return;
  }

  /*No masks*/
  bool left_side = outer_area->x1 <= inner_area->x1 ? true : false;
  bool right_side = outer_area->x2 >= inner_area->x2 ? true : false;

  bool split_hor = true;
  if (left_side && right_side && top_side && bottom_side && core_w < SPLIT_LIMIT) {
    split_hor = false;
  }

  /*Draw the straight lines first if they are long enough*/
  if (top_side && split_hor) {
    blend_area.x1 = core_area.x1;
    blend_area.x2 = core_area.x2;
    blend_area.y1 = outer_area->y1;
    blend_area.y2 = inner_area->y1 - 1;
    _lv_blend_fill(clip_area, &blend_area, color, NULL, LV_DRAW_MASK_RES_FULL_COVER, opa, blend_mode);
  }

  if (bottom_side && split_hor) {
    blend_area.x1 = core_area.x1;
    blend_area.x2 = core_area.x2;
    blend_area.y1 = inner_area->y2 + 1;
    blend_area.y2 = outer_area->y2;
    _lv_blend_fill(clip_area, &blend_area, color, NULL, LV_DRAW_MASK_RES_FULL_COVER, opa, blend_mode);
  }

  if (left_side) {
    blend_area.x1 = outer_area->x1;
    blend_area.x2 = inner_area->x1 - 1;
    blend_area.y1 = core_area.y1;
    blend_area.y2 = core_area.y2;
    _lv_blend_fill(clip_area, &blend_area, color, NULL, LV_DRAW_MASK_RES_FULL_COVER, opa, blend_mode);
  }

  if (right_side) {
    blend_area.x1 = inner_area->x2 + 1;
    blend_area.x2 = outer_area->x2;
    blend_area.y1 = core_area.y1;
    blend_area.y2 = core_area.y2;
    _lv_blend_fill(clip_area, &blend_area, color, NULL, LV_DRAW_MASK_RES_FULL_COVER, opa, blend_mode);
  }

  /*Draw the corners*/
  lv_coord_t blend_w;

  /*Left and right corner together is they close to eachother*/
  if (!split_hor) {
    /*Calculate the top corner and mirror it to the bottom*/
    blend_area.x1 = draw_area.x1;
    blend_area.x2 = draw_area.x2;
    lv_coord_t max_h = LV_MAX(rout, outer_area->y1 - inner_area->y1);
    for (h = 0; h < max_h; h++) {
      lv_coord_t top_y = outer_area->y1 + h;
      lv_coord_t bottom_y = outer_area->y2 - h;
      if (top_y < draw_area.y1 && bottom_y > draw_area.y2)
        continue; /*This line is clipped now*/

      lv_memset_ff(mask_buf, draw_area_w);
      mask_res = lv_draw_mask_apply(mask_buf, blend_area.x1, top_y, draw_area_w);

      if (top_y >= draw_area.y1) {
        blend_area.y1 = top_y;
        blend_area.y2 = top_y;
        _lv_blend_fill(clip_area, &blend_area, color, mask_buf, mask_res, opa, blend_mode);
      }

      if (bottom_y <= draw_area.y2) {
        blend_area.y1 = bottom_y;
        blend_area.y2 = bottom_y;
        _lv_blend_fill(clip_area, &blend_area, color, mask_buf, mask_res, opa, blend_mode);
      }
    }
  } else {
    /*Left corners*/
    blend_area.x1 = draw_area.x1;
    blend_area.x2 = LV_MIN(draw_area.x2, core_area.x1 - 1);
    blend_w = lv_area_get_width(&blend_area);
    if (blend_w > 0) {
      if (left_side || top_side) {
        for (h = draw_area.y1; h < core_area.y1; h++) {
          blend_area.y1 = h;
          blend_area.y2 = h;

          lv_memset_ff(mask_buf, blend_w);
          mask_res = lv_draw_mask_apply(mask_buf, blend_area.x1, h, blend_w);
          _lv_blend_fill(clip_area, &blend_area, color, mask_buf, mask_res, opa, blend_mode);
        }
      }

      if (left_side || bottom_side) {
        for (h = core_area.y2 + 1; h <= draw_area.y2; h++) {
          blend_area.y1 = h;
          blend_area.y2 = h;

          lv_memset_ff(mask_buf, blend_w);
          mask_res = lv_draw_mask_apply(mask_buf, blend_area.x1, h, blend_w);
          _lv_blend_fill(clip_area, &blend_area, color, mask_buf, mask_res, opa, blend_mode);
        }
      }
    }

    /*Right corners*/
    blend_area.x1 = LV_MAX(draw_area.x1, core_area.x2 + 1);
    blend_area.x2 = draw_area.x2;
    blend_w = lv_area_get_width(&blend_area);

    if (blend_w > 0) {
      if (right_side || top_side) {
        for (h = draw_area.y1; h < core_area.y1; h++) {
          blend_area.y1 = h;
          blend_area.y2 = h;

          lv_memset_ff(mask_buf, blend_w);
          mask_res = lv_draw_mask_apply(mask_buf, blend_area.x1, h, blend_w);
          _lv_blend_fill(clip_area, &blend_area, color, mask_buf, mask_res, opa, blend_mode);
        }
      }

      if (right_side || bottom_side) {
        for (h = core_area.y2 + 1; h <= draw_area.y2; h++) {
          blend_area.y1 = h;
          blend_area.y2 = h;

          lv_memset_ff(mask_buf, blend_w);
          mask_res = lv_draw_mask_apply(mask_buf, blend_area.x1, h, blend_w);
          _lv_blend_fill(clip_area, &blend_area, color, mask_buf, mask_res, opa, blend_mode);
        }
      }
    }
  }

  lv_draw_mask_free_param(&mask_rin_param);
  lv_draw_mask_remove_id(mask_rin_id);
  lv_draw_mask_free_param(&mask_rout_param);
  lv_draw_mask_remove_id(mask_rout_id);
  lv_mem_buf_release(mask_buf);

#else /*LV_DRAW_COMPLEX*/
  LV_UNUSED(blend_mode);
#endif /*LV_DRAW_COMPLEX*/
}

static void draw_border_simple(const lv_area_t* clip, const lv_area_t* outer_area, const lv_area_t* inner_area,
    lv_color_t color, lv_opa_t opa)
{
  bool top_side = outer_area->y1 <= inner_area->y1 ? true : false;
  bool bottom_side = outer_area->y2 >= inner_area->y2 ? true : false;
  bool left_side = outer_area->x1 <= inner_area->x1 ? true : false;
  bool right_side = outer_area->x2 >= inner_area->x2 ? true : false;

  lv_area_t a;
  /*Top*/
  a.x1 = outer_area->x1;
  a.x2 = outer_area->x2;
  a.y1 = outer_area->y1;
  a.y2 = inner_area->y1 - 1;
  if (top_side) {
    _lv_blend_fill(clip, &a, color, NULL, LV_DRAW_MASK_RES_FULL_COVER, opa, LV_BLEND_MODE_NORMAL);
  }

  /*Bottom*/
  a.y1 = inner_area->y2 + 1;
  a.y2 = outer_area->y2;
  if (bottom_side) {
    _lv_blend_fill(clip, &a, color, NULL, LV_DRAW_MASK_RES_FULL_COVER, opa, LV_BLEND_MODE_NORMAL);
  }

  /*Left*/
  a.x1 = outer_area->x1;
  a.x2 = inner_area->x1 - 1;
  a.y1 = (top_side) ? inner_area->y1 : outer_area->y1;
  a.y2 = (bottom_side) ? inner_area->y2 : outer_area->y2;
  if (left_side) {
    _lv_blend_fill(clip, &a, color, NULL, LV_DRAW_MASK_RES_FULL_COVER, opa, LV_BLEND_MODE_NORMAL);
  }

  /*Right*/
  a.x1 = inner_area->x2 + 1;
  a.x2 = outer_area->x2;
  if (right_side) {
    _lv_blend_fill(clip, &a, color, NULL, LV_DRAW_MASK_RES_FULL_COVER, opa, LV_BLEND_MODE_NORMAL);
  }
}

/* Copied from lv_draw_label.c */

LV_ATTRIBUTE_FAST_MEM static void draw_letter_normal(lv_coord_t pos_x, lv_coord_t pos_y, lv_font_glyph_dsc_t* g,
    const lv_area_t* clip_area,
    const uint8_t* map_p, lv_color_t color, lv_opa_t opa, lv_blend_mode_t blend_mode)
{
  const uint8_t* bpp_opa_table_p;
  uint32_t bitmask_init;
  uint32_t bitmask;
  uint32_t bpp = g->bpp;
  uint32_t shades;
  if (bpp == 3)
    bpp = 4;

  switch (bpp) {
  case 1:
    bpp_opa_table_p = _lv_bpp1_opa_table;
    bitmask_init = 0x80;
    shades = 2;
    break;
  case 2:
    bpp_opa_table_p = _lv_bpp2_opa_table;
    bitmask_init = 0xC0;
    shades = 4;
    break;
  case 4:
    bpp_opa_table_p = _lv_bpp4_opa_table;
    bitmask_init = 0xF0;
    shades = 16;
    break;
  case 8:
    bpp_opa_table_p = _lv_bpp8_opa_table;
    bitmask_init = 0xFF;
    shades = 256;
    break; /*No opa table, pixel value will be used directly*/
  default:
    LV_LOG_WARN("lv_draw_letter: invalid bpp");
    return; /*Invalid bpp. Can't render the letter*/
  }

  static lv_opa_t opa_table[256];
  static lv_opa_t prev_opa = LV_OPA_TRANSP;
  static uint32_t prev_bpp = 0;
  if (opa < LV_OPA_MAX) {
    if (prev_opa != opa || prev_bpp != bpp) {
      uint32_t i;
      for (i = 0; i < shades; i++) {
        opa_table[i] = bpp_opa_table_p[i] == LV_OPA_COVER ? opa : ((bpp_opa_table_p[i] * opa) >> 8);
      }
    }
    bpp_opa_table_p = opa_table;
    prev_opa = opa;
    prev_bpp = bpp;
  }

  int32_t col, row;
  int32_t box_w = g->box_w;
  int32_t box_h = g->box_h;
  int32_t width_bit = box_w * bpp; /*Letter width in bits*/

  /*Calculate the col/row start/end on the map*/
  int32_t col_start = pos_x >= clip_area->x1 ? 0 : clip_area->x1 - pos_x;
  int32_t col_end = pos_x + box_w <= clip_area->x2 ? box_w : clip_area->x2 - pos_x + 1;
  int32_t row_start = pos_y >= clip_area->y1 ? 0 : clip_area->y1 - pos_y;
  int32_t row_end = pos_y + box_h <= clip_area->y2 ? box_h : clip_area->y2 - pos_y + 1;

  /*Move on the map too*/
  uint32_t bit_ofs = (row_start * width_bit) + (col_start * bpp);
  map_p += bit_ofs >> 3;

  uint8_t letter_px;
  uint32_t col_bit;
  col_bit = bit_ofs & 0x7; /*"& 0x7" equals to "% 8" just faster*/

  lv_coord_t hor_res = lv_disp_get_hor_res(_lv_refr_get_disp_refreshing());
  uint32_t mask_buf_size = box_w * box_h > hor_res ? hor_res : box_w * box_h;
  lv_opa_t* mask_buf = lv_mem_buf_get(mask_buf_size);
  int32_t mask_p = 0;

  lv_area_t fill_area;
  fill_area.x1 = col_start + pos_x;
  fill_area.x2 = col_end + pos_x - 1;
  fill_area.y1 = row_start + pos_y;
  fill_area.y2 = fill_area.y1;
#if LV_DRAW_COMPLEX
  bool mask_any = lv_draw_mask_is_any(&fill_area);
#endif

  uint32_t col_bit_max = 8 - bpp;
  uint32_t col_bit_row_ofs = (box_w + col_start - col_end) * bpp;

  for (row = row_start; row < row_end; row++) {
#if LV_DRAW_COMPLEX
    int32_t mask_p_start = mask_p;
#endif
    bitmask = bitmask_init >> col_bit;
    for (col = col_start; col < col_end; col++) {
      /*Load the pixel's opacity into the mask*/
      letter_px = (*map_p & bitmask) >> (col_bit_max - col_bit);
      if (letter_px) {
        mask_buf[mask_p] = bpp_opa_table_p[letter_px];
      } else {
        mask_buf[mask_p] = 0;
      }

      /*Go to the next column*/
      if (col_bit < col_bit_max) {
        col_bit += bpp;
        bitmask = bitmask >> bpp;
      } else {
        col_bit = 0;
        bitmask = bitmask_init;
        map_p++;
      }

      /*Next mask byte*/
      mask_p++;
    }

#if LV_DRAW_COMPLEX
    /*Apply masks if any*/
    if (mask_any) {
      lv_draw_mask_res_t mask_res = lv_draw_mask_apply(mask_buf + mask_p_start, fill_area.x1, fill_area.y2,
          lv_area_get_width(&fill_area));
      if (mask_res == LV_DRAW_MASK_RES_TRANSP) {
        lv_memset_00(mask_buf + mask_p_start, lv_area_get_width(&fill_area));
      }
    }
#endif

    if ((uint32_t)mask_p + (col_end - col_start) < mask_buf_size) {
      fill_area.y2++;
    } else {
      _lv_blend_fill(clip_area, &fill_area,
          color, mask_buf, LV_DRAW_MASK_RES_CHANGED, LV_OPA_COVER,
          blend_mode);

      fill_area.y1 = fill_area.y2 + 1;
      fill_area.y2 = fill_area.y1;
      mask_p = 0;
    }

    col_bit += col_bit_row_ofs;
    map_p += (col_bit >> 3);
    col_bit = col_bit & 0x7;
  }

  /*Flush the last part*/
  if (fill_area.y1 != fill_area.y2) {
    fill_area.y2--;
    _lv_blend_fill(clip_area, &fill_area,
        color, mask_buf, LV_DRAW_MASK_RES_CHANGED, LV_OPA_COVER,
        blend_mode);
    mask_p = 0;
  }

  lv_mem_buf_release(mask_buf);
}

#if LV_DRAW_COMPLEX && LV_USE_FONT_SUBPX
static void draw_letter_subpx(lv_coord_t pos_x, lv_coord_t pos_y, lv_font_glyph_dsc_t* g, const lv_area_t* clip_area,
    const uint8_t* map_p, lv_color_t color, lv_opa_t opa, lv_blend_mode_t blend_mode)
{
  const uint8_t* bpp_opa_table;
  uint32_t bitmask_init;
  uint32_t bitmask;
  uint32_t bpp = g->bpp;
  if (bpp == 3)
    bpp = 4;

  switch (bpp) {
  case 1:
    bpp_opa_table = _lv_bpp1_opa_table;
    bitmask_init = 0x80;
    break;
  case 2:
    bpp_opa_table = _lv_bpp2_opa_table;
    bitmask_init = 0xC0;
    break;
  case 4:
    bpp_opa_table = _lv_bpp4_opa_table;
    bitmask_init = 0xF0;
    break;
  case 8:
    bpp_opa_table = _lv_bpp8_opa_table;
    bitmask_init = 0xFF;
    break; /*No opa table, pixel value will be used directly*/
  default:
    LV_LOG_WARN("lv_draw_letter: invalid bpp not found");
    return; /*Invalid bpp. Can't render the letter*/
  }

  int32_t col, row;

  int32_t box_w = g->box_w;
  int32_t box_h = g->box_h;
  int32_t width_bit = box_w * bpp; /*Letter width in bits*/

  /*Calculate the col/row start/end on the map*/
  int32_t col_start = pos_x >= clip_area->x1 ? 0 : (clip_area->x1 - pos_x) * 3;
  int32_t col_end = pos_x + box_w / 3 <= clip_area->x2 ? box_w : (clip_area->x2 - pos_x + 1) * 3;
  int32_t row_start = pos_y >= clip_area->y1 ? 0 : clip_area->y1 - pos_y;
  int32_t row_end = pos_y + box_h <= clip_area->y2 ? box_h : clip_area->y2 - pos_y + 1;

  /*Move on the map too*/
  int32_t bit_ofs = (row_start * width_bit) + (col_start * bpp);
  map_p += bit_ofs >> 3;

  uint8_t letter_px;
  lv_opa_t px_opa;
  int32_t col_bit;
  col_bit = bit_ofs & 0x7; /*"& 0x7" equals to "% 8" just faster*/

  lv_area_t map_area;
  map_area.x1 = col_start / 3 + pos_x;
  map_area.x2 = col_end / 3 + pos_x - 1;
  map_area.y1 = row_start + pos_y;
  map_area.y2 = map_area.y1;

  if (map_area.x2 <= map_area.x1)
    return;

  int32_t mask_buf_size = box_w * box_h > _LV_MASK_BUF_MAX_SIZE ? _LV_MASK_BUF_MAX_SIZE : g->box_w * g->box_h;
  lv_opa_t* mask_buf = lv_mem_buf_get(mask_buf_size);
  int32_t mask_p = 0;

  lv_color_t* color_buf = lv_mem_buf_get(mask_buf_size * sizeof(lv_color_t));

  lv_disp_t* disp = _lv_refr_get_disp_refreshing();
  lv_disp_draw_buf_t* draw_buf = lv_disp_get_draw_buf(disp);

  int32_t disp_buf_width = lv_area_get_width(&draw_buf->area);
  lv_color_t* disp_buf_buf_tmp = draw_buf->buf_act;

  /*Set a pointer on draw_buf to the first pixel of the letter*/
  disp_buf_buf_tmp += ((pos_y - draw_buf->area.y1) * disp_buf_width) + pos_x - draw_buf->area.x1;

  /*If the letter is partially out of mask the move there on draw_buf*/
  disp_buf_buf_tmp += (row_start * disp_buf_width) + col_start / 3;

  bool mask_any = lv_draw_mask_is_any(&map_area);
  uint8_t font_rgb[3];

#if LV_COLOR_16_SWAP == 0
  uint8_t txt_rgb[3] = { color.ch.red, color.ch.green, color.ch.blue };
#else
  uint8_t txt_rgb[3] = { color.ch.red, (color.ch.green_h << 3) + color.ch.green_l, color.ch.blue };
#endif

  for (row = row_start; row < row_end; row++) {
    uint32_t subpx_cnt = 0;
    bitmask = bitmask_init >> col_bit;
    int32_t mask_p_start = mask_p;

    for (col = col_start; col < col_end; col++) {
      /*Load the pixel's opacity into the mask*/
      letter_px = (*map_p & bitmask) >> (8 - col_bit - bpp);
      if (letter_px != 0) {
        if (opa == LV_OPA_COVER) {
          px_opa = bpp == 8 ? letter_px : bpp_opa_table[letter_px];
        } else {
          px_opa = bpp == 8 ? (uint32_t)((uint32_t)letter_px * opa) >> 8
                            : (uint32_t)((uint32_t)bpp_opa_table[letter_px] * opa) >> 8;
        }
      } else {
        px_opa = 0;
      }

      font_rgb[subpx_cnt] = px_opa;

      subpx_cnt++;
      if (subpx_cnt == 3) {
        subpx_cnt = 0;

        lv_color_t res_color;
#if LV_COLOR_16_SWAP == 0
        uint8_t bg_rgb[3] = { disp_buf_buf_tmp->ch.red, disp_buf_buf_tmp->ch.green, disp_buf_buf_tmp->ch.blue };
#else
        uint8_t bg_rgb[3] = { disp_buf_buf_tmp->ch.red,
          (disp_buf_buf_tmp->ch.green_h << 3) + disp_buf_buf_tmp->ch.green_l,
          disp_buf_buf_tmp->ch.blue };
#endif

#if LV_FONT_SUBPX_BGR
        res_color.ch.blue = (uint32_t)((uint32_t)txt_rgb[0] * font_rgb[0] + (bg_rgb[0] * (255 - font_rgb[0]))) >> 8;
        res_color.ch.red = (uint32_t)((uint32_t)txt_rgb[2] * font_rgb[2] + (bg_rgb[2] * (255 - font_rgb[2]))) >> 8;
#else
        res_color.ch.red = (uint32_t)((uint16_t)txt_rgb[0] * font_rgb[0] + (bg_rgb[0] * (255 - font_rgb[0]))) >> 8;
        res_color.ch.blue = (uint32_t)((uint16_t)txt_rgb[2] * font_rgb[2] + (bg_rgb[2] * (255 - font_rgb[2]))) >> 8;
#endif

#if LV_COLOR_16_SWAP == 0
        res_color.ch.green = (uint32_t)((uint32_t)txt_rgb[1] * font_rgb[1] + (bg_rgb[1] * (255 - font_rgb[1]))) >> 8;
#else
        uint8_t green = (uint32_t)((uint32_t)txt_rgb[1] * font_rgb[1] + (bg_rgb[1] * (255 - font_rgb[1]))) >> 8;
        res_color.ch.green_h = green >> 3;
        res_color.ch.green_l = green & 0x7;
#endif

#if LV_COLOR_DEPTH == 32
        res_color.ch.alpha = 0xff;
#endif

        if (font_rgb[0] == 0 && font_rgb[1] == 0 && font_rgb[2] == 0)
          mask_buf[mask_p] = LV_OPA_TRANSP;
        else
          mask_buf[mask_p] = LV_OPA_COVER;
        color_buf[mask_p] = res_color;

        /*Next mask byte*/
        mask_p++;
        disp_buf_buf_tmp++;
      }

      /*Go to the next column*/
      if (col_bit < (int32_t)(8 - bpp)) {
        col_bit += bpp;
        bitmask = bitmask >> bpp;
      } else {
        col_bit = 0;
        bitmask = bitmask_init;
        map_p++;
      }
    }

    /*Apply masks if any*/
    if (mask_any) {
      lv_draw_mask_res_t mask_res = lv_draw_mask_apply(mask_buf + mask_p_start, map_area.x1, map_area.y2,
          lv_area_get_width(&map_area));
      if (mask_res == LV_DRAW_MASK_RES_TRANSP) {
        lv_memset_00(mask_buf + mask_p_start, lv_area_get_width(&map_area));
      }
    }

    if ((int32_t)mask_p + (col_end - col_start) < mask_buf_size) {
      map_area.y2++;
    } else {
      _lv_blend_map(clip_area, &map_area, color_buf, mask_buf, LV_DRAW_MASK_RES_CHANGED, opa, blend_mode);

      map_area.y1 = map_area.y2 + 1;
      map_area.y2 = map_area.y1;
      mask_p = 0;
    }

    col_bit += ((box_w - col_end) + col_start) * bpp;

    map_p += (col_bit >> 3);
    col_bit = col_bit & 0x7;

    /*Next row in draw_buf*/
    disp_buf_buf_tmp += disp_buf_width - (col_end - col_start) / 3;
  }

  /*Flush the last part*/
  if (map_area.y1 != map_area.y2) {
    map_area.y2--;
    _lv_blend_map(clip_area, &map_area, color_buf, mask_buf, LV_DRAW_MASK_RES_CHANGED, opa, blend_mode);
  }

  lv_mem_buf_release(mask_buf);
  lv_mem_buf_release(color_buf);
}
#endif /*LV_DRAW_COMPLEX && LV_USE_FONT_SUBPX*/

/* Copied from lv_draw_img.c */

LV_ATTRIBUTE_FAST_MEM static lv_res_t lv_img_draw_core(const lv_area_t* coords, const lv_area_t* clip_area,
    const void* src,
    const lv_draw_img_dsc_t* draw_dsc)
{
  if (draw_dsc->opa <= LV_OPA_MIN)
    return LV_RES_OK;

  _lv_img_cache_entry_t* cdsc = _lv_img_cache_open(src, draw_dsc->recolor, draw_dsc->frame_id);

  if (cdsc == NULL)
    return LV_RES_INV;

  bool chroma_keyed = lv_img_cf_is_chroma_keyed(cdsc->dec_dsc.header.cf);
  bool alpha_byte = lv_img_cf_has_alpha(cdsc->dec_dsc.header.cf);

  if (cdsc->dec_dsc.error_msg != NULL) {
    LV_LOG_WARN("Image draw error");

    show_error(coords, clip_area, cdsc->dec_dsc.error_msg);
  }
  /*The decoder could open the image and gave the entire uncompressed image.
     *Just draw it!*/
  else if (cdsc->dec_dsc.img_data) {
    lv_area_t map_area_rot;
    lv_area_copy(&map_area_rot, coords);
    if (draw_dsc->angle || draw_dsc->zoom != LV_IMG_ZOOM_NONE) {
      int32_t w = lv_area_get_width(coords);
      int32_t h = lv_area_get_height(coords);

      _lv_img_buf_get_transformed_area(&map_area_rot, w, h, draw_dsc->angle, draw_dsc->zoom, &draw_dsc->pivot);

      map_area_rot.x1 += coords->x1;
      map_area_rot.y1 += coords->y1;
      map_area_rot.x2 += coords->x1;
      map_area_rot.y2 += coords->y1;
    }

    lv_area_t mask_com; /*Common area of mask and coords*/
    bool union_ok;
    union_ok = _lv_area_intersect(&mask_com, clip_area, &map_area_rot);
    /*Out of mask. There is nothing to draw so the image is drawn successfully.*/
    if (union_ok == false) {
      draw_cleanup(cdsc);
      return LV_RES_OK;
    }

    lv_draw_map(coords, &mask_com, cdsc->dec_dsc.img_data, draw_dsc, chroma_keyed, alpha_byte);
  }
  /*The whole uncompressed image is not available. Try to read it line-by-line*/
  else {
    lv_area_t mask_com; /*Common area of mask and coords*/
    bool union_ok;
    union_ok = _lv_area_intersect(&mask_com, clip_area, coords);
    /*Out of mask. There is nothing to draw so the image is drawn successfully.*/
    if (union_ok == false) {
      draw_cleanup(cdsc);
      return LV_RES_OK;
    }

    int32_t width = lv_area_get_width(&mask_com);

    uint8_t* buf = lv_mem_buf_get(lv_area_get_width(&mask_com) * LV_IMG_PX_SIZE_ALPHA_BYTE); /*+1 because of the possible alpha byte*/

    lv_area_t line;
    lv_area_copy(&line, &mask_com);
    lv_area_set_height(&line, 1);
    int32_t x = mask_com.x1 - coords->x1;
    int32_t y = mask_com.y1 - coords->y1;
    int32_t row;
    lv_res_t read_res;
    for (row = mask_com.y1; row <= mask_com.y2; row++) {
      lv_area_t mask_line;
      union_ok = _lv_area_intersect(&mask_line, clip_area, &line);
      if (union_ok == false)
        continue;

      read_res = lv_img_decoder_read_line(&cdsc->dec_dsc, x, y, width, buf);
      if (read_res != LV_RES_OK) {
        lv_img_decoder_close(&cdsc->dec_dsc);
        LV_LOG_WARN("Image draw can't read the line");
        lv_mem_buf_release(buf);
        draw_cleanup(cdsc);
        return LV_RES_INV;
      }

      lv_draw_map(&line, &mask_line, buf, draw_dsc, chroma_keyed, alpha_byte);
      line.y1++;
      line.y2++;
      y++;
    }
    lv_mem_buf_release(buf);
  }

  draw_cleanup(cdsc);
  return LV_RES_OK;
}

/**
 * Draw a color map to the display (image)
 * @param map_area coordinates the color map
 * @param clip_area the map will drawn only on this area  (truncated to draw_buf area)
 * @param map_p pointer to a lv_color_t array
 * @param draw_dsc pointer to an initialized `lv_draw_img_dsc_t` variable
 * @param chroma_key true: enable transparency of LV_IMG_LV_COLOR_TRANSP color pixels
 * @param alpha_byte true: extra alpha byte is inserted for every pixel
 */
LV_ATTRIBUTE_FAST_MEM static void lv_draw_map(const lv_area_t* map_area, const lv_area_t* clip_area,
    const uint8_t* map_p,
    const lv_draw_img_dsc_t* draw_dsc,
    bool chroma_key, bool alpha_byte)
{
  /*Use the clip area as draw area*/
  lv_area_t draw_area;
  lv_area_copy(&draw_area, clip_area);

  lv_disp_t* disp = _lv_refr_get_disp_refreshing();
  lv_disp_draw_buf_t* draw_buf = lv_disp_get_draw_buf(disp);
  const lv_area_t* disp_area = &draw_buf->area;

  /*Now `draw_area` has absolute coordinates.
     *Make it relative to `disp_area` to simplify draw to `disp_buf`*/
  draw_area.x1 -= disp_area->x1;
  draw_area.y1 -= disp_area->y1;
  draw_area.x2 -= disp_area->x1;
  draw_area.y2 -= disp_area->y1;

  bool mask_any = lv_draw_mask_is_any(&draw_area);
  /*The simplest case just copy the pixels into the draw_buf*/

  if (!mask_any && chroma_key == false) {
    if (_lv_map_gpu(clip_area, map_area, (lv_color_t*)map_p, draw_dsc, alpha_byte) == LV_RES_OK)
      return;
  }
  if (!mask_any && draw_dsc->angle == 0 && draw_dsc->zoom == LV_IMG_ZOOM_NONE && chroma_key == false && alpha_byte == false && draw_dsc->recolor_opa == LV_OPA_TRANSP) {
    _lv_blend_map(clip_area, map_area, (lv_color_t*)map_p, NULL, LV_DRAW_MASK_RES_FULL_COVER, draw_dsc->opa,
        draw_dsc->blend_mode);
  }

#if LV_USE_GPU_NXP_PXP
  /*Simple case without masking and transformations*/
  else if (!mask_any && draw_dsc->angle == 0 && draw_dsc->zoom == LV_IMG_ZOOM_NONE && alpha_byte == false && chroma_key == true && draw_dsc->recolor_opa == LV_OPA_TRANSP) { /*copy with color keying (+ alpha)*/
    lv_gpu_nxp_pxp_enable_color_key();
    _lv_blend_map(clip_area, map_area, (lv_color_t*)map_p, NULL, LV_DRAW_MASK_RES_FULL_COVER, draw_dsc->opa,
        draw_dsc->blend_mode);
    lv_gpu_nxp_pxp_disable_color_key();
  } else if (!mask_any && draw_dsc->angle == 0 && draw_dsc->zoom == LV_IMG_ZOOM_NONE && alpha_byte == false && chroma_key == false && draw_dsc->recolor_opa != LV_OPA_TRANSP) { /*copy with recolor (+ alpha)*/
    lv_gpu_nxp_pxp_enable_recolor(draw_dsc->recolor, draw_dsc->recolor_opa);
    _lv_blend_map(clip_area, map_area, (lv_color_t*)map_p, NULL, LV_DRAW_MASK_RES_FULL_COVER, draw_dsc->opa,
        draw_dsc->blend_mode);
    lv_gpu_nxp_pxp_disable_recolor();
  }
#endif
  /*In the other cases every pixel need to be checked one-by-one*/
  else {
    //#if LV_DRAW_COMPLEX
    /*The pixel size in byte is different if an alpha byte is added too*/
    uint8_t px_size_byte = alpha_byte ? LV_IMG_PX_SIZE_ALPHA_BYTE : sizeof(lv_color_t);

    /*Go to the first displayed pixel of the map*/
    int32_t map_w = lv_area_get_width(map_area);
    const uint8_t* map_buf_tmp = map_p;
    map_buf_tmp += map_w * (draw_area.y1 - (map_area->y1 - disp_area->y1)) * px_size_byte;
    map_buf_tmp += (draw_area.x1 - (map_area->x1 - disp_area->x1)) * px_size_byte;

    lv_color_t c;
    lv_color_t chroma_keyed_color = LV_COLOR_CHROMA_KEY;
    uint32_t px_i = 0;

    const uint8_t* map_px;

    lv_coord_t draw_area_h = lv_area_get_height(&draw_area);
    lv_coord_t draw_area_w = lv_area_get_width(&draw_area);

    lv_area_t blend_area;
    blend_area.x1 = draw_area.x1 + disp_area->x1;
    blend_area.x2 = blend_area.x1 + draw_area_w - 1;
    blend_area.y1 = disp_area->y1 + draw_area.y1;
    blend_area.y2 = blend_area.y1;

    bool transform = draw_dsc->angle != 0 || draw_dsc->zoom != LV_IMG_ZOOM_NONE ? true : false;
    /*Simple ARGB image. Handle it as special case because it's very common*/
    if (!mask_any && !transform && !chroma_key && draw_dsc->recolor_opa == LV_OPA_TRANSP && alpha_byte) {
#if LV_USE_GPU_STM32_DMA2D && LV_COLOR_DEPTH == 32
      /*Blend ARGB images directly*/
      if (lv_area_get_size(&draw_area) > 240) {
        int32_t disp_w = lv_area_get_width(disp_area);
        lv_color_t* disp_buf = draw_buf->buf_act;
        lv_color_t* disp_buf_first = disp_buf + disp_w * draw_area.y1 + draw_area.x1;
        lv_gpu_stm32_dma2d_blend(disp_buf_first, disp_w, (const lv_color_t*)map_buf_tmp, draw_dsc->opa, map_w, draw_area_w,
            draw_area_h);
        return;
      }
#endif
      uint32_t hor_res = (uint32_t)lv_disp_get_hor_res(disp);
      uint32_t mask_buf_size = lv_area_get_size(&draw_area) > (uint32_t)hor_res ? hor_res : lv_area_get_size(&draw_area);
      lv_color_t* map2 = lv_mem_buf_get(mask_buf_size * sizeof(lv_color_t));
      lv_opa_t* mask_buf = lv_mem_buf_get(mask_buf_size);

      int32_t x;
      int32_t y;
      for (y = 0; y < draw_area_h; y++) {
        map_px = map_buf_tmp;
        for (x = 0; x < draw_area_w; x++, map_px += px_size_byte, px_i++) {
          lv_opa_t px_opa = map_px[LV_IMG_PX_SIZE_ALPHA_BYTE - 1];
          mask_buf[px_i] = px_opa;
          if (px_opa) {
#if LV_COLOR_DEPTH == 8 || LV_COLOR_DEPTH == 1
            map2[px_i].full = map_px[0];
#elif LV_COLOR_DEPTH == 16
            map2[px_i].full = map_px[0] + (map_px[1] << 8);
#elif LV_COLOR_DEPTH == 32
            map2[px_i].full = *((uint32_t*)map_px);
#endif
          }
#if LV_COLOR_DEPTH == 32
          map2[px_i].ch.alpha = 0xFF;
#endif
        }

        map_buf_tmp += map_w * px_size_byte;
        if (px_i + draw_area_w < mask_buf_size) {
          blend_area.y2++;
        } else {
          _lv_blend_map(clip_area, &blend_area, map2, mask_buf, LV_DRAW_MASK_RES_CHANGED, draw_dsc->opa, draw_dsc->blend_mode);

          blend_area.y1 = blend_area.y2 + 1;
          blend_area.y2 = blend_area.y1;

          px_i = 0;
        }
      }
      /*Flush the last part*/
      if (blend_area.y1 != blend_area.y2) {
        blend_area.y2--;
        _lv_blend_map(clip_area, &blend_area, map2, mask_buf, LV_DRAW_MASK_RES_CHANGED, draw_dsc->opa, draw_dsc->blend_mode);
      }

      lv_mem_buf_release(mask_buf);
      lv_mem_buf_release(map2);
    }
    /*Most complicated case: transform or other mask or chroma keyed*/
    else {
      /*Build the image and a mask line-by-line*/
      uint32_t hor_res = (uint32_t)lv_disp_get_hor_res(disp);
      uint32_t mask_buf_size = lv_area_get_size(&draw_area) > hor_res ? hor_res : lv_area_get_size(&draw_area);
      lv_color_t* map2 = lv_mem_buf_get(mask_buf_size * sizeof(lv_color_t));
      lv_opa_t* mask_buf = lv_mem_buf_get(mask_buf_size);

#if LV_DRAW_COMPLEX
      lv_img_transform_dsc_t trans_dsc;
      lv_memset_00(&trans_dsc, sizeof(lv_img_transform_dsc_t));
      if (transform) {
        lv_img_cf_t cf = LV_IMG_CF_TRUE_COLOR;
        if (alpha_byte)
          cf = LV_IMG_CF_TRUE_COLOR_ALPHA;
        else if (chroma_key)
          cf = LV_IMG_CF_TRUE_COLOR_CHROMA_KEYED;

        trans_dsc.cfg.angle = draw_dsc->angle;
        trans_dsc.cfg.zoom = draw_dsc->zoom;
        trans_dsc.cfg.src = map_p;
        trans_dsc.cfg.src_w = map_w;
        trans_dsc.cfg.src_h = lv_area_get_height(map_area);
        ;
        trans_dsc.cfg.cf = cf;
        trans_dsc.cfg.pivot_x = draw_dsc->pivot.x;
        trans_dsc.cfg.pivot_y = draw_dsc->pivot.y;
        trans_dsc.cfg.color = draw_dsc->recolor;
        trans_dsc.cfg.antialias = draw_dsc->antialias;

        _lv_img_buf_transform_init(&trans_dsc);
      }
#endif
      uint16_t recolor_premult[3] = { 0 };
      lv_opa_t recolor_opa_inv = 255 - draw_dsc->recolor_opa;
      if (draw_dsc->recolor_opa != 0) {
        lv_color_premult(draw_dsc->recolor, draw_dsc->recolor_opa, recolor_premult);
      }

      lv_draw_mask_res_t mask_res;
      mask_res = (alpha_byte || chroma_key || draw_dsc->angle || draw_dsc->zoom != LV_IMG_ZOOM_NONE) ? LV_DRAW_MASK_RES_CHANGED : LV_DRAW_MASK_RES_FULL_COVER;

      /*Prepare the `mask_buf`if there are other masks*/
      if (mask_any) {
        lv_memset_ff(mask_buf, mask_buf_size);
      }

      int32_t x;
      int32_t y;
#if LV_DRAW_COMPLEX
      int32_t rot_y = disp_area->y1 + draw_area.y1 - map_area->y1;
#endif
      for (y = 0; y < draw_area_h; y++) {
        map_px = map_buf_tmp;
#if LV_DRAW_COMPLEX
        uint32_t px_i_start = px_i;
        int32_t rot_x = disp_area->x1 + draw_area.x1 - map_area->x1;
#endif

        for (x = 0; x < draw_area_w; x++, map_px += px_size_byte, px_i++) {

#if LV_DRAW_COMPLEX
          if (transform) {

            /*Transform*/
            bool ret;
            ret = _lv_img_buf_transform(&trans_dsc, rot_x + x, rot_y + y);
            if (ret == false) {
              mask_buf[px_i] = LV_OPA_TRANSP;
              continue;
            } else {
              mask_buf[px_i] = trans_dsc.res.opa;
              c.full = trans_dsc.res.color.full;
            }
          }
          /*No transform*/
          else
#endif
          {
            if (alpha_byte) {
              lv_opa_t px_opa = map_px[LV_IMG_PX_SIZE_ALPHA_BYTE - 1];
              mask_buf[px_i] = px_opa;
              if (px_opa == 0) {
#if LV_COLOR_DEPTH == 32
                map2[px_i].full = 0;
#endif
                continue;
              }
            } else {
              mask_buf[px_i] = 0xFF;
            }

#if LV_COLOR_DEPTH == 1
            c.full = map_px[0];
#elif LV_COLOR_DEPTH == 8
            c.full = map_px[0];
#elif LV_COLOR_DEPTH == 16
            c.full = map_px[0] + (map_px[1] << 8);
#elif LV_COLOR_DEPTH == 32
            c.full = *((uint32_t*)map_px);
            c.ch.alpha = 0xFF;
#endif
            if (chroma_key) {
              if (c.full == chroma_keyed_color.full) {
                mask_buf[px_i] = LV_OPA_TRANSP;
#if LV_COLOR_DEPTH == 32
                map2[px_i].full = 0;
#endif
                continue;
              }
            }
          }
          if (draw_dsc->recolor_opa != 0) {
            c = lv_color_mix_premult(recolor_premult, c, recolor_opa_inv);
          }

          map2[px_i].full = c.full;
        }
#if LV_DRAW_COMPLEX
        /*Apply the masks if any*/
        if (mask_any) {
          lv_draw_mask_res_t mask_res_sub;
          mask_res_sub = lv_draw_mask_apply(mask_buf + px_i_start, draw_area.x1 + draw_buf->area.x1,
              y + draw_area.y1 + draw_buf->area.y1,
              draw_area_w);
          if (mask_res_sub == LV_DRAW_MASK_RES_TRANSP) {
            lv_memset_00(mask_buf + px_i_start, draw_area_w);
            mask_res = LV_DRAW_MASK_RES_CHANGED;
          } else if (mask_res_sub == LV_DRAW_MASK_RES_CHANGED) {
            mask_res = LV_DRAW_MASK_RES_CHANGED;
          }
        }
#endif

        map_buf_tmp += map_w * px_size_byte;
        if (px_i + draw_area_w < mask_buf_size) {
          blend_area.y2++;
        } else {

          _lv_blend_map(clip_area, &blend_area, map2, mask_buf, mask_res, draw_dsc->opa, draw_dsc->blend_mode);

          blend_area.y1 = blend_area.y2 + 1;
          blend_area.y2 = blend_area.y1;

          px_i = 0;
          mask_res = (alpha_byte || chroma_key || draw_dsc->angle || draw_dsc->zoom != LV_IMG_ZOOM_NONE) ? LV_DRAW_MASK_RES_CHANGED : LV_DRAW_MASK_RES_FULL_COVER;

          /*Prepare the `mask_buf`if there are other masks*/
          if (mask_any) {
            lv_memset_ff(mask_buf, mask_buf_size);
          }
        }
      }

      /*Flush the last part*/
      if (blend_area.y1 != blend_area.y2) {
        blend_area.y2--;
        _lv_blend_map(clip_area, &blend_area, map2, mask_buf, mask_res, draw_dsc->opa, draw_dsc->blend_mode);
      }

      lv_mem_buf_release(mask_buf);
      lv_mem_buf_release(map2);
    }
  }
}

static void show_error(const lv_area_t* coords, const lv_area_t* clip_area, const char* msg)
{
  lv_draw_rect_dsc_t rect_dsc;
  lv_draw_rect_dsc_init(&rect_dsc);
  rect_dsc.bg_color = lv_color_white();
  lv_draw_rect(coords, clip_area, &rect_dsc);

  lv_draw_label_dsc_t label_dsc;
  lv_draw_label_dsc_init(&label_dsc);
  lv_draw_label(coords, clip_area, &label_dsc, msg, NULL);
}

static void draw_cleanup(_lv_img_cache_entry_t* cache)
{
  /*Automatically close images with no caching*/
#if LV_IMG_CACHE_DEF_SIZE == 0
  lv_img_decoder_close(&cache->dec_dsc);
#else
  LV_UNUSED(cache);
#endif
}

#endif /* LV_USE_EXTERNAL_RENDERER */

/***
 * Fills vg_lite_buffer_t structure according given parameters.
 * @param[out] dst Buffer structure to be filled
 * @param[in] width Width of buffer in pixels
 * @param[in] height Height of buffer in pixels
 * @param[in] stride Stride of the buffer in bytes
 * @param[in] ptr Pointer to the buffer (must be aligned according VG-Lite requirements)
 */
static lv_res_t init_vg_buf(void* vdst, uint32_t width, uint32_t height, uint32_t stride, void* ptr, uint8_t format, bool source)
{
  vg_lite_buffer_t* dst = vdst;
  // LV_LOG_WARN("width:%d height:%d stride:%d ptr:%p format:%d\n", width, height, stride, ptr, format);
  if (source && (width & 0xF)) { /*Test for stride alignment*/
    LV_LOG_ERROR("Buffer stride (%d bytes) not aligned to 16px.", stride);
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

static void gpu_wait()
{
  // usleep(10);
}

/****************************************************************************
 * Global Functions
 ****************************************************************************/
#if LV_USE_EXTERNAL_RENDERER

/* Copied from lv_draw_blend.c */

/**
 * Fill and area in the display buffer.
 * @param clip_area clip the fill to this area  (absolute coordinates)
 * @param fill_area fill this area  (absolute coordinates) (should be clipped)
 * @param color fill color
 * @param mask a mask to apply on the fill (uint8_t array with 0x00..0xff values).
 *             Relative to fill area but its width is truncated to clip area.
 * @param mask_res LV_MASK_RES_COVER: the mask has only 0xff values (no mask),
 *                 LV_MASK_RES_TRANSP: the mask has only 0x00 values (full transparent),
 *                 LV_MASK_RES_CHANGED: the mask has mixed values
 * @param opa overall opacity in 0x00..0xff range
 * @param mode blend mode from `lv_blend_mode_t`
 */
LV_ATTRIBUTE_FAST_MEM void _lv_blend_fill(const lv_area_t* clip_area, const lv_area_t* fill_area,
    lv_color_t color, lv_opa_t* mask, lv_draw_mask_res_t mask_res, lv_opa_t opa,
    lv_blend_mode_t mode)
{
  /*Do not draw transparent things*/
  if (opa < LV_OPA_MIN)
    return;
  if (mask_res == LV_DRAW_MASK_RES_TRANSP)
    return;

  lv_disp_t* disp = _lv_refr_get_disp_refreshing();
  lv_disp_draw_buf_t* draw_buf = lv_disp_get_draw_buf(disp);
  const lv_area_t* disp_area = &draw_buf->area;
  lv_color_t* disp_buf = draw_buf->buf_act;

  if (disp->driver->gpu_wait_cb)
    disp->driver->gpu_wait_cb(disp->driver);

  /*Get clipped fill area which is the real draw area.
     *It is always the same or inside `fill_area`*/
  lv_area_t draw_area;
  if (!_lv_area_intersect(&draw_area, clip_area, fill_area))
    return;

  /*Now `draw_area` has absolute coordinates.
     *Make it relative to `disp_area` to simplify the drawing to `disp_buf`*/
  lv_area_move(&draw_area, -disp_area->x1, -disp_area->y1);

  /*Round the values in the mask if anti-aliasing is disabled*/
  if (mask && disp->driver->antialiasing == 0 && mask) {
    int32_t mask_w = lv_area_get_width(&draw_area);
    int32_t i;
    for (i = 0; i < mask_w; i++)
      mask[i] = mask[i] > 128 ? LV_OPA_COVER : LV_OPA_TRANSP;
  }

  if (disp->driver->set_px_cb) {
    fill_set_px(disp_area, disp_buf, &draw_area, color, opa, mask, mask_res);
  } else if (mode == LV_BLEND_MODE_NORMAL) {
    fill_normal(disp_area, disp_buf, &draw_area, color, opa, mask, mask_res);
  }
#if LV_DRAW_COMPLEX
  else {
    fill_blended(disp_area, disp_buf, &draw_area, color, opa, mask, mask_res, mode);
  }
#endif
}

/**
 * Copy a map (image) to a display buffer.
 * @param clip_area clip the map to this area (absolute coordinates)
 * @param map_area area of the image  (absolute coordinates)
 * @param map_buf a pixels of the map (image)
 * @param mask a mask to apply on every pixel (uint8_t array with 0x00..0xff values).
 *                Relative to map area but its width is truncated to clip area.
 * @param mask_res LV_MASK_RES_COVER: the mask has only 0xff values (no mask),
 *                 LV_MASK_RES_TRANSP: the mask has only 0x00 values (full transparent),
 *                 LV_MASK_RES_CHANGED: the mask has mixed values
 * @param opa  overall opacity in 0x00..0xff range
 * @param mode blend mode from `lv_blend_mode_t`
 */
LV_ATTRIBUTE_FAST_MEM void _lv_blend_map(const lv_area_t* clip_area, const lv_area_t* map_area,
    const lv_color_t* map_buf,
    lv_opa_t* mask, lv_draw_mask_res_t mask_res,
    lv_opa_t opa, lv_blend_mode_t mode)
{
  /*Do not draw transparent things*/
  if (opa < LV_OPA_MIN)
    return;
  if (mask_res == LV_DRAW_MASK_RES_TRANSP)
    return;

  /*Get clipped fill area which is the real draw area.
     *It is always the same or inside `fill_area`*/
  lv_area_t draw_area;
  bool is_common;
  is_common = _lv_area_intersect(&draw_area, clip_area, map_area);
  if (!is_common)
    return;

  lv_disp_t* disp = _lv_refr_get_disp_refreshing();
  lv_disp_draw_buf_t* draw_buf = lv_disp_get_draw_buf(disp);
  const lv_area_t* disp_area = &draw_buf->area;
  lv_color_t* disp_buf = draw_buf->buf_act;

  if (disp->driver->gpu_wait_cb)
    disp->driver->gpu_wait_cb(disp->driver);

  /*Now `draw_area` has absolute coordinates.
     *Make it relative to `disp_area` to simplify draw to `disp_buf`*/
  draw_area.x1 -= disp_area->x1;
  draw_area.y1 -= disp_area->y1;
  draw_area.x2 -= disp_area->x1;
  draw_area.y2 -= disp_area->y1;

  /*Round the values in the mask if anti-aliasing is disabled*/
  if (mask && disp->driver->antialiasing == 0) {
    int32_t mask_w = lv_area_get_width(&draw_area);
    int32_t i;
    for (i = 0; i < mask_w; i++)
      mask[i] = mask[i] > 128 ? LV_OPA_COVER : LV_OPA_TRANSP;
  }
  if (disp->driver->set_px_cb) {
    map_set_px(disp_area, disp_buf, &draw_area, map_area, map_buf, opa, mask, mask_res);
  } else if (mode == LV_BLEND_MODE_NORMAL) {
    map_normal(disp_area, disp_buf, &draw_area, map_area, map_buf, opa, mask, mask_res);
  }
#if LV_DRAW_COMPLEX
  else {
    map_blended(disp_area, disp_buf, &draw_area, map_area, map_buf, opa, mask, mask_res, mode);
  }
#endif
}

/* Copied from lv_draw_rect.c */

/**
 * Draw a rectangle
 * @param coords the coordinates of the rectangle
 * @param mask the rectangle will be drawn only in this mask
 * @param dsc pointer to an initialized `lv_draw_rect_dsc_t` variable
 */
void lv_draw_rect(const lv_area_t* coords, const lv_area_t* clip, const lv_draw_rect_dsc_t* dsc)
{
  if (lv_area_get_height(coords) < 1 || lv_area_get_width(coords) < 1)
    return;
#if LV_DRAW_COMPLEX
  draw_shadow(coords, clip, dsc);
#endif

  draw_bg(coords, clip, dsc);
  draw_bg_img(coords, clip, dsc);

  draw_border(coords, clip, dsc);

  draw_outline(coords, clip, dsc);

  LV_ASSERT_MEM_INTEGRITY();
}

/* Copied from lv_draw_label.c */

/**
 * Draw a letter in the Virtual Display Buffer
 * @param pos_p left-top coordinate of the latter
 * @param mask_p the letter will be drawn only on this area  (truncated to draw_buf area)
 * @param font_p pointer to font
 * @param letter a letter to draw
 * @param color color of letter
 * @param opa opacity of letter (0..255)
 */
LV_ATTRIBUTE_FAST_MEM void lv_draw_letter(const lv_point_t* pos_p, const lv_area_t* clip_area,
    const lv_font_t* font_p,
    uint32_t letter,
    lv_color_t color, lv_opa_t opa, lv_blend_mode_t blend_mode)
{
  if (opa < LV_OPA_MIN)
    return;
  if (opa > LV_OPA_MAX)
    opa = LV_OPA_COVER;

  if (font_p == NULL) {
    LV_LOG_WARN("lv_draw_letter: font is NULL");
    return;
  }

  lv_font_glyph_dsc_t g;
  bool g_ret = lv_font_get_glyph_dsc(font_p, &g, letter, '\0');
  if (g_ret == false) {
    /*Add warning if the dsc is not found
         *but do not print warning for non printable ASCII chars (e.g. '\n')*/
    if (letter >= 0x20 && letter != 0xf8ff && /*LV_SYMBOL_DUMMY*/
        letter != 0x200c) { /*ZERO WIDTH NON-JOINER*/
      LV_LOG_WARN("lv_draw_letter: glyph dsc. not found for U+%X", (unsigned int)letter);
    }
    return;
  }

  /*Don't draw anything if the character is empty. E.g. space*/
  if ((g.box_h == 0) || (g.box_w == 0))
    return;

  int32_t pos_x = pos_p->x + g.ofs_x;
  int32_t pos_y = pos_p->y + (font_p->line_height - font_p->base_line) - g.box_h - g.ofs_y;

  /*If the letter is completely out of mask don't draw it*/
  if (pos_x + g.box_w < clip_area->x1 || pos_x > clip_area->x2 || pos_y + g.box_h < clip_area->y1 || pos_y > clip_area->y2) {
    return;
  }

  const uint8_t* map_p = lv_font_get_glyph_bitmap(font_p, letter);
  if (map_p == NULL) {
    LV_LOG_WARN("lv_draw_letter: character's bitmap not found");
    return;
  }

  if (font_p->subpx) {
#if LV_DRAW_COMPLEX && LV_USE_FONT_SUBPX
    draw_letter_subpx(pos_x, pos_y, &g, clip_area, map_p, color, opa, blend_mode);
#else
    LV_LOG_WARN("Can't draw sub-pixel rendered letter because LV_USE_FONT_SUBPX == 0 in lv_conf.h");
#endif
  } else {
    draw_letter_normal(pos_x, pos_y, &g, clip_area, map_p, color, opa, blend_mode);
  }
}

/* Copied from lv_draw_img.c */

/**
 * Draw an image
 * @param coords the coordinates of the image
 * @param mask the image will be drawn only in this area
 * @param src pointer to a lv_color_t array which contains the pixels of the image
 * @param dsc pointer to an initialized `lv_draw_img_dsc_t` variable
 */
void lv_draw_img(const lv_area_t* coords, const lv_area_t* mask, const void* src, const lv_draw_img_dsc_t* dsc)
{
  if (src == NULL) {
    LV_LOG_WARN("Image draw: src is NULL");
    show_error(coords, mask, "No\ndata");
    return;
  }

  if (dsc->opa <= LV_OPA_MIN)
    return;

  lv_res_t res;
  res = lv_img_draw_core(coords, mask, src, dsc);

  if (res == LV_RES_INV) {
    LV_LOG_WARN("Image draw error");
    show_error(coords, mask, "No\ndata");
    return;
  }
}

#endif /* LV_USE_EXTERNAL_RENDERER */

/****************************************************************************
 * Name: _lv_fill_gpu
 *
 * Description:
 *   Use GPU to fill rectangular area in buffer.
 *
 * Input Parameters:
 *   @param[in] dest_buf Destination buffer pointer (must be aligned on 32 bytes)
 *   @param[in] dest_width Destination buffer width in pixels (must be aligned on 16 px)
 *   @param[in] dest_height Destination buffer height in pixels
 *   @param[in] fill_area Area to be filled
 *   @param[in] color Fill color
 *   @param[in] opa Opacity (255 = full, 128 = 50% background/50% color, 0 = no fill)
 *
 * Returned Value:
 *   @retval LV_RES_OK Fill completed
 *   @retval LV_RES_INV Error occurred
 *
 ****************************************************************************/

lv_res_t _lv_fill_gpu(lv_color_t* dest_buf, lv_coord_t dest_width, lv_coord_t dest_height,
    const lv_area_t* fill_area, lv_color_t color, lv_opa_t opa)
{
  vg_lite_buffer_t rt;
  vg_lite_rectangle_t rect;
  vg_lite_error_t err = VG_LITE_SUCCESS;
  lv_color32_t col32 = { .full = lv_color_to32(color) }; /*Convert color to RGBA8888*/
  lv_disp_t* disp = _lv_refr_get_disp_refreshing();

  if (init_vg_buf(&rt, (uint32_t)dest_width, (uint32_t)dest_height, (uint32_t)dest_width * sizeof(lv_color_t),
          (const lv_color_t*)dest_buf, VGLITE_PX_FMT, false)
      != LV_RES_OK) {
    LV_LOG_ERROR("init_vg_buf reported error. Fill failed.");
    return LV_RES_INV;
  }

  if (opa >= (lv_opa_t)LV_OPA_MAX) { /*Opaque fill*/
    rect.x = fill_area->x1;
    rect.y = fill_area->y1;
    rect.width = (int32_t)fill_area->x2 - (int32_t)fill_area->x1 + 1;
    rect.height = (int32_t)fill_area->y2 - (int32_t)fill_area->y1 + 1;

    if (disp != NULL && disp->driver->clean_dcache_cb != NULL) { /*Clean & invalidate cache*/
      disp->driver->clean_dcache_cb(disp->driver);
    }

    err = vg_lite_clear(&rt, &rect, col32.full);
    if (err != VG_LITE_SUCCESS) {
      LV_LOG_ERROR("vg_lite_clear reported error. Fill failed.");
      return LV_RES_INV;
    }
    err = vg_lite_finish();
    if (err != VG_LITE_SUCCESS) {
      LV_LOG_ERROR("vg_lite_finish reported error. Fill failed.");
      return LV_RES_INV;
    }
  } else { /*fill with transparency*/

    vg_lite_path_t path;
    lv_color32_t colMix;
    int16_t path_data[] = { /*VG rectangular path*/
      VLC_OP_MOVE, fill_area->x1, fill_area->y1,
      VLC_OP_LINE, fill_area->x2 + 1, fill_area->y1,
      VLC_OP_LINE, fill_area->x2 + 1, fill_area->y2 + 1,
      VLC_OP_LINE, fill_area->x1, fill_area->y2 + 1,
      VLC_OP_LINE, fill_area->x1, fill_area->y1,
      VLC_OP_END
    };

    err = vg_lite_init_path(&path, VG_LITE_S16, VG_LITE_LOW, sizeof(path_data), path_data,
        (vg_lite_float_t)fill_area->x1, (vg_lite_float_t)fill_area->y1, ((vg_lite_float_t)fill_area->x2) + 1.0f,
        ((vg_lite_float_t)fill_area->y2) + 1.0f);
    if (err != VG_LITE_SUCCESS) {
      LV_LOG_ERROR("vg_lite_init_path() failed.");
      return LV_RES_INV;
    }

    colMix.ch.red = (uint8_t)(((uint16_t)col32.ch.red * opa) >> 8); /*Pre-multiply color*/
    colMix.ch.green = (uint8_t)(((uint16_t)col32.ch.green * opa) >> 8);
    colMix.ch.blue = (uint8_t)(((uint16_t)col32.ch.blue * opa) >> 8);
    colMix.ch.alpha = opa;

    if ((disp != NULL) && (disp->driver->clean_dcache_cb != NULL)) { /*Clean & invalidate cache*/
      disp->driver->clean_dcache_cb(disp->driver);
    }

    vg_lite_matrix_t matrix;
    vg_lite_identity(&matrix);

    /*Draw rectangle*/
    err = vg_lite_draw(&rt, &path, VG_LITE_FILL_EVEN_ODD, &matrix, VG_LITE_BLEND_SRC_OVER, colMix.full);
    if (err != VG_LITE_SUCCESS) {
      LV_LOG_ERROR("vg_lite_draw() failed.");
      vg_lite_clear_path(&path);
      return LV_RES_INV;
    }

    err = vg_lite_finish();
    if (err != VG_LITE_SUCCESS) {
      LV_LOG_ERROR("vg_lite_finish() failed.");
      return LV_RES_INV;
    }

    err = vg_lite_clear_path(&path);
    if (err != VG_LITE_SUCCESS) {
      LV_LOG_ERROR("vg_lite_clear_path() failed.");
      return LV_RES_INV;
    }
  }

  if (err == VG_LITE_SUCCESS) {
    return LV_RES_OK;
  } else {
    LV_LOG_ERROR("VG Lite Fill failed.");
    return LV_RES_INV;
  }
}

/****************************************************************************
 * Name: _lv_map_gpu
 *
 * Description:
 *   Use GPU to draw a picture, optionally transformed.
 *
 * Input Parameters:
 *   @param[in] clip_area Clip output to this area
 *   @param[in] map_area The location of image BEFORE transformation
 *   @param[in] map_buf Source image buffer address
 *   @param[in] lv_draw_img_dsc_t Draw image descriptor
 *   @param[in] alpha_byte Alpha-byte color format, such as BGRA5658
 *
 * Returned Value:
 *   @retval LV_RES_OK Blit completed
 *   @retval LV_RES_INV Error occurred
 *
 ****************************************************************************/

LV_ATTRIBUTE_FAST_MEM lv_res_t _lv_map_gpu(const lv_area_t* clip_area, const lv_area_t* map_area,
    const lv_color_t* map_buf, const lv_draw_img_dsc_t* draw_dsc,
    bool alpha_byte)
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
  LV_LOG_WARN("clip:%d %d %d %d map:%d %d %d %d ang:%d zoom:%d pivot:(%d %d) opa:%d\n", clip_area->x1, clip_area->y1, clip_area->x2, clip_area->y2, map_area->x1, map_area->y1, map_area->x2, map_area->y2, angle, zoom, pivot.x, pivot.y, opa);
  lv_disp_t* disp = _lv_refr_get_disp_refreshing();
  lv_disp_draw_buf_t* draw_buf = lv_disp_get_draw_buf(disp);
  const lv_area_t* disp_area = &draw_buf->area;
  lv_color_t* disp_buf = draw_buf->buf_act;
  int32_t disp_w = lv_area_get_width(disp_area);
  int32_t disp_h = lv_area_get_height(disp_area);
  int32_t map_w = lv_area_get_width(map_area);
  int32_t map_h = lv_area_get_height(map_area);
  vg_lite_buffer_t dst_vgbuf;
  LV_LOG_WARN("disp_area:%d %d %d %d\n", disp_area->x1, disp_area->y1, disp_area->x2, disp_area->y2);

  init_vg_buf(&dst_vgbuf, disp_w, disp_h, disp_w * sizeof(lv_color_t), disp_buf, VGLITE_PX_FMT, false);
  uint32_t rect[4];
  lv_area_t map_tf, draw_area;
  lv_area_copy(&draw_area, clip_area);
  lv_area_move(&draw_area, -disp_area->x1, -disp_area->y1);

  _lv_img_buf_get_transformed_area(&map_tf, map_w, map_h, angle, zoom, &pivot);
  LV_LOG_WARN("map_tf:%d %d %d %d\n", map_tf.x1, map_tf.y1, map_tf.x2, map_tf.y2);
  lv_area_move(&map_tf, map_area->x1 - disp_area->x1, map_area->y1 - disp_area->y1);
  LV_LOG_WARN("after move:%d %d %d %d\n", map_tf.x1, map_tf.y1, map_tf.x2, map_tf.y2);
  if (_lv_area_intersect(&draw_area, &draw_area, &map_tf) == false) {
    return LV_RES_OK;
  }
  if ((map_tf.x1 < 0 || map_tf.y1 < 0 || map_tf.x2 > disp_area->x2 || map_tf.y2 > disp_area->y2)) {
    LV_LOG_ERROR("Transformed image clipped, fallback to SW for now\n");
    return LV_RES_INV;
  }
  LV_LOG_WARN("draw_area:%d %d %d %d\n", draw_area.x1, draw_area.y1, draw_area.x2, draw_area.y2);
  int32_t draw_area_w = lv_area_get_width(&draw_area);
  int32_t draw_area_h = lv_area_get_height(&draw_area);
  if (lv_area_get_size(&draw_area) < GPU_SIZE_LIMIT) {
    LV_LOG_WARN("GPU blit failed: too small");
    return LV_RES_INV;
  }

  void* mem = map_buf;

  if (
#if LV_COLOR_DEPTH == 16
      alpha_byte == true ||
#endif
      init_vg_buf(&src_vgbuf, map_w, map_h, map_w * sizeof(lv_color_t), map_buf, VGLITE_PX_FMT, true) != LV_RES_OK) {
    int32_t vgbuf_w = ALIGN_UP(map_w, 16);
    int32_t vgbuf_stride = vgbuf_w * sizeof(lv_color_t);
    uint8_t vgbuf_format = VGLITE_PX_FMT;
    int32_t map_stride = map_w * sizeof(lv_color_t);
#if LV_COLOR_DEPTH == 16
    if (alpha_byte) {
      vgbuf_stride = vgbuf_w * sizeof(lv_color32_t);
      vgbuf_format = VG_LITE_BGRA8888;
      map_stride = map_w * LV_IMG_PX_SIZE_ALPHA_BYTE;
    }
#endif
    int error;
    mem = malloc(map_h * vgbuf_stride);
    // CHECK_ERROR(vg_lite_allocate(&src_vgbuf));
    if (mem != NULL) {
      init_vg_buf(&src_vgbuf, vgbuf_w, map_h, vgbuf_stride, mem, vgbuf_format, true);
      uint8_t* px_buf = src_vgbuf.memory;
      uint8_t* px_map = map_buf;
#if LV_COLOR_DEPTH == 16
      if (alpha_byte) {
#ifdef CONFIG_ARM_HAVE_MVE

        int32_t blkCnt;
        const uint32_t _maskA[4] = { 0xff000000, 0xff0000, 0xff00, 0xff };
        const uint32_t _maskRB[4] = { 0xf8000000, 0xf80000, 0xf800, 0xf8 };
        const uint32_t _maskG[4] = { 0xfc000000, 0xfc0000, 0xfc00, 0xfc };
        const uint32_t _shiftC[4] = { 0x1, 0x100, 0x10000, 0x1000000 };

        for (int_fast16_t y = 0; y < map_h; y++) {
          const uint16_t* phwSource = px_map;
          uint32_t* pwTarget = px_buf;

          blkCnt = map_w;
          while (!IS_ALIGNED(phwSource, 4)) {
            bgra5658_to_8888(phwSource, pwTarget);
            phwSource = (uint8_t*)phwSource + 3;
            pwTarget++;
            blkCnt--;
          }
// #define USE_MVE_INTRINSICS (disabled due to intrinsics being much slower than hand-written asm)
#ifdef USE_MVE_INTRINSICS
          uint32x4_t maskA = vldrwq_u32(_maskA);
          uint32x4_t maskRB = vldrwq_u32(_maskRB);
          uint32x4_t maskG = vldrwq_u32(_maskG);
          uint32x4_t shiftC = vldrwq_u32(_shiftC);
          do {
            mve_pred16_t tailPred = vctp32q(blkCnt);

            /* load a vector of 4 bgra5658 pixels (residuals are processed in the next loop) */
            uint32x4_t vecIn = vld1q_z_u32((const uint32_t*)phwSource, tailPred);
            /* extract individual channels and place them in high 8bits (P=GlB, Q=RGh) */

            uint32_t carry = 0;
            vecIn = vshlcq(vecIn, &carry, 8); /* |***A|QPAQ|PAQP|AQP0| */
            uint32x4_t vecA = vandq(vecIn, maskA); /* |000A|00A0|0A00|A000| */
            vecA = vmulq(vecA, shiftC); /* |A000|A000|A000|A000| */
            vecIn = vshlcq(vecIn, &carry, 8); /* |**AQ|PAQP|AQPA|QP**| */
            uint32x4_t vecR = vandq(vecIn, maskRB); /* |000R|00R0|0R00|R000| */
            vecR = vmulq(vecR, shiftC); /* |R000|R000|R000|R000| */
            vecIn = vshlcq(vecIn, &carry, 5); /* Similar operation on G channel */
            uint32x4_t vecG = vandq(vecIn, maskG);
            vecG = vmulq(vecG, shiftC);
            vecIn = vshlcq(vecIn, &carry, 6);
            uint32x4_t vecB = vandq(vecIn, maskRB);
            vecB = vmulq(vecB, shiftC);
            /* merge channels */
            uint32x4_t vOut = vecA | vecR >> 8 | vecG >> 16 | vecB >> 24;
            /* store a vector of 4 bgra8888 pixels */
            vst1q_p(pwTarget, vOut, tailPred);
            phwSource += 6;
            pwTarget += 4;
            blkCnt -= 4;
          } while (blkCnt > 0);
#else
          uint32_t carry;
          __asm volatile(
              "   vldrw.32                q4, [%[maskA]]                      \n"
              "   vldrw.32                q5, [%[shiftC]]                     \n"
              "   vldrw.32                q6, [%[maskRB]]                     \n"
              "   vldrw.32                q7, [%[maskG]]                      \n"
              "   .p2align 2                                                  \n"
              "   wlstp.32                lr, %[loopCnt], 1f                  \n"
              "   2:                                                          \n"
              /* load a vector of 4 bgra5658 pixels */
              "   vldrw.32                q0, [%[pSource]], #12               \n"
              /* q0 => |****|AQPA|QPAQ|PAQP| */
              "   vshlc                   q0, %[pCarry], #8                   \n"
              /* q0 => |***A|QPAQ|PAQP|AQP0| */
              "   vand                    q2, q0, q4                          \n"
              /* q2 => |000A|00A0|0A00|A000| */
              "   vmul.i32                q1, q2, q5                          \n"
              /* q1 => |A000|A000|A000|A000|, use q1 as final output */
              "   vshlc                   q0, %[pCarry], #8                   \n"
              /* q0 => |**AQ|PAQP|AQPA|QP**| */
              "   vand                    q2, q0, q6                          \n"
              /* q2 => |000r|00r0|0r00|r000| */
              "   vmul.i32                q3, q2, q5                          \n"
              /* q3 => |r000|r000|r000|r000| */
              "   vsri.32                 q1, q3, #8                          \n"
              /* q1 => |Ar00|Ar00|Ar00|Ar00| */
              "   vsri.32                 q1, q3, #13                         \n"
              /* q1 => |AR*0|AR*0|AR*0|AR*0| */
              "   vshlc                   q0, %[pCarry], #5                   \n"
              /* Similar operation on G channel */
              "   vand                    q2, q0, q7                          \n"
              /* q2 => |000g|00g0|0g00|g000| */
              "   vmul.i32                q3, q2, q5                          \n"
              /* q3 => |g000|g000|g000|g000| */
              "   vsri.32                 q1, q3, #16                         \n"
              /* q1 => |ARg0|ARg0|ARg0|ARg0| */
              "   vsri.32                 q1, q3, #22                         \n"
              /* q1 => |ARG*|ARG*|ARG*|ARG*| */
              "   vshlc                   q0, %[pCarry], #6                   \n"
              /* Similar operation on B channel */
              "   vand                    q2, q0, q6                          \n"
              /* q2 => |000b|00b0|0b00|b000| */
              "   vmul.i32                q3, q2, q5                          \n"
              /* q3 => |b000|b000|b000|b000| */
              "   vsri.32                 q1, q3, #24                         \n"
              /* q1 => |ARGb|ARGb|ARGb|ARGb| */
              "   vsri.32                 q1, q3, #29                         \n"
              /* q1 => |ARGB|ARGB|ARGB|ARGB| */
              /* store a vector of 4 bgra8888 pixels */
              "   vstrw.32                q1, [%[pTarget]], #16               \n"
              "   letp                    lr, 2b                              \n"
              "   1:                                                          \n"

              : [pSource] "+r"(phwSource), [pTarget] "+r"(pwTarget)
              : [loopCnt] "r"(blkCnt), [maskA] "r"(_maskA), [shiftC] "r"(_shiftC),
              [maskRB] "r"(_maskRB), [maskG] "r"(_maskG), [pCarry] "r"(carry)
              : "q0", "q1", "q2", "q3", "q4", "q5", "q6", "q7", "lr", "memory");
#endif
          px_map += map_stride;
          px_buf += vgbuf_stride;
        }
#else
        for (uint32_t i = 0; i < map_h; i++) {
          for (uint32_t j = 0; j < map_w; j++) {
            lv_color32_t* c32 = (uint32_t*)px_buf + j;
            lv_color16_t* c16 = (const lv_color16_t*)px_map;
            c32->ch.red = (c16->ch.red * 263 + 7) >> 5;
            c32->ch.green = (c16->ch.green * 259 + 3) >> 6;
            c32->ch.blue = (c16->ch.blue * 263 + 7) >> 5;
            c32->ch.alpha = px_map[LV_IMG_PX_SIZE_ALPHA_BYTE - 1];
            // *((uint32_t*)px_buf + j) = c32.full;
            px_map += LV_IMG_PX_SIZE_ALPHA_BYTE;
          }
          // lv_memset_00(px_buf + map_w * 4, vgbuf_stride - map_w * 4);
          px_buf += vgbuf_stride;
        }
#endif
      } else
#endif
        for (int i = 0; i < map_h; i++) {
          lv_memcpy(px_buf, px_map, map_stride);
          //   lv_memset_00(px_buf + map_stride, vgbuf_stride - map_stride);
          px_map += map_stride;
          px_buf += vgbuf_stride;
        }
    } else {
      LV_LOG_ERROR("GPU blit failed: insufficient GPU memory");
      return LV_RES_INV;
    }
  }
  if (!transformed) {
    map_w = draw_area_w;
    map_h = draw_area_h;
    src_vgbuf.memory = (uint8_t*)src_vgbuf.memory + (draw_area.y1 - map_tf.y1) * src_vgbuf.stride + draw_area.x1 - map_tf.x1;
  }
  rect[0] = 0;
  rect[1] = 0;
  rect[2] = map_w;
  rect[3] = map_h;
  vg_lite_matrix_t matrix;
  vg_lite_identity(&matrix);
  vg_lite_translate(MAX(0, map_area->x1 - disp_area->x1), MAX(0, map_area->y1 - disp_area->y1), &matrix);
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
  if (opa >= LV_OPA_MAX && recolor_opa == LV_OPA_TRANSP) {
    color.full = 0x0;
    src_vgbuf.image_mode = VG_LITE_NORMAL_IMAGE_MODE;
  } else {
    color.full = (opa << 24) | (opa << 16) | (opa << 8) | opa;
    if (recolor_opa != LV_OPA_TRANSP) {
      lv_color32_t recolor32 = { .full = lv_color_to32(recolor) };
      lv_opa_t opa_res = opa * (255 - recolor_opa) + LV_COLOR_MIX_ROUND_OFS;
      LV_COLOR_SET_R32(color, LV_UDIV255((uint16_t)LV_COLOR_GET_R32(recolor32) * recolor_opa + opa_res));
      LV_COLOR_SET_G32(color, LV_UDIV255((uint16_t)LV_COLOR_GET_G32(recolor32) * recolor_opa + opa_res));
      LV_COLOR_SET_B32(color, LV_UDIV255((uint16_t)LV_COLOR_GET_B32(recolor32) * recolor_opa + opa_res));
    }
    src_vgbuf.image_mode = VG_LITE_MULTIPLY_IMAGE_MODE;
  }

  cpu_cache_flush(src_vgbuf.memory, src_vgbuf.height * src_vgbuf.stride);

  LV_LOG_WARN("rect:(%d %d %d %d) dst:%p src:%p\n", rect[0], rect[1], rect[2], rect[3], dst_vgbuf.memory, src_vgbuf.memory);
  vg_lite_error_t error = VG_LITE_SUCCESS;
  vg_lite_filter_t filter = transformed ? VG_LITE_FILTER_BI_LINEAR : VG_LITE_FILTER_POINT;
  CHECK_ERROR(vg_lite_blit_rect(&dst_vgbuf, &src_vgbuf, rect, &matrix, blend, color.full, filter));

  CHECK_ERROR(vg_lite_finish());
  // vg_lite_free(&src_vgbuf);
  if (mem != map_buf) {
    free(mem);
  }
  if (error != VG_LITE_SUCCESS) {
    LV_LOG_WARN("GPU blit failed. Fallback to SW.\n");
    /*Fall back to SW render in case of error*/
    return LV_RES_INV;
  }
  cpu_gpu_data_cache_invalid(dst_vgbuf.memory, dst_vgbuf.height * dst_vgbuf.stride);
  return LV_RES_OK;
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
