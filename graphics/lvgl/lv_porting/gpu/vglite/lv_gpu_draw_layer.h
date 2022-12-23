/****************************************************************************
 * apps/graphics/lvgl/lv_porting/gpu/lv_gpu_draw_layer.h
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


#ifndef __LV_GPU_DRAW_LAYER_H__
#define __LV_GPU_DRAW_LAYER_H__

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "lv_draw_sw.h"
#include "../../misc/lv_area.h"

/****************************************************************************
 * Private Types
 ****************************************************************************/

typedef struct {
    lv_draw_layer_ctx_t base_draw;

    uint32_t buf_size_bytes: 31;
    uint32_t has_alpha : 1;
    lv_area_t area_aligned;
} lv_gpu_draw_layer_ctx_t;

/****************************************************************************
 * Public Prototypes
 ****************************************************************************/

#ifdef __cplusplus
#define EXTERN extern "C"
extern "C" {
#else
#define EXTERN extern
#endif

struct _lv_draw_layer_ctx_t * lv_gpu_draw_layer_create(struct _lv_draw_ctx_t * draw_ctx, lv_draw_layer_ctx_t * layer_ctx,
                                                      lv_draw_layer_flags_t flags);

void lv_gpu_draw_layer_adjust(struct _lv_draw_ctx_t * draw_ctx, struct _lv_draw_layer_ctx_t * layer_ctx,
                             lv_draw_layer_flags_t flags);

void lv_gpu_draw_layer_blend(struct _lv_draw_ctx_t * draw_ctx, struct _lv_draw_layer_ctx_t * layer_ctx,
                            const lv_draw_img_dsc_t * draw_dsc);

void lv_gpu_draw_layer_destroy(lv_draw_ctx_t * draw_ctx, lv_draw_layer_ctx_t * layer_ctx);

#undef EXTERN
#ifdef __cplusplus
}
#endif

#endif /* __LV_GPU_DRAW_LAYER_H__ */