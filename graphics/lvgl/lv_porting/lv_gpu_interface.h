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

LV_ATTRIBUTE_FAST_MEM lv_res_t _lv_fill_gpu(lv_color_t* dest_buf, lv_coord_t dest_width, lv_coord_t dest_height,
    const lv_area_t* fill_area, lv_color_t color, lv_opa_t opa);

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
    const lv_color_t* map_buf, const lv_draw_img_dsc_t* draw_dsc, bool alpha_byte);

#undef EXTERN
#ifdef __cplusplus
}
#endif

#endif /* CONFIG_LV_USE_GPU_INTERFACE */

#endif /* __LV_GPU_INTERFACE_H__ */
