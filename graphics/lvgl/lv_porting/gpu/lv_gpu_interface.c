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
#include "../lv_gpu_interface.h"
#include "lv_porting/decoder/vglite/lv_gpu_decoder.h"
#include "lv_gpu_draw_utils.h"
#include "vglite/lv_gpu_draw_layer.h"
#include "gpu_port.h"
#include "vg_lite.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#ifdef CONFIG_ARM_HAVE_MVE
#include "arm_mve.h"
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

#ifdef CONFIG_LV_GPU_DRAW_RECT
LV_ATTRIBUTE_FAST_MEM static void gpu_draw_bg(
    struct _lv_draw_ctx_t* draw_ctx,
    const lv_draw_rect_dsc_t* dsc,
    const lv_area_t* coords)
{
  if (lv_draw_bg_gpu(draw_ctx, dsc, coords) != LV_RES_OK) {
    lv_draw_sw_bg(draw_ctx, dsc, coords);
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
    lv_area_t coords_aligned16 = *coords;
    const lv_area_t* clip_area_ori = draw_ctx->clip_area;
    lv_area_t clip_area;
    if (!_lv_area_intersect(&clip_area, coords, draw_ctx->clip_area))
      return;

    vg_lite_buffer_t* vgbuf = lv_gpu_get_vgbuf((void*)map_p);
    if (!vgbuf) {
      lv_draw_sw_img_decoded(draw_ctx, dsc, coords, map_p, color_format);
    } else {
      coords_aligned16.x2 = coords_aligned16.x1 + vgbuf->width - 1;
      if (vgbuf->format == VGLITE_PX_FMT || vgbuf->format == VG_LITE_BGRA8888) {
        lv_draw_sw_img_decoded(draw_ctx, dsc, &coords_aligned16,
            map_p + sizeof(gpu_data_header_t), color_format);
      } else if (vgbuf->format == VG_LITE_INDEX_8) {
        uint8_t* buf = gpu_heap_alloc(vgbuf->width * vgbuf->height << 2);
        if (buf) {
          lv_img_header_t header = { .w = vgbuf->width, .h = vgbuf->height };
          const uint32_t* palette = (const uint32_t*)(map_p
              + sizeof(gpu_data_header_t) + vgbuf->height * vgbuf->stride);
          convert_indexed8_to_argb8888(buf, vgbuf->width * sizeof(uint32_t),
              vgbuf->memory, vgbuf->stride, palette, &header);
          lv_draw_sw_img_decoded(draw_ctx, dsc, &coords_aligned16,
              buf, LV_IMG_CF_TRUE_COLOR_ALPHA);
          gpu_heap_free(buf);
        }
      } else {
        LV_LOG_ERROR("Unexpected draw img failure, type: %d", vgbuf->format);
      }
    }
    draw_ctx->clip_area = clip_area_ori;
  }
}
#endif

LV_ATTRIBUTE_FAST_MEM static void gpu_wait(struct _lv_draw_ctx_t* draw)
{
#if 0
  gpu_wait_area(draw->clip_area);
#else
  vg_lite_finish();
#endif
}

LV_ATTRIBUTE_FAST_MEM static void gpu_draw_blend(lv_draw_ctx_t* draw_ctx,
    const lv_draw_sw_blend_dsc_t* dsc)
{
  if (lv_gpu_draw_blend(draw_ctx, dsc) != LV_RES_OK) {
    return lv_draw_sw_blend_basic(draw_ctx, dsc);
  }
}

/****************************************************************************
 * Global Functions
 ****************************************************************************/
/****************************************************************************
 * Name: init_vg_buf
 *
 * Description:
 *   Fills vg_lite_buffer_t structure according given parameters.
 *
 * Input Parameters:
 * @param vdst Buffer structure to be filled
 * @param width Width of buffer in pixels
 * @param height Height of buffer in pixels
 * @param stride Stride of the buffer in bytes
 * @param ptr Pointer to the buffer (must be aligned according VG-Lite
 *   requirements)
 * @param format Destination buffer format (vg_lite_buffer_format_t)
 * @param source if true, stride alignment check will be performed
 *
 * Returned Value:
 * @return LV_RES_OK on success, LV_RES_INV on failure.
 *
 ****************************************************************************/
LV_ATTRIBUTE_FAST_MEM lv_res_t init_vg_buf(void* vdst, uint32_t width,
    uint32_t height, uint32_t stride, void* ptr, uint8_t format, bool source)
{
  vg_lite_buffer_t* dst = vdst;
  if (source && (width & 0xF)) { /*Test for stride alignment*/
    GPU_WARN("Buffer width (%ld) not aligned to 16px.", width);
    return -1;
  }

  lv_memset_00(vdst, sizeof(vg_lite_buffer_t));
  dst->format = format;
  dst->tiled = VG_LITE_LINEAR;
  dst->image_mode = VG_LITE_NORMAL_IMAGE_MODE;
  dst->transparency_mode = VG_LITE_IMAGE_OPAQUE;

  dst->width = width;
  dst->height = height;
  dst->stride = stride;

  lv_memset_00(&dst->yuv, sizeof(dst->yuv));

  dst->memory = ptr;
  dst->address = (uint32_t)(uintptr_t)ptr;
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
  gpu_draw_ctx->base_draw.draw_line = gpu_draw_line;
#endif
#ifdef CONFIG_LV_GPU_DRAW_ARC
  gpu_draw_ctx->base_draw.draw_arc = gpu_draw_arc;
#endif
#ifdef CONFIG_LV_GPU_DRAW_POLYGON
  gpu_draw_ctx->base_draw.draw_polygon = gpu_draw_polygon;
#endif
#ifdef CONFIG_LV_GPU_DRAW_RECT
  gpu_draw_ctx->base_draw.draw_rect = gpu_draw_rect;
  gpu_draw_ctx->base_draw.draw_bg = gpu_draw_bg;
#endif
#ifdef CONFIG_LV_GPU_DRAW_IMG
  gpu_draw_ctx->base_draw.draw_img_decoded = gpu_draw_img_decoded;
#endif
#if defined(CONFIG_ARM_HAVE_MVE) && LV_COLOR_DEPTH == 32
  gpu_draw_ctx->blend = gpu_draw_blend;
#else
  LV_UNUSED(gpu_draw_blend);
#endif
  gpu_draw_ctx->base_draw.layer_init = lv_gpu_draw_layer_create;
  gpu_draw_ctx->base_draw.layer_adjust = lv_gpu_draw_layer_adjust;
  gpu_draw_ctx->base_draw.layer_blend = lv_gpu_draw_layer_blend;
  gpu_draw_ctx->base_draw.layer_destroy = lv_gpu_draw_layer_destroy;
  gpu_draw_ctx->base_draw.layer_instance_size = sizeof(lv_gpu_draw_layer_ctx_t);
  gpu_draw_ctx->base_draw.wait_for_finish = gpu_wait;
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
 * @return LV_RES_OK on success; LV_RES_INV on failure. (always succeed)
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
 * @return power mode from lv_gpu_mode_t
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
 * @param mode - power mode from lv_gpu_mode_t
 *
 * Returned Value:
 * @return LV_RES_OK on success; LV_RES_INV on failure.
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

  return LV_RES_OK;
}

/****************************************************************************
 * Name: gpu_heap_alloc
 *
 * Description:
 *   Allocate space in GPU custom heap.
 *
 * Input Parameters:
 * @param[in] size bytes to allocate
 *
 * Returned Value:
 * @return address of allocated space on success, NULL on failure.
 *
 ****************************************************************************/

LV_ATTRIBUTE_FAST_MEM FAR void* gpu_heap_alloc(size_t size)
{
  return malloc(size);
}

/****************************************************************************
 * Name: gpu_heap_aligned_alloc
 *
 * Description:
 *   Allocate space in GPU custom heap with aligned address.
 *
 * Input Parameters:
 * @param[in] size bytes to allocate
 *
 * Returned Value:
 * @return address of allocated space on success, NULL on failure.
 *
 ****************************************************************************/

LV_ATTRIBUTE_FAST_MEM FAR void* gpu_heap_aligned_alloc(size_t alignment,
    size_t size)
{
  return aligned_alloc(alignment, size);
}

/****************************************************************************
 * Name: gpu_heap_realloc
 *
 * Description:
 *   Realloc space in GPU custom heap.
 *
 * Input Parameters:
 * @param[in] mem address of memory to reallocate
 * @param[in] size desired allocated size
 *
 * Returned Value:
 * @return address of reallocated space on success, NULL on failure.
 *
 ****************************************************************************/

LV_ATTRIBUTE_FAST_MEM void* gpu_heap_realloc(FAR void* mem, size_t size)
{
  return realloc(mem, size);
}

/****************************************************************************
 * Name: gpu_heap_free
 *
 * Description:
 *   Free space in GPU custom heap.
 *
 * Input Parameters:
 * @param[in] mem address of memory to free
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

LV_ATTRIBUTE_FAST_MEM void gpu_heap_free(FAR void* mem)
{
  free(mem);
}