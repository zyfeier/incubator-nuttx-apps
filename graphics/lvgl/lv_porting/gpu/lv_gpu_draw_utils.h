/****************************************************************************
 * apps/graphics/lvgl/lv_porting/lv_gpu_draw_utils.h
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

#ifndef __LV_GPU_DRAW_UTILS_H__
#define __LV_GPU_DRAW_UTILS_H__

/*********************
 *      INCLUDES
 *********************/

#include "lv_porting/lv_gpu_interface.h"
#include <lvgl/lvgl.h>

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/
enum {
  CURVE_NOP = 0,
  CURVE_END = 1, /* optional, must be preceded by CURVE_CLOSE */
  CURVE_LINE = 3, /* draw a line to next point */
  CURVE_CLOSE = 4, /* close the curve after this point */
  CURVE_QUAD = 5, /* the next two points will be control points */
  CURVE_CUBIC = 7, /* the next three points will be control points */
  CURVE_ARC_90 = 8, /* draw a right angle arc. the next point is center */
  CURVE_ARC_ACUTE = 9 /* draw an acute angle arc. obtuse arc should be splitted */
};
typedef uint8_t lv_gpu_curve_op_t;

enum {
  CURVE_FILL_COLOR = 0,
  CURVE_FILL_IMAGE = 0x4,
  CURVE_FILL_LINEAR_GRADIENT = 0x8,
  CURVE_FILL_RADIAL_GRADIENT = 0x10
};
typedef uint8_t lv_gpu_curve_fill_type_t;
enum {
  CURVE_FILL_RULE_NONZERO = 0,
  CURVE_FILL_RULE_EVENODD
};
enum {
  CURVE_FILL_PATTERN_COLOR = 0,
  CURVE_FILL_PATTERN_PAD = 2
};
#define CURVE_FILL_RULE(x) ((x)&1)
#define CURVE_FILL_PATTERN(x) (((x)&2) >> 1)
#define CURVE_FILL_TYPE(x) ((x) & ~3)

typedef struct {
  lv_img_dsc_t* img_dsc;
  lv_area_t* coords;
  lv_draw_img_dsc_t* draw_dsc;
} lv_gpu_image_dsc_t;

typedef struct {
  lv_gpu_curve_fill_type_t type;
  lv_color_t color;
  lv_opa_t opa;
  lv_gpu_image_dsc_t* img;
  lv_grad_dsc_t* grad;
} lv_gpu_curve_fill_t;

typedef struct {
  float x;
  float y;
} lv_fpoint_t;

typedef struct {
  lv_point_t* points;
  lv_fpoint_t* fpoints;
  uint32_t num;
  lv_gpu_curve_op_t* op;
  lv_gpu_curve_fill_t* fill;
} lv_gpu_curve_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

#ifdef __cplusplus
#define EXTERN extern "C"
extern "C" {
#else
#define EXTERN extern
#endif

/****************************************************************************
 * Name: gpu_draw_curve
 *
 * Description:
 *   Draw any curve with GPU
 *
 *   For example, to draw a Pac-Man with radias 200 at (200,200):
 *
 *          (0,0)
 *            +      * * *
 *                *        *
 *              *            *(373,100)
 *             *          *
 *     (0,200) *       *(200,200)
 *             *          *
 *              *            *(373,300)
 *                *        *
 *                   * * *
 *
 *   lv_point_t points[] = { {200,200}, {373,100}, {200,200}, {200,0},
 *      {200,200}, {0,200}, {200,200}, {200,400}, {200,200}, {373,300} };
 *   lv_gpu_curve_op_t op[] = { CURVE_LINE, CURVE_ARC_ACUTE, CURVE_NOP,
 *      CURVE_ARC_90, CURVE_NOP, CURVE_ARC_90, CURVE_NOP, CURVE_ARC_ACUTE,
 *      CURVE_NOP, CURVE_CLOSE };
 *   lv_gpu_curve_t curve = {.points = points, .op = op, .num = sizeof(op)};
 *   curve.fill = { .type = CURVE_FILL_COLOR | CURVE_FILL_RULE_EVENODD,
 *                  .color = 0xFF4F82B2,
 *                  .opa = 0x7F
 *                };
 *   lv_gpu_buffer_t gpu_buf = { .buf = buf, .w = w, .h = h, .cf = cf };
 *   gpu_draw_curve(&curve, &gpu_buf);
 *
 *   Every op indicates a shape starting from this point. CURVE_LINE will
 *   draw a line to the next point. The next point can have any op indicating
 *   the next shape starting from it, so no other op is needed. CURVE_ARC_90
 *   and CURVE_ARC_ACUTE requires the next point to be the center of the arc,
 *   so a CURVE_NOP is inserted here for clarity(other op here will be ignored
 *   anyway). Arcs larger than 90 degree should be divided into 90deg + acute
 *   angled arcs. Bezier commands (CURVE_QUAD and CURVE_CUBIC) are similarly
 *   used, with the next one or two points to be control points, respectively.
 *   The last command should always be CURVE_CLOSE, which will draw
 *   a line to the starting point (or the point after the last CURVE_CLOSE)
 *   automatically.
 *   Three (or four in the future) types of fill are supported: color, image
 *   and linear gradient. Notice that fill.opa will affect all three types,
 *   and fill.color is the pattern color to be used if image pattern mode is
 *   CURVE_FILL_PATTERN_COLOR.
 *
 * Input Parameters:
 * @param curve curve description
 * @param gpu_buf buffer description
 *
 * Returned Value:
 * @return LV_RES_OK on success, LV_RES_INV on failure.
 *
 ****************************************************************************/

LV_ATTRIBUTE_FAST_MEM lv_res_t gpu_draw_curve(lv_gpu_curve_t* curve,
    const lv_gpu_buffer_t* gpu_buf);

/****************************************************************************
 * Name: gpu_set_tf
 *
 * Description:
 *   Set transformation matrix from LVGL draw image descriptor
 *
 * Input Parameters:
 * @param matrix target transformation matrix (vg_lite_matrix_t*)
 * @param dsc LVGL draw image descriptor
 * @param coords image location
 *
 * Returned Value:
 * @return LV_RES_OK on success, LV_RES_INV on failure.
 *
 ****************************************************************************/

LV_ATTRIBUTE_FAST_MEM lv_res_t gpu_set_tf(void* matrix,
    const lv_draw_img_dsc_t* dsc, const lv_area_t* coords);

/****************************************************************************
 * Name: lv_gpu_get_buf_from_cache
 *
 * Description:
 *   Get a GPU decoder cached image buffer
 *
 * Input Parameters:
 * @param src image src (img_dsc_t or file path string)
 * @param recolor object recolor prop (image cache is unique to recolor)
 * @param frame_id frame id (normally 0)
 *
 * Returned Value:
 * @return buf pointer on success, NULL on failure.
 *
 ****************************************************************************/

void* lv_gpu_get_buf_from_cache(void* src, lv_color_t recolor, int32_t frame_id);

/****************************************************************************
 * Name: lv_gpu_draw_mask_apply_path
 *
 * Description:
 *   Convert supported masks over an area to VGLite path data. Returning true
 *   indicates that vpath->path and vpath->path_length has been modified.
 *
 * Input Parameters:
 * @param vpath targer VG path (vg_lite_path_t*)
 * @param coords area to check
 *
 * Returned Value:
 * @return true if supported mask is found, false otherwise
 *
 ****************************************************************************/

bool lv_gpu_draw_mask_apply_path(void *vpath, lv_area_t* coords);

#undef EXTERN
#ifdef __cplusplus
}
#endif

#endif /* __LV_GPU_DRAW_UTILS_H__ */
