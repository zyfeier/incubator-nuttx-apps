/****************************************************************************
 * apps/graphics/lvgl/lv_porting/gpu/lv_gpu_draw_img.h
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

#ifndef __LV_GPU_DRAW_IMG_H__
#define __LV_GPU_DRAW_IMG_H__

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <lvgl/lvgl.h>
#include <nuttx/config.h>

/****************************************************************************
 * Public Prototypes
 ****************************************************************************/

#ifdef __cplusplus
#define EXTERN extern "C"
extern "C" {
#else
#define EXTERN extern
#endif

/****************************************************************************
 * Name: lv_draw_image_decoded_gpu
 *
 * Description:
 *   Copy a transformed map (image) to a display buffer.
 *
 * Input Parameters:
 * @param draw_ctx draw context (refer to LVGL 8.2 changelog)
 * @param dsc draw image description
 * @param coords area of the image  (absolute coordinates)
 * @param map_p pointer to GPU data struct (if decoded by GPU decoder) or
 *              pointer to an image buf (opened by other decoder)
 * @param color_format image format
 *
 * Returned Value:
 * @return LV_RES_OK on success, LV_RES_INV on failure.
 *
 ****************************************************************************/

LV_ATTRIBUTE_FAST_MEM lv_res_t lv_draw_img_decoded_gpu(
    lv_draw_ctx_t* draw_ctx, const lv_draw_img_dsc_t* dsc,
    const lv_area_t* coords, const uint8_t* map_p, lv_img_cf_t color_format);

#undef EXTERN
#ifdef __cplusplus
}
#endif

#endif /* __LV_GPU_DRAW_IMG_H__ */
