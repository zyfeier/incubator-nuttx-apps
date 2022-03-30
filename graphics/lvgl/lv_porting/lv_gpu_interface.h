/****************************************************************************
 * apps/graphics/lvgl/lv_porting/lv_gpu_interface.h
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

#ifndef __LV_GPU_INTERFACE_H__
#define __LV_GPU_INTERFACE_H__

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <lvgl/lvgl.h>
#include <nuttx/config.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#if defined(CONFIG_LV_USE_GPU_INTERFACE)

#if defined(CONFIG_GPU_MODE_POWERSAVE)
#define LV_GPU_DEFAULT_MODE LV_GPU_MODE_POWERSAVE
#elif defined(CONFIG_GPU_MODE_BALANCED)
#define LV_GPU_DEFAULT_MODE LV_GPU_MODE_BALANCED
#else
#define LV_GPU_DEFAULT_MODE LV_GPU_MODE_PERFORMANCE
#endif

#ifdef CONFIG_LV_GPU_USE_LOG
#define LV_GPU_USE_LOG
#endif

#ifdef CONFIG_LV_GPU_USE_PERF
#define LV_GPU_USE_PERF
#endif

#define GPU_SIZE_LIMIT 240
#define GPU_SPLIT_SIZE (480 * 100)

#ifndef POSSIBLY_UNUSED
#define POSSIBLY_UNUSED __attribute__((unused))
#endif

extern const char* error_type[];
extern const uint8_t bmode[];

/****************************************************************************
 * Macros
 ****************************************************************************/

#define BPP_TO_VG_FMT(x) ((x) == 32 ? VG_LITE_BGRA8888 : (x) == 16 ? VG_LITE_BGR565  \
        : (x) == 8                                                 ? VG_LITE_INDEX_8 \
        : (x) == 4                                                 ? VG_LITE_INDEX_4 \
        : (x) == 2                                                 ? VG_LITE_INDEX_2 \
        : (x) == 1                                                 ? VG_LITE_INDEX_1 \
                                                                   : -1)

#define BPP_TO_LV_FMT(x) ((x) == 32 || (x) == 24 ? LV_IMG_CF_TRUE_COLOR_ALPHA : (x) == 16 ? LV_IMG_CF_TRUE_COLOR   \
        : (x) == 8                                                                        ? LV_IMG_CF_INDEXED_8BIT \
        : (x) == 4                                                                        ? LV_IMG_CF_INDEXED_4BIT \
        : (x) == 2                                                                        ? LV_IMG_CF_INDEXED_2BIT \
        : (x) == 1                                                                        ? LV_IMG_CF_INDEXED_1BIT \
                                                                                          : -1)

#define VG_FMT_TO_BPP(y) ((y) == VG_LITE_BGRA8888 ? 32 : (y) == VG_LITE_BGR565 ? 16 \
        : ((y) == VG_LITE_INDEX_8) || ((y) == VG_LITE_A8)                      ? 8  \
        : ((y) == VG_LITE_INDEX_4) || ((y) == VG_LITE_A4)                      ? 4  \
        : (y) == VG_LITE_INDEX_2                                               ? 2  \
        : (y) == VG_LITE_INDEX_1                                               ? 1  \
                                                                               : 0)

#define VG_FMT_TO_LV_FMT(z) ((z) == VG_LITE_A4 || (z) == VG_LITE_A8 ? (z)-VG_LITE_A4 + LV_IMG_CF_ALPHA_4BIT : BPP_TO_LV_FMT(VG_FMT_TO_BPP(z)))
#define VGLITE_PX_FMT BPP_TO_VG_FMT(LV_COLOR_DEPTH)

#define LV_BLEND_MODE_TO_VG(x) ((x) >= 0 && (x) <= LV_BLEND_MODE_MULTIPLY ? bmode[(x)] : VG_LITE_BLEND_NONE)

#define BGRA_TO_RGBA(c) (((c)&0xFF00FF00) | ((c) >> 16 & 0xFF) | ((c) << 16 & 0xFF0000))

#ifndef ALIGN_UP
#define ALIGN_UP(num, align) (((num) + ((align)-1)) & ~((align)-1))
#endif

#ifndef IS_ALIGNED
#define IS_ALIGNED(num, align) (((uint32_t)(num) & ((align)-1)) == 0)
#endif

#ifndef IS_CACHED
#define IS_CACHED(addr) (((uint32_t)addr & 0xFF000000) == 0x3C000000)
#endif

#ifdef LV_GPU_USE_LOG
#define GPU_INFO LV_LOG_INFO
#define GPU_WARN LV_LOG_WARN
#define GPU_ERROR LV_LOG_ERROR
#else
#define GPU_INFO(...)
#define GPU_WARN(...)
#define GPU_ERROR(...)
#endif

#ifdef LV_GPU_USE_PERF
#define TC_INIT             \
  volatile uint32_t time_s; \
  volatile uint32_t time_e;
#define TC_START time_s = lv_tick_get();
#define TC_END time_e = lv_tick_get();
#define _TSTR(s) #s
#define TC_REP(s)                                  \
  do {                                             \
    printf(_TSTR(s) ":%ld ms\n", time_e - time_s); \
  } while (0);
#else
#define TC_INIT
#define TC_START
#define TC_END
#define TC_REP(s)
#endif

#ifndef __func__
#define __func__ __FUNCTION__
#endif
#define IS_ERROR(status) (status > 0)
#define CHECK_ERROR(Function)                                                               \
  vgerr = Function;                                                                         \
  if (IS_ERROR(vgerr)) {                                                                    \
    GPU_ERROR("[%s: %d] failed.error type is %s\n", __func__, __LINE__, error_type[vgerr]); \
  }

/****************************************************************************
 * Type Definitions
 ****************************************************************************/

/**
 * GPU power options
 */
enum {
  LV_GPU_MODE_POWERSAVE, /**< low power mode*/
  LV_GPU_MODE_BALANCED, /**< balanced mode*/
  LV_GPU_MODE_PERFORMANCE, /**< high performance mode*/
};

typedef uint8_t lv_gpu_mode_t;

typedef struct {
  void* dst;
  const void* src;
  uint8_t dst_bpp;
  uint8_t src_bpp;
  lv_coord_t width;
  lv_coord_t height;
} lv_gpu_color_fmt_convert_dsc_t;

typedef struct {
  void* buf;
  lv_area_t *buf_area;
  lv_area_t *clip_area;
  lv_coord_t w;
  lv_coord_t h;
  lv_img_cf_t cf;
  bool premult;
} lv_gpu_buffer_t;

typedef lv_draw_ctx_t gpu_draw_ctx_t;

/****************************************************************************
 * Public Data
 ****************************************************************************/

#ifdef __cplusplus
#define EXTERN extern "C"
extern "C" {
#else
#define EXTERN extern
#endif

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

lv_res_t lv_gpu_interface_init(void);

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

lv_gpu_mode_t lv_gpu_getmode(void);

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

lv_res_t lv_gpu_setmode(lv_gpu_mode_t mode);

/****************************************************************************
 * Name: lv_gpu_color_fmt_convert
 *
 * Description:
 *   Use GPU to convert color formats (16 to/from 32).
 *
 * Input Parameters:
 * @param dsc descriptor of destination and source
 *   (see lv_gpu_color_fmt_convert_dsc_t)
 *
 * Returned Value:
 * @return LV_RES_OK on success, LV_RES_INV on failure.
 *
 ****************************************************************************/

LV_ATTRIBUTE_FAST_MEM lv_res_t lv_gpu_color_fmt_convert(
    const lv_gpu_color_fmt_convert_dsc_t* dsc);

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
    uint32_t height, uint32_t stride, void* ptr, uint8_t format, bool source);

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

void lv_gpu_draw_ctx_init(lv_disp_drv_t* drv, lv_draw_ctx_t* draw_ctx);

#undef EXTERN
#ifdef __cplusplus
}
#endif

#endif /* CONFIG_LV_USE_GPU_INTERFACE */

#endif /* __LV_GPU_INTERFACE_H__ */
