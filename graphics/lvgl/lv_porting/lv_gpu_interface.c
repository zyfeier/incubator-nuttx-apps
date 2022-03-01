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
#include "gpu_port.h"
#include "gpu/lv_gpu_decoder.h"
#include "gpu/lv_gpu_draw.h"
#include "vg_lite.h"
#include <lvgl/src/lv_conf_internal.h>
#include <nuttx/cache.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#ifdef CONFIG_ARM_HAVE_MVE
#include "arm_mve.h"
#endif
#ifdef CONFIG_LV_GPU_DRAW_LINE
#include "gpu/lv_gpu_draw_line.h"
#endif
#ifdef CONFIG_LV_GPU_DRAW_ARC
#include "gpu/lv_gpu_draw_arc.h"
#endif
#ifdef CONFIG_LV_GPU_DRAW_POLYGON
#include "gpu/lv_gpu_draw_polygon.h"
#endif
#ifdef CONFIG_LV_GPU_DRAW_RECT
#include "gpu/lv_gpu_draw_rect.h"
#endif
#ifdef CONFIG_LV_GPU_DRAW_IMG
#include "gpu/lv_gpu_draw_img.h"
#endif

/****************************************************************************
 * Preprocessor Definitions
 ****************************************************************************/

POSSIBLY_UNUSED const char* error_type[] = {
  "VG_LITE_SUCCESS",
  "VG_LITE_INVALID_ARGUMENT",
  "VG_LITE_OUT_OF_MEMORY",
  "VG_LITE_NO_CONTEXT",
  "VG_LITE_TIMEOUT",
  "VG_LITE_OUT_OF_RESOURCES",
  "VG_LITE_GENERIC_IO",
  "VG_LITE_NOT_SUPPORT",
};
const uint8_t bmode[] = {
  VG_LITE_BLEND_SRC_OVER,
  VG_LITE_BLEND_ADDITIVE,
  VG_LITE_BLEND_SUBTRACT,
  VG_LITE_BLEND_MULTIPLY
};

/****************************************************************************
 * Macros
 ****************************************************************************/

/****************************************************************************
 * Private Data
 ****************************************************************************/

static lv_gpu_mode_t power_mode;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

#ifdef CONFIG_LV_GPU_DRAW_LINE
LV_ATTRIBUTE_FAST_MEM static void gpu_draw_line(
    struct _lv_draw_ctx_t* draw_ctx,
    const lv_draw_line_dsc_t* dsc,
    const lv_point_t* point1,
    const lv_point_t* point2)
{
  if (lv_draw_line_gpu(draw_ctx, dsc, point1, point2) != LV_RES_OK) {
    lv_draw_sw_line(draw_ctx, dsc, point1, point2);
  }
}
#endif

#ifdef CONFIG_LV_GPU_DRAW_ARC
LV_ATTRIBUTE_FAST_MEM static void gpu_draw_arc(
    struct _lv_draw_ctx_t* draw_ctx,
    const lv_draw_arc_dsc_t* dsc,
    const lv_point_t* center,
    uint16_t radius,
    uint16_t start_angle,
    uint16_t end_angle)
{
  if (lv_draw_arc_gpu(draw_ctx, dsc, center, radius, start_angle, end_angle)
      != LV_RES_OK) {
    lv_draw_sw_arc(draw_ctx, dsc, center, radius, start_angle, end_angle);
  }
}
#endif

#ifdef CONFIG_LV_GPU_DRAW_POLYGON
LV_ATTRIBUTE_FAST_MEM static void gpu_draw_polygon(
    struct _lv_draw_ctx_t* draw_ctx,
    const lv_draw_rect_dsc_t* dsc,
    const lv_point_t* points,
    uint16_t point_cnt)
{
  if (lv_draw_polygon_gpu(draw_ctx, dsc, points,
          point_cnt)
      != LV_RES_OK) {
    lv_draw_sw_polygon(draw_ctx, dsc, points, point_cnt);
  }
}
#endif

#ifdef CONFIG_LV_GPU_DRAW_RECT
LV_ATTRIBUTE_FAST_MEM static void gpu_draw_rect(
    struct _lv_draw_ctx_t* draw_ctx,
    const lv_draw_rect_dsc_t* dsc,
    const lv_area_t* coords)
{
  if (lv_draw_rect_gpu(draw_ctx, dsc, coords) != LV_RES_OK) {
    lv_draw_sw_rect(draw_ctx, dsc, coords);
  }
}
#endif

#ifdef CONFIG_LV_GPU_DRAW_IMG
LV_ATTRIBUTE_FAST_MEM static void gpu_draw_img_decoded(
    struct _lv_draw_ctx_t* draw_ctx,
    const lv_draw_img_dsc_t* dsc,
    const lv_area_t* coords,
    const uint8_t* map_p,
    lv_img_cf_t color_format)
{
  if (lv_draw_img_decoded_gpu(draw_ctx, dsc, coords, map_p,
          color_format)
      != LV_RES_OK) {
    lv_draw_sw_img_decoded(draw_ctx, dsc, coords,
        map_p + sizeof(gpu_data_header_t), color_format);
  }
}
#endif

LV_ATTRIBUTE_FAST_MEM static void gpu_wait(struct _lv_draw_ctx_t* draw)
{
  vg_lite_finish();
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
 * @param[in] ptr Pointer to the buffer (must be aligned according to VGLite
 *                requirements)
 */
LV_ATTRIBUTE_FAST_MEM lv_res_t init_vg_buf(void* vdst, uint32_t width,
    uint32_t height, uint32_t stride, void* ptr, uint8_t format, bool source)
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
 * Name: lv_gpu_draw_ctx_init
 *
 * Description:
 *   GPU draw context init callback. (Do not call directly)
 *
 * Input Parameters:
 * @param drv lvgl display driver
 * @param draw_ctx lvgl draw context struct
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void lv_gpu_draw_ctx_init(lv_disp_drv_t* drv, lv_draw_ctx_t* draw_ctx)
{
  /*Initialize the parent type first */
  lv_draw_sw_init_ctx(drv, draw_ctx);

  /*Change some callbacks*/
  gpu_draw_ctx_t* gpu_draw_ctx = (gpu_draw_ctx_t*)draw_ctx;
#ifdef CONFIG_LV_GPU_DRAW_LINE
  gpu_draw_ctx->draw_line = gpu_draw_line;
#endif
#ifdef CONFIG_LV_GPU_DRAW_ARC
  gpu_draw_ctx->draw_arc = gpu_draw_arc;
#endif
#ifdef CONFIG_LV_GPU_DRAW_POLYGON
  gpu_draw_ctx->draw_polygon = gpu_draw_polygon;
#endif
#ifdef CONFIG_LV_GPU_DRAW_RECT
  gpu_draw_ctx->draw_rect = gpu_draw_rect;
#endif
#ifdef CONFIG_LV_GPU_DRAW_IMG
  gpu_draw_ctx->draw_img_decoded = gpu_draw_img_decoded;
#endif
  gpu_draw_ctx->wait_for_finish = gpu_wait;
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
#ifdef CONFIG_LV_GPU_DRAW_IMG
  lv_gpu_decoder_init();
#endif
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

LV_ATTRIBUTE_FAST_MEM lv_res_t lv_gpu_color_fmt_convert(
    const lv_gpu_color_fmt_convert_dsc_t* dsc)
{
  vg_lite_buffer_t src;
  vg_lite_buffer_t dst;
  lv_coord_t w = dsc->width;
  lv_coord_t h = dsc->height;
  vg_lite_error_t vgerr;

  if (init_vg_buf(&src, w, h, w * dsc->src_bpp >> 3, (void*)dsc->src,
          BPP_TO_VG_FMT(dsc->src_bpp), 1)
          != LV_RES_OK
      || init_vg_buf(&dst, w, h, w * dsc->dst_bpp >> 3, dsc->dst,
             BPP_TO_VG_FMT(dsc->dst_bpp), 0)
          != LV_RES_OK) {
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
    up_invalidate_dcache((uintptr_t)dst.memory,
        (uintptr_t)dst.memory + h * dst.stride);
    TC_END
    TC_REP(dst_cache_invalid)
  }
  return LV_RES_OK;
}
