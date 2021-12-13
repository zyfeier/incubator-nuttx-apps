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
#include "../lvgl/src/draw/sw/lv_draw_sw.h"
#include "../lvgl/src/misc/lv_color.h"
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

#define GPU_SIZE_LIMIT 240
#define GPU_SPLIT_SIZE (480 * 100)
/****************************************************************************
 * Macros
 ****************************************************************************/
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
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

extern void cpu_cache_flush(uint32_t start, uint32_t length);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static lv_gpu_mode_t power_mode;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

LV_ATTRIBUTE_FAST_MEM static inline void bgra5658_to_8888(const uint8_t* src, uint32_t* dst)
{
  lv_color32_t* c32 = dst;
  lv_color16_t* c16 = (const lv_color16_t*)src;
  c32->ch.red = c16->ch.red << 3 | c16->ch.red >> 2;
  c32->ch.green = c16->ch.green << 2 | c16->ch.green >> 4;
  c32->ch.blue = c16->ch.blue << 3 | c16->ch.blue >> 2;
  c32->ch.alpha = src[LV_IMG_PX_SIZE_ALPHA_BYTE - 1];
}

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
    LV_LOG_ERROR("Buffer width (%d) not aligned to 16px.", width);
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

static void lv_draw_hw_img(const lv_area_t* map_area, const lv_area_t* clip_area,
    const uint8_t* map_p, const lv_draw_img_dsc_t* draw_dsc, bool chroma_key, bool alpha_byte)
{
  if (lv_draw_map_gpu(map_area, clip_area, map_p, draw_dsc, chroma_key, alpha_byte) != LV_RES_OK) {
    lv_draw_sw_img(map_area, clip_area, map_p, draw_dsc, chroma_key, alpha_byte);
  }
}

static void lv_gpu_backend_init(void)
{
  static lv_draw_backend_t backend;
  lv_draw_backend_init(&backend);

  backend.draw_arc = lv_draw_sw_arc;
  backend.draw_rect = lv_draw_sw_rect;
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
    const lv_color_t* map_buf, const lv_draw_img_dsc_t* draw_dsc, bool chroma_key, bool alpha_byte)
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
  lv_gpu_backend_init();
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
