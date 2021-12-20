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

#define GPU_SIZE_LIMIT    240
#define GPU_SPLIT_SIZE    (480 * 100)

/****************************************************************************
 * Macros
 ****************************************************************************/

#define BPP_TO_VG_FMT(x)  ((x) == 32 ? VG_LITE_BGRA8888 : \
                           (x) == 16 ? VG_LITE_BGR565   : \
                           (x) == 8  ? VG_LITE_INDEX_8  : \
                           (x) == 4  ? VG_LITE_INDEX_4  : \
                           (x) == 2  ? VG_LITE_INDEX_2  : \
                           (x) == 1  ? VG_LITE_INDEX_1  : -1)
#define VG_FMT_TO_BPP(y)  ((y) == VG_LITE_BGRA8888 ? 32 : \
                           (y) == VG_LITE_BGR565   ? 16 : \
                           (y) == VG_LITE_INDEX_8  ? 8  : \
                           (y) == VG_LITE_INDEX_4  ? 4  : \
                           (y) == VG_LITE_INDEX_2  ? 2  : \
                           (y) == VG_LITE_INDEX_1  ? 1  : 0)
#define VGLITE_PX_FMT     BPP_TO_VG_FMT(LV_COLOR_DEPTH)

#ifndef ALIGN_UP
#define ALIGN_UP(num, align) (((num) + ((align)-1)) & ~((align)-1))
#endif

#ifndef IS_ALIGNED
#define IS_ALIGNED(num, align) (((uint32_t)(num) & ((align)-1)) == 0)
#endif

#ifndef IS_CACHED
#define IS_CACHED(addr) (((uint32_t)addr & 0xFF000000) == 0x3C000000)
#endif

#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

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
  void *dst;
  const void *src;
  uint8_t dst_bpp;
  uint8_t src_bpp;
  lv_coord_t width;
  lv_coord_t height;
} lv_gpu_color_fmt_convert_dsc_t;

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
 *   LV_RES_OK on success; LV_RES_INV on failure.
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
 *   power mode from lv_gpu_mode_t
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
 *   mode - power mode from lv_gpu_mode_t
 *
 * Returned Value:
 *   LV_RES_OK on success; LV_RES_INV on failure.
 *
 ****************************************************************************/

lv_res_t lv_gpu_setmode(lv_gpu_mode_t mode);

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
    const lv_color_t* map_buf, const lv_draw_img_dsc_t* draw_dsc, bool chroma_key, bool alpha_byte);

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

lv_res_t lv_gpu_color_fmt_convert(const lv_gpu_color_fmt_convert_dsc_t *dsc);

/***
 * Fills vg_lite_buffer_t structure according given parameters.
 * @param[out] dst Buffer structure to be filled
 * @param[in] width Width of buffer in pixels
 * @param[in] height Height of buffer in pixels
 * @param[in] stride Stride of the buffer in bytes
 * @param[in] ptr Pointer to the buffer (must be aligned according VG-Lite requirements)
 */
LV_ATTRIBUTE_FAST_MEM lv_res_t init_vg_buf(void* vdst, uint32_t width, uint32_t height, uint32_t stride, void* ptr, uint8_t format, bool source);

LV_ATTRIBUTE_FAST_MEM void bgra5658_to_8888(const uint8_t* src, uint32_t* dst);
#undef EXTERN
#ifdef __cplusplus
}
#endif

#endif /* CONFIG_LV_USE_GPU_INTERFACE */

#endif /* __LV_GPU_INTERFACE_H__ */
