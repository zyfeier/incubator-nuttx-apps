/****************************************************************************
 * apps/graphics/lvgl/lv_porting/gpu/lv_gpu_draw_blend.c
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

#include "arm_mve.h"
#include "lv_gpu_draw_utils.h"
#include "lv_porting/lv_gpu_interface.h"

/****************************************************************************
 * Preprocessor Definitions
 ****************************************************************************/

#define TMP_MASK_MAX_LEN 480

/****************************************************************************
 * Macros
 ****************************************************************************/

/****************************************************************************
 * Private Data
 ****************************************************************************/

static lv_opa_t tmp_mask[TMP_MASK_MAX_LEN];
static const uint8_t ff = 0xFF;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

LV_ATTRIBUTE_FAST_MEM static void fill_normal(lv_color_t* dst,
    const lv_area_t* draw_area, lv_coord_t dst_stride, lv_color_t color,
    lv_opa_t opa, const lv_opa_t* mask, lv_coord_t mask_stride)
{
  lv_coord_t w = lv_area_get_width(draw_area);
  lv_coord_t h = lv_area_get_height(draw_area);
  if (mask) {
    for (lv_coord_t i = 0; i < h; i++) {
      uint32_t* pwTarget = (uint32_t*)dst;
      uint8_t* pMask = (uint8_t*)mask;
      if (!IS_ALIGNED(mask, 4)) {
        lv_memcpy(tmp_mask, mask, mask_stride);
        pMask = tmp_mask;
      }
      register unsigned blkCnt __asm("lr") = w;
      uint8_t R = color.ch.red;
      uint8_t G = color.ch.green;
      uint8_t B = color.ch.blue;
      __asm volatile(
          "   .p2align 2                                                  \n"
          "   wlstp.8                 lr, %[loopCnt], 1f                  \n"
          "   2:                                                          \n"
          "   vld40.8                 {q0, q1, q2, q3}, [%[pDst]]         \n"
          "   vld41.8                 {q0, q1, q2, q3}, [%[pDst]]         \n"
          "   vld42.8                 {q0, q1, q2, q3}, [%[pDst]]         \n"
          "   vld43.8                 {q0, q1, q2, q3}, [%[pDst]]         \n"
          "   vldrb.8                 q4, [%[pMask]], #16                 \n"
          "   vdup.8                  q5, %[opa]                          \n"
          "   vrmulh.u8               q4, q4, q5                          \n"
          "   vdup.8                  q3, %[ff]                           \n"
          "   vsub.i8                 q5, q3, q4                          \n"
          "   vrmulh.u8               q0, q0, q5                          \n"
          "   vrmulh.u8               q1, q1, q5                          \n"
          "   vrmulh.u8               q2, q2, q5                          \n"
          "   vdup.8                  q5, %[B]                            \n"
          "   vrmulh.u8               q5, q4, q5                          \n"
          "   vadd.i8                 q0, q0, q5                          \n"
          "   vdup.8                  q5, %[G]                            \n"
          "   vrmulh.u8               q5, q4, q5                          \n"
          "   vadd.i8                 q1, q1, q5                          \n"
          "   vdup.8                  q5, %[R]                            \n"
          "   vrmulh.u8               q5, q4, q5                          \n"
          "   vadd.i8                 q2, q2, q5                          \n"
          "   vst40.8                 {q0, q1, q2, q3}, [%[pDst]]         \n"
          "   vst41.8                 {q0, q1, q2, q3}, [%[pDst]]         \n"
          "   vst42.8                 {q0, q1, q2, q3}, [%[pDst]]         \n"
          "   vst43.8                 {q0, q1, q2, q3}, [%[pDst]]!        \n"
          "   letp                    lr, 2b                              \n"
          "   1:                                                          \n"
          : [pDst] "+r"(pwTarget), [pMask] "+r"(pMask), [loopCnt] "+r"(blkCnt)
          : [R] "r"(R), [G] "r"(G), [B] "r"(B), [ff] "r"(ff), [opa] "r"(opa)
          : "q0", "q1", "q2", "q3", "q4", "q5", "memory");
      dst += dst_stride;
      mask += mask_stride;
    }
  } else if (opa != LV_OPA_COVER) {
    for (lv_coord_t i = 0; i < h; i++) {
      uint32_t* pwTarget = (uint32_t*)dst;
      lv_opa_t mix = 255 - opa;
      register unsigned blkCnt __asm("lr") = w;
      __asm volatile(
          "   .p2align 2                                                  \n"
          "   vdup.32                 q0, %[color]                        \n"
          "   vdup.8                  q1, %[opa]                          \n"
          "   vrmulh.u8               q0, q0, q1                          \n"
          "   vdup.8                  q1, %[mix]                          \n"
          "   wlstp.32                lr, %[loopCnt], 1f                  \n"
          "   2:                                                          \n"
          "   vldrw.32                q2, [%[pDst]]                       \n"
          "   vrmulh.u8               q2, q2, q1                          \n"
          "   vadd.i8                 q2, q0, q2                          \n"
          "   vstrw.32                q2, [%[pDst]], #16                  \n"
          "   letp                    lr, 2b                              \n"
          "   1:                                                          \n"
          : [pDst] "+r"(pwTarget), [loopCnt] "+r"(blkCnt)
          : [color] "r"(color), [opa] "r"(opa), [mix] "r"(mix)
          : "q0", "q1", "q2", "memory");
      dst += dst_stride;
    }
  } else {
    for (lv_coord_t i = 0; i < h; i++) {
      uint32_t* pwTarget = (uint32_t*)dst;
      register unsigned blkCnt __asm("lr") = w;
      __asm volatile(
          "   .p2align 2                                                  \n"
          "   vdup.32                 q0, %[color]                        \n"
          "   wlstp.32                lr, %[loopCnt], 1f                  \n"
          "   2:                                                          \n"
          "   vstrw.32                q0, [%[pDst]], #16                  \n"
          "   letp                    lr, 2b                              \n"
          "   1:                                                          \n"
          : [pDst] "+r"(pwTarget), [loopCnt] "+r"(blkCnt)
          : [color] "r"(color)
          : "q0", "memory");
      dst += dst_stride;
    }
  }
}

LV_ATTRIBUTE_FAST_MEM static void map_normal(lv_color_t* dst,
    const lv_area_t* draw_area, lv_coord_t dst_stride, const lv_color_t* src,
    lv_coord_t src_stride, lv_opa_t opa, const lv_opa_t* mask,
    lv_coord_t mask_stride)
{
  lv_coord_t w = lv_area_get_width(draw_area);
  lv_coord_t h = lv_area_get_height(draw_area);
  if (mask) {
    for (lv_coord_t i = 0; i < h; i++) {
      uint32_t* phwSource = (uint32_t*)src;
      uint32_t* pwTarget = (uint32_t*)dst;
      uint8_t* pMask = (uint8_t*)mask;
      if (!IS_ALIGNED(mask, 4)) {
        lv_memcpy(tmp_mask, mask, mask_stride);
        pMask = tmp_mask;
      }
      register unsigned blkCnt __asm("lr") = w;
      __asm volatile(
          "   .p2align 2                                                  \n"
          "   wlstp.8                 lr, %[loopCnt], 1f                  \n"
          "   2:                                                          \n"
          "   vld40.8                 {q0, q1, q2, q3}, [%[pSrc]]         \n"
          "   vld41.8                 {q0, q1, q2, q3}, [%[pSrc]]         \n"
          "   vld42.8                 {q0, q1, q2, q3}, [%[pSrc]]         \n"
          "   vld43.8                 {q0, q1, q2, q3}, [%[pSrc]]!        \n"
          "   vld40.8                 {q4, q5, q6, q7}, [%[pDst]]         \n"
          "   vld41.8                 {q4, q5, q6, q7}, [%[pDst]]         \n"
          "   vld42.8                 {q4, q5, q6, q7}, [%[pDst]]         \n"
          "   vld43.8                 {q4, q5, q6, q7}, [%[pDst]]         \n"
          "   vldrb.8                 q3, [%[pMask]], #16                 \n"
          "   vdup.8                  q7, %[opa]                          \n"
          "   vrmulh.u8               q3, q3, q7                          \n"
          "   vrmulh.u8               q0, q0, q3                          \n"
          "   vrmulh.u8               q1, q1, q3                          \n"
          "   vrmulh.u8               q2, q2, q3                          \n"
          "   vdup.8                  q7, %[ff]                           \n"
          "   vsub.i8                 q3, q7, q3                          \n"
          "   vrmulh.u8               q4, q4, q3                          \n"
          "   vrmulh.u8               q5, q5, q3                          \n"
          "   vrmulh.u8               q6, q6, q3                          \n"
          "   vadd.i8                 q4, q0, q4                          \n"
          "   vadd.i8                 q5, q1, q5                          \n"
          "   vadd.i8                 q6, q2, q6                          \n"
          "   vst40.8                 {q4, q5, q6, q7}, [%[pDst]]         \n"
          "   vst41.8                 {q4, q5, q6, q7}, [%[pDst]]         \n"
          "   vst42.8                 {q4, q5, q6, q7}, [%[pDst]]         \n"
          "   vst43.8                 {q4, q5, q6, q7}, [%[pDst]]!        \n"
          "   letp                    lr, 2b                              \n"
          "   1:                                                          \n"
          : [pSrc] "+r"(phwSource), [pDst] "+r"(pwTarget), [pMask] "+r"(pMask),
          [loopCnt] "+r"(blkCnt)
          : [ff] "r"(ff), [opa] "r"(opa)
          : "q0", "q1", "q2", "q3", "q4", "q5", "q6", "q7", "memory");
      src += src_stride;
      dst += dst_stride;
      mask += mask_stride;
    }
  } else if (opa != LV_OPA_COVER) {
    for (lv_coord_t i = 0; i < h; i++) {
      uint32_t* phwSource = (uint32_t*)src;
      uint32_t* pwTarget = (uint32_t*)dst;
      lv_opa_t mix = 255 - opa;
      register unsigned blkCnt __asm("lr") = w;
      __asm volatile(
          "   .p2align 2                                                  \n"
          "   vdup.8                  q0, %[opa]                          \n"
          "   vdup.8                  q1, %[mix]                          \n"
          "   wlstp.32                lr, %[loopCnt], 1f                  \n"
          "   2:                                                          \n"
          "   vldrw.32                q2, [%[pSrc]], #16                  \n"
          "   vrmulh.u8               q2, q2, q0                          \n"
          "   vldrw.32                q3, [%[pDst]]                       \n"
          "   vrmulh.u8               q3, q3, q1                          \n"
          "   vadd.i8                 q3, q2, q3                          \n"
          "   vstrw.32                q3, [%[pDst]], #16                  \n"
          "   letp                    lr, 2b                              \n"
          "   1:                                                          \n"
          : [pSrc] "+r"(phwSource), [pDst] "+r"(pwTarget),
          [loopCnt] "+r"(blkCnt)
          : [opa] "r"(opa), [mix] "r"(mix)
          : "q0", "q1", "q2", "q3", "memory");
      src += src_stride;
      dst += dst_stride;
    }
  } else {
    for (lv_coord_t i = 0; i < h; i++) {
      uint32_t* phwSource = (uint32_t*)src;
      uint32_t* pwTarget = (uint32_t*)dst;
      register unsigned blkCnt __asm("lr") = w;
      __asm volatile(
          "   .p2align 2                                                  \n"
          "   wlstp.32                lr, %[loopCnt], 1f                  \n"
          "   2:                                                          \n"
          "   vldrw.32                q0, [%[pSrc]], #16                  \n"
          "   vstrw.32                q0, [%[pDst]], #16                  \n"
          "   letp                    lr, 2b                              \n"
          "   1:                                                          \n"
          : [pSrc] "+r"(phwSource), [pDst] "+r"(pwTarget),
          [loopCnt] "+r"(blkCnt)
          :
          : "q0", "memory");
      src += src_stride;
      dst += dst_stride;
    }
  }
}

/****************************************************************************
 * Global Functions
 ****************************************************************************/

/****************************************************************************
 * Name: lv_gpu_draw_blend
 *
 * Description:
 *   Blend an area to a display buffer.
 *
 * Input Parameters:
 * @param draw_ctx draw context (refer to LVGL 8.2 changelog)
 * @param dsc blend descriptor
 *
 * Returned Value:
 * @return LV_RES_OK on success, LV_RES_INV on failure.
 *
 ****************************************************************************/
LV_ATTRIBUTE_FAST_MEM lv_res_t lv_gpu_draw_blend(lv_draw_ctx_t* draw_ctx,
    const lv_draw_sw_blend_dsc_t* dsc)
{
  if (dsc->blend_mode != LV_BLEND_MODE_NORMAL) {
    GPU_WARN("only normal blend mode acceleration supported at the moment");
    return LV_RES_INV;
  }
  lv_disp_t* disp = _lv_refr_get_disp_refreshing();
  if (disp->driver->set_px_cb) {
    GPU_WARN("drawing with set_px_cb can't be accelerated");
    return LV_RES_INV;
  }
  if (disp->driver->screen_transp) {
    GPU_WARN("transparent screen blend acceleration unsupported at the moment");
    return LV_RES_INV;
  }
  if (!IS_ALIGNED(dsc->src_buf, 4) || !IS_ALIGNED(draw_ctx->buf, 4)) {
    GPU_WARN("Unaligned src/dst buffer");
    return LV_RES_INV;
  }
  if (dsc->mask_buf && dsc->mask_res == LV_DRAW_MASK_RES_TRANSP) {
    return LV_RES_OK;
  }
  lv_area_t draw_area;
  const lv_area_t* disp_area = draw_ctx->buf_area;
  if (!_lv_area_intersect(&draw_area, dsc->blend_area, draw_ctx->clip_area)) {
    return LV_RES_OK;
  }

  lv_coord_t src_stride = 0;
  lv_coord_t dst_stride = lv_area_get_width(disp_area);
  lv_coord_t mask_stride = 0;

  lv_color_t* dst = draw_ctx->buf;
  dst += dst_stride * (draw_area.y1 - disp_area->y1) + draw_area.x1 - disp_area->x1;
  const lv_color_t* src = dsc->src_buf;
  if (src) {
    src_stride = lv_area_get_width(dsc->blend_area);
    src += src_stride * (draw_area.y1 - dsc->blend_area->y1) + draw_area.x1 - dsc->blend_area->x1;
  }
  const lv_opa_t* mask = dsc->mask_res == LV_DRAW_MASK_RES_FULL_COVER ? NULL : dsc->mask_buf;
  if (mask) {
    mask_stride = lv_area_get_width(dsc->mask_area);
    mask += mask_stride * (draw_area.y1 - dsc->mask_area->y1) + draw_area.x1 - dsc->mask_area->x1;
  }
  if (src) {
    map_normal(dst, &draw_area, dst_stride, src, src_stride, dsc->opa, mask,
        mask_stride);
  } else {
    fill_normal(dst, &draw_area, dst_stride, dsc->color, dsc->opa, mask,
        mask_stride);
  }

  return LV_RES_OK;
}
