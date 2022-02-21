/****************************************************************************
 * apps/graphics/lvgl/lv_porting/lv_gpu_draw_arc.h
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

#ifndef __LV_GPU_DRAW_ARC_H__
#define __LV_GPU_DRAW_ARC_H__

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
 * Name: lv_draw_arc_gpu
 *
 * Description:
 *   Draw arc with GPU
 *
 * Input Parameters:
 * @param draw_ctx draw context (refer to LVGL 8.2 changelog)
 * @param dsc draw arc description
 * @param center arc center
 * @param radius arc radius
 * @param start_angle arc start angle
 * @param end_angle arc end angle
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

LV_ATTRIBUTE_FAST_MEM lv_res_t lv_draw_arc_gpu(
    struct _lv_draw_ctx_t* draw_ctx,
    const lv_draw_arc_dsc_t* dsc,
    const lv_point_t* center,
    uint16_t radius,
    uint16_t start_angle,
    uint16_t end_angle);

#undef EXTERN
#ifdef __cplusplus
}
#endif

#endif /* __LV_GPU_DRAW_ARC_H__ */
