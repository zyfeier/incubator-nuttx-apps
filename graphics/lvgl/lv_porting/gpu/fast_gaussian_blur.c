/****************************************************************************
 * apps/graphics/lvgl/lv_porting/gpu/lv_gpu_decoder.c
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

// This file was modified from C++ version by B.Fraboni. Original license
// is attached below.

// Copyright (C) 2017-2022 Basile Fraboni
// Copyright (C) 2014 Ivan Kutskir (for the original fast blur implmentation)
// All Rights Reserved
// You may use, distribute and modify this code under the
// terms of the MIT license. For further details please refer
// to : https://mit-license.org/

#include <lvgl/lvgl.h>
#include <nuttx/config.h>

enum ARGB {
  B,
  G,
  R,
  A
};

#define MAX_STRIDE 480 + 2

static void horizontal_blur(const lv_color_t* src, lv_color_t* dst,
    lv_coord_t src_stride, lv_coord_t dst_stride, lv_coord_t w, lv_coord_t h,
    lv_coord_t r, lv_color_t* tmp)
{
  for (lv_coord_t i = 0; i < h; i++) {
    lv_color_t fv, lv;
    uint32_t val[4], len = r + r + 1;
    uint32_t src_res_px = src_stride;
    uint32_t dst_res_px = dst_stride;
    uint32_t res_w = w;
    val[A] = src[0].ch.alpha * (r + 1);
    val[R] = src[0].ch.red * (r + 1);
    val[G] = src[0].ch.green * (r + 1);
    val[B] = src[0].ch.blue * (r + 1);
    tmp[0].full = tmp[1].full = src[0].full;
#if defined(CONFIG_ARM_HAVE_MVE) && LV_COLOR_DEPTH == 32
    if (r == 1 && w >= 16) {
      uint32_t* phwSource = (uint32_t*)src;
      uint32_t* pwTarget = (uint32_t*)dst;
      int32_t blkCnt = w >> 4;
      __asm volatile(
          "   .p2align 2                                                  \n"
          "   vldrw.32                q6, [%[pSrc]], #4                   \n"
          "   mov                     r4, #0x55                           \n"
          "   vdup.8                  q5, r4                              \n"
          "   vrmulh.u8               q6, q6, q5                          \n"
          "   vmov.u8                 r0, q6[0]                           \n"
          "   vmov.u8                 r1, q6[1]                           \n"
          "   vmov.u8                 r2, q6[2]                           \n"
          "   vmov.u8                 r3, q6[3]                           \n"
          "   orr                     r0, r0, r0, lsl #8                  \n"
          "   orr                     r1, r1, r1, lsl #8                  \n"
          "   orr                     r2, r2, r2, lsl #8                  \n"
          "   orr                     r3, r3, r3, lsl #8                  \n"
          "   wls                     lr, %[loopCnt], 1f                  \n"
          "   2:                                                          \n"
          "   vld40.8                 {q0, q1, q2, q3}, [%[pSrc]]         \n"
          "   vld41.8                 {q0, q1, q2, q3}, [%[pSrc]]         \n"
          "   vld42.8                 {q0, q1, q2, q3}, [%[pSrc]]         \n"
          "   vld43.8                 {q0, q1, q2, q3}, [%[pSrc]]!        \n"
          "   mov                     r4, #0x55                           \n"
          "   vdup.8                  q7, r4                              \n"
          "   vrmulh.u8               q0, q0, q7                          \n"
          "   vrmulh.u8               q1, q1, q7                          \n"
          "   vrmulh.u8               q2, q2, q7                          \n"
          "   vrmulh.u8               q3, q3, q7                          \n"
          "   vmov                    q4, q0                              \n"
          "   vmov.u16                r4, q0[7]                           \n"
          "   mov                     r3, r0, lsr #8                      \n"
          "   vshlc                   q0, r3, #8                          \n"
          "   vadd.u8                 q4, q4, q0                          \n"
          "   vshlc                   q0, r0, #8                          \n"
          "   vadd.u8                 q4, q4, q0                          \n"
          "   mov                     r0, r4                              \n"
          "   vmov                    q5, q1                              \n"
          "   vmov.u16                r4, q1[7]                           \n"
          "   mov                     r3, r1, lsr #8                      \n"
          "   vshlc                   q1, r3, #8                          \n"
          "   vadd.u8                 q5, q5, q1                          \n"
          "   vshlc                   q1, r1, #8                          \n"
          "   vadd.u8                 q5, q5, q1                          \n"
          "   mov                     r1, r4                              \n"
          "   vmov                    q6, q2                              \n"
          "   vmov.u16                r4, q2[7]                           \n"
          "   mov                     r3, r2, lsr #8                      \n"
          "   vshlc                   q2, r3, #8                          \n"
          "   vadd.u8                 q6, q6, q2                          \n"
          "   vshlc                   q2, r2, #8                          \n"
          "   vadd.u8                 q6, q6, q2                          \n"
          "   mov                     r2, r4                              \n"
          "   vmov                    q7, q3                              \n"
          "   vmov.u16                r4, q3[7]                           \n"
          "   vmov.8                  q3[15], r3                          \n"
          "   mov                     r3, r3, lsr #8                      \n"
          "   vshlc                   q3, r3, #8                          \n"
          "   vadd.u8                 q7, q7, q3                          \n"
          "   vshlc                   q3, r3, #8                          \n"
          "   vadd.u8                 q7, q7, q3                          \n"
          "   mov                     r3, r4                              \n"
          "   vst40.8                 {q4, q5, q6, q7}, [%[pDst]]         \n"
          "   vst41.8                 {q4, q5, q6, q7}, [%[pDst]]         \n"
          "   vst42.8                 {q4, q5, q6, q7}, [%[pDst]]         \n"
          "   vst43.8                 {q4, q5, q6, q7}, [%[pDst]]!        \n"
          "   le                      lr, 2b                              \n"
          "   1:                                                          \n"
          "   and                     r0, r0, #0xFF                       \n"
          "   and                     r1, r1, #0xFF                       \n"
          "   and                     r2, r2, #0xFF                       \n"
          "   and                     r3, r3, #0xFF                       \n"
          "   str                     r0, [%[val]]                        \n"
          "   str                     r1, [%[val], #4]                    \n"
          "   str                     r2, [%[val], #8]                    \n"
          "   str                     r3, [%[val], #12]                   \n"
          : [pSrc] "+r"(phwSource), [pDst] "+r"(pwTarget)
          : [loopCnt] "r"(blkCnt), [val] "r"(val)
          : "q0", "q1", "q2", "q3", "q4", "q5", "q6", "q7", "r0", "r1", "r2",
          "r3", "r4", "lr", "memory");
      blkCnt <<= 4;
      src += blkCnt;
      dst += blkCnt;
      src_res_px -= blkCnt;
      dst_res_px -= blkCnt;
      res_w -= blkCnt;
      if (res_w) {
        tmp[0].full = 0;
        tmp[1].ch.blue = val[B] *= 3;
        tmp[1].ch.green = val[G] *= 3;
        tmp[1].ch.red = val[R] *= 3;
        tmp[1].ch.alpha = val[A] *= 3;
      } else {
        lv_color_t* last = dst - 1;
        last->ch.blue += val[B] - src->ch.blue / 3;
        last->ch.green += val[G] - src->ch.green / 3;
        last->ch.red += val[R] - src->ch.red / 3;
        last->ch.alpha += val[A] - src->ch.alpha / 3;
        src += src_res_px;
        dst += dst_res_px;
        continue;
      }
    }
#endif
    for (lv_coord_t j = 0; j < r; j++) {
      val[B] += src[j].ch.blue;
      val[G] += src[j].ch.green;
      val[R] += src[j].ch.red;
      val[A] += src[j].ch.alpha;
    }
    for (lv_coord_t j = 0; j < res_w; j++) {
      fv.full = j > r - 2 ? tmp[j - r + 1].full : tmp[0].full;
      lv.full = j < res_w - r ? src[j + r].full : src[res_w - 1].full;
      val[B] += lv.ch.blue - fv.ch.blue;
      val[G] += lv.ch.green - fv.ch.green;
      val[R] += lv.ch.red - fv.ch.red;
      val[A] += lv.ch.alpha - fv.ch.alpha;
      tmp[j + 2] = src[j];
      dst[j].ch.blue = val[B] / len;
      dst[j].ch.green = val[G] / len;
      dst[j].ch.red = val[R] / len;
      dst[j].ch.alpha = val[A] / len;
    }
    src += src_res_px;
    dst += dst_res_px;
  }
}

static void vertical_blur(const lv_color_t* src, lv_color_t* dst,
    lv_coord_t src_stride, lv_coord_t dst_stride, lv_coord_t w, lv_coord_t h,
    lv_coord_t r, lv_color_t* tmp)
{

#if defined(CONFIG_ARM_HAVE_MVE) && LV_COLOR_DEPTH == 32
  if (r == 1 && w >= 16) {
    lv_coord_t w16 = w >> 4;
    for (lv_coord_t i = 0; i < w16; i++) {
      uint32_t* phwSource = (uint32_t*)src;
      uint32_t* pwTarget = (uint32_t*)dst;
      int32_t blkCnt = h - 1;
      __asm volatile(
          "   .p2align 2                                                  \n"
          "   vld40.8                 {q0, q1, q2, q3}, [%[pSrc]]         \n"
          "   vld41.8                 {q0, q1, q2, q3}, [%[pSrc]]         \n"
          "   vld42.8                 {q0, q1, q2, q3}, [%[pSrc]]         \n"
          "   vld43.8                 {q0, q1, q2, q3}, [%[pSrc]]         \n"
          "   mov                     r0, #0x55                           \n"
          "   vdup.8                  q6, r0                              \n"
          "   vrmulh.u8               q0, q0, q6                          \n"
          "   vrmulh.u8               q1, q1, q6                          \n"
          "   vrmulh.u8               q2, q2, q6                          \n"
          "   vrmulh.u8               q3, q3, q6                          \n"
          "   mov                     r0, %[tmp]                          \n"
          "   vstrw.32                q0, [r0], #16                       \n"
          "   vstrw.32                q1, [r0], #16                       \n"
          "   vstrw.32                q2, [r0], #16                       \n"
          "   vstrw.32                q3, [r0], #16                       \n"
          "   vstrw.32                q0, [r0], #16                       \n"
          "   vstrw.32                q1, [r0], #16                       \n"
          "   vstrw.32                q2, [r0], #16                       \n"
          "   vstrw.32                q3, [r0], #16                       \n"
          "   mov                     r1, %[tmp]                          \n"
          "   mov                     r2, r0                              \n"
          "   add                     %[pSrc], %[pSrc], %[sS], lsl #2     \n"
          "   wls                     lr, %[loopCnt], 1f                  \n"
          "   2:                                                          \n"
          "   cmp                     r0, r2                              \n"
          "   it                      eq                                  \n"
          "   moveq                   r0, %[tmp]                          \n"
          "   mov                     r1, %[tmp]                          \n"
          "   vld40.8                 {q0, q1, q2, q3}, [%[pSrc]]         \n"
          "   vld41.8                 {q0, q1, q2, q3}, [%[pSrc]]         \n"
          "   vld42.8                 {q0, q1, q2, q3}, [%[pSrc]]         \n"
          "   vld43.8                 {q0, q1, q2, q3}, [%[pSrc]]         \n"
          "   vrmulh.u8               q4, q0, q6                          \n"
          "   vldrw.32                q5, [r1, #64]                       \n"
          "   vadd.u8                 q0, q4, q5                          \n"
          "   vldrw.32                q5, [r1], #16                       \n"
          "   vadd.u8                 q0, q0, q5                          \n"
          "   vstrw.32                q4, [r0], #16                       \n"
          "   vrmulh.u8               q4, q1, q6                          \n"
          "   vldrw.32                q5, [r1, #64]                       \n"
          "   vadd.u8                 q1, q4, q5                          \n"
          "   vldrw.32                q5, [r1], #16                       \n"
          "   vadd.u8                 q1, q1, q5                          \n"
          "   vstrw.32                q4, [r0], #16                       \n"
          "   vrmulh.u8               q4, q2, q6                          \n"
          "   vldrw.32                q5, [r1, #64]                       \n"
          "   vadd.u8                 q2, q4, q5                          \n"
          "   vldrw.32                q5, [r1], #16                       \n"
          "   vadd.u8                 q2, q2, q5                          \n"
          "   vstrw.32                q4, [r0], #16                       \n"
          "   vrmulh.u8               q4, q3, q6                          \n"
          "   vldrw.32                q5, [r1, #64]                       \n"
          "   vadd.u8                 q3, q4, q5                          \n"
          "   vldrw.32                q5, [r1], #16                       \n"
          "   vadd.u8                 q3, q3, q5                          \n"
          "   vstrw.32                q4, [r0], #16                       \n"
          "   vst40.8                 {q0, q1, q2, q3}, [%[pDst]]         \n"
          "   vst41.8                 {q0, q1, q2, q3}, [%[pDst]]         \n"
          "   vst42.8                 {q0, q1, q2, q3}, [%[pDst]]         \n"
          "   vst43.8                 {q0, q1, q2, q3}, [%[pDst]]         \n"
          "   add                     %[pSrc], %[pSrc], %[sS], lsl #2     \n"
          "   add                     %[pDst], %[pDst], %[dS], lsl #2     \n"
          "   le                      lr, 2b                              \n"
          "   1:                                                          \n"
          "   sub                     r0, r0, #64                         \n"
          "   mov                     r1, %[tmp]                          \n"
          "   vldrw.32                q4, [r0], #16                       \n"
          "   vldrw.32                q0, [r1, #64]                       \n"
          "   vadd.u8                 q0, q0, q4                          \n"
          "   vldrw.32                q4, [r1], #16                       \n"
          "   vadd.u8                 q0, q0, q4                          \n"
          "   vldrw.32                q4, [r0], #16                       \n"
          "   vldrw.32                q1, [r1, #64]                       \n"
          "   vadd.u8                 q1, q1, q4                          \n"
          "   vldrw.32                q4, [r1], #16                       \n"
          "   vadd.u8                 q1, q1, q4                          \n"
          "   vldrw.32                q4, [r0], #16                       \n"
          "   vldrw.32                q2, [r1, #64]                       \n"
          "   vadd.u8                 q2, q2, q4                          \n"
          "   vldrw.32                q4, [r1], #16                       \n"
          "   vadd.u8                 q2, q2, q4                          \n"
          "   vldrw.32                q4, [r0], #16                       \n"
          "   vldrw.32                q3, [r1, #64]                       \n"
          "   vadd.u8                 q3, q3, q4                          \n"
          "   vldrw.32                q4, [r1], #16                       \n"
          "   vadd.u8                 q3, q3, q4                          \n"
          "   vst40.8                 {q0, q1, q2, q3}, [%[pDst]]         \n"
          "   vst41.8                 {q0, q1, q2, q3}, [%[pDst]]         \n"
          "   vst42.8                 {q0, q1, q2, q3}, [%[pDst]]         \n"
          "   vst43.8                 {q0, q1, q2, q3}, [%[pDst]]         \n"
          : [pSrc] "+r"(phwSource), [pDst] "+r"(pwTarget)
          : [loopCnt] "r"(blkCnt), [sS] "r"(src_stride), [dS] "r"(dst_stride),
          [tmp] "r"(tmp)
          : "q0", "q1", "q2", "q3", "q4", "q5", "q6", "r0", "r1", "r2", "lr",
          "memory");
      src += 16;
      dst += 16;
    }
    w -= w16 << 4;
  }
#endif
  for (lv_coord_t i = 0; i < w; i++) {
    lv_color_t fv, lv;
    uint32_t val[4] = { 0, 0, 0, 0 }, len = r + r + 1;
    uint32_t sj = i;
    val[B] = (r + 1) * src[sj].ch.blue;
    val[G] = (r + 1) * src[sj].ch.green;
    val[R] = (r + 1) * src[sj].ch.red;
    val[A] = (r + 1) * src[sj].ch.alpha;
    for (lv_coord_t j = 0; j < r; j++) {
      val[B] += src[sj].ch.blue;
      val[G] += src[sj].ch.green;
      val[R] += src[sj].ch.red;
      val[A] += src[sj].ch.alpha;
      sj += src_stride;
    }
    sj = i;
    uint32_t tj = i;
    tmp[0] = src[i];
    for (lv_coord_t j = 0; j < h; j++) {
      fv.full = j > r ? tmp[j - r - 1].full : tmp[0].full;
      lv.full = j < h - r ? src[sj + r * src_stride].full
                          : src[i + (h - 1) * src_stride].full;
      val[B] += lv.ch.blue - fv.ch.blue;
      val[G] += lv.ch.green - fv.ch.green;
      val[R] += lv.ch.red - fv.ch.red;
      val[A] += lv.ch.alpha - fv.ch.alpha;
      tmp[j] = src[sj];
      dst[tj].ch.blue = val[B] / len;
      dst[tj].ch.green = val[G] / len;
      dst[tj].ch.red = val[R] / len;
      dst[tj].ch.alpha = val[A] / len;
      sj += src_stride;
      tj += dst_stride;
    }
  }
}

static void box_blur_3(const lv_color_t* src, lv_color_t* dst,
    lv_coord_t src_stride, lv_coord_t dst_stride,
    lv_coord_t w, lv_coord_t h, lv_coord_t r)
{
  lv_color_t tmp[MAX_STRIDE];
  horizontal_blur(src, dst, src_stride, dst_stride, w, h, r, tmp);
  horizontal_blur(dst, dst, dst_stride, dst_stride, w, h, r, tmp);
  horizontal_blur(dst, dst, dst_stride, dst_stride, w, h, r, tmp);
  vertical_blur(dst, dst, dst_stride, dst_stride, w, h, r, tmp);
  vertical_blur(dst, dst, dst_stride, dst_stride, w, h, r, tmp);
  vertical_blur(dst, dst, dst_stride, dst_stride, w, h, r, tmp);
}

/****************************************************************************
 * Name: fast_gaussian_blur
 *
 * Description:
 *   Do gaussian blur in an area.
 *
 * Input Parameters:
 * @param src source image
 * @param dst target buffer for the result
 * @param src_stride source image width
 * @param dst_stride target image width
 * @param blur_area blur area on src, target area starts from dst @ (0,0)
 * @param r filter width, r=1 is accelerated by MVE
 *
 ****************************************************************************/
void fast_gaussian_blur(lv_color_t* src, lv_color_t* dst,
    lv_coord_t src_stride, lv_coord_t dst_stride,
    lv_area_t* blur_area, int r)
{
  lv_coord_t blur_w = lv_area_get_width(blur_area);
  lv_coord_t blur_h = lv_area_get_height(blur_area);
  int d = r << 1 | 1;
  if (d > blur_w || d > blur_h) {
    r = (LV_MIN(blur_w, blur_h) - 1) >> 1;
  }
  if (!r) {
    return;
  }
  src += blur_area->y1 * src_stride + blur_area->x1;
  if (!dst) {
    dst = src;
    dst_stride = src_stride;
  }
  box_blur_3(src, dst, src_stride, dst_stride, blur_w, blur_h, r);
}
