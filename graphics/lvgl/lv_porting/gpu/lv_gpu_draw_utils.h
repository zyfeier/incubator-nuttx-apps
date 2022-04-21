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

#define MAX_PATH_LENGTH 32512 /* when GPU CMD_BUF_LEN == 32k */
#define GPU_RECT_PATH_LEN 13 /* 3(MOVE) + 3(LINE) * 3 + 1(CLOSE/END) */
#define GPU_POINT_PATH_LEN 41 /* 3(MOVE) + 3(LINE) * 3 + 7(CUBIC) * 4 + 1(CLOSE/END) */
#define GPU_POINT_PATH_SIZE 164 /* GPU_POINT_PATH_LEN * sizeof(float) */
#define GPU_LINE_PATH_SIZE 52 /* (3(LINE) * 4 + 1(CLOSE/END)) * sizeof(float) */
#define GPU_LINE_PATH_ROUND_DELTA 44 /* (7(CUBIC) * 2 - 3(LINE)) * sizeof(float) */
#define GPU_POLYGON_PATH_SIZE(n) (((n)*3 + 1) * sizeof(float))

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
  lv_grad_dsc_t* grad_dsc;
  lv_area_t* coords;
} lv_gpu_grad_dsc_t;

typedef struct {
  lv_gpu_curve_fill_type_t type;
  lv_color_t color;
  lv_opa_t opa;
  lv_gpu_image_dsc_t* img;
  lv_gpu_grad_dsc_t* grad;
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

enum {
  GPU_LINE_PATH,
  GPU_RECT_PATH,
  GPU_ARC_PATH,
  GPU_POLYGON_PATH,
  GPU_CIRCLE_PATH,
  GPU_POINT_PATH
};
typedef uint8_t gpu_fill_path_type_t;

typedef struct {
  lv_coord_t w;
  lv_coord_t h;
} gpu_point_dsc_t;

typedef struct {
  lv_coord_t num;
} gpu_polygon_dsc_t;

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

void* lv_gpu_get_buf_from_cache(void* src, lv_color32_t recolor,
  int32_t frame_id);

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

bool lv_gpu_draw_mask_apply_path(void* vpath, const lv_area_t* coords);

/****************************************************************************
 * Name: gpu_draw_path
 *
 * Description:
 *   Draw a pre-computed path(use gpu_fill_path or refer to VGLite manuals).
 *
 * Input Parameters:
 * @param path address of path buffer (must be float*)
 * @param length length of path in bytes
 * @param fill gpu curve fill descriptor
 * @param gpu_buf GPU buffer descriptor
 *
 * Returned Value:
 * @return LV_RES_OK on success, LV_RES_INV on failure.
 *
 ****************************************************************************/
lv_res_t gpu_draw_path(float* path, lv_coord_t length,
    lv_gpu_curve_fill_t* fill, const lv_gpu_buffer_t* gpu_buf);

/****************************************************************************
 * Name: gpu_fill_path
 *
 * Description:
 *   Fill in a float* buffer with VGLite commands. Currently supported types:
 *     GPU_LINE_PATH: draw a line with given endpoints and description
 *         points[2] = { start, end } , lv_draw_line_dsc_t* dsc     ______
 *     GPU_POINT_PATH: draw a circle or rounded hor/ver line like: (______)
 *         points[2] = { start, end } , gpu_point_dsc_t* dsc
 *         *dsc = { w, h }, where w/h is HALF of point hor/ver dimension
 *
 * Input Parameters:
 * @param type type of curve to fill
 * @param points array of points, see above for detailed info
 * @param dsc curve descriptor, see above for detailed info
 *
 * Returned Value:
 * @return offset of path after fill (1/sizeof(float) of filled bytes)
 *
 ****************************************************************************/
LV_ATTRIBUTE_FAST_MEM uint16_t gpu_fill_path(float* path,
    gpu_fill_path_type_t type, const lv_point_t* points, const void* dsc);

/****************************************************************************
 * Name: gpu_img_buf_alloc
 *
 * Description:
 *   Allocate an image buffer in RAM
 *
 * @param w width of image
 * @param h height of image
 * @param cf a color format (`LV_IMG_CF_...`)
 *
 * @return an allocated image descriptor, or NULL on failure
 *
 ****************************************************************************/
lv_img_dsc_t* gpu_img_buf_alloc(lv_coord_t w, lv_coord_t h, lv_img_cf_t cf);

/****************************************************************************
 * Name: gpu_img_buf_free
 *
 * Description:
 *   Free GPU image buf and descriptor allocated by gpu_img_buf_alloc
 *
 * @param dsc GPU image buf descriptor to free
 *
 * @return None
 *
 ****************************************************************************/
void gpu_img_buf_free(lv_img_dsc_t* dsc);

/****************************************************************************
 * Name: gpu_data_update
 *
 * Description:
 *   Update gpu specific data header
 *
 * @param dsc gpu decoded image data descriptor
 *
 * @return None
 *
 ****************************************************************************/
void gpu_data_update(lv_img_dsc_t* dsc);

/****************************************************************************
 * Name: gpu_img_buf_get_img_size
 *
 * Description:
 *   Get gpu data size of given parameter
 *
 * @param w image width
 * @param h image height
 * @param cf image color format
 *
 * @return size of gpu image data
 *
 ****************************************************************************/
uint32_t gpu_img_buf_get_img_size(lv_coord_t w, lv_coord_t h,
    lv_img_cf_t cf);

/****************************************************************************
 * Name: gpu_data_get_buf
 *
 * Description:
 *   Get pointer to image buf inside gpu data
 *
 * @param dsc gpu decoded image data descriptor
 *
 * @return pointer to image data, or NULL on failure
 *
 ****************************************************************************/
void* gpu_data_get_buf(lv_img_dsc_t* data);

/****************************************************************************
 * Name: gpu_data_get_buf_size
 *
 * Description:
 *   Get image buffer size inside gpu data
 *
 * @param dsc gpu decoded image data descriptor
 *
 * @return image data buffer size, or 0 on failure
 *
 ****************************************************************************/
uint32_t gpu_data_get_buf_size(lv_img_dsc_t* dsc);

/****************************************************************************
 * Name: gpu_pre_multiply
 *
 * Description:
 *   Pre-multiply alpha to RGB channels
 *
 * @param dst destination pixel buffer
 * @param src source pixel buffer
 * @param count num of pixels
 *
 * @return None
 *
 ****************************************************************************/
LV_ATTRIBUTE_FAST_MEM void gpu_pre_multiply(lv_color32_t* dst,
    const lv_color32_t* src, uint32_t count);

LV_ATTRIBUTE_FAST_MEM void recolor_palette(lv_color32_t* dst,
    const lv_color32_t* src, uint16_t size, uint32_t recolor);

#undef EXTERN
#ifdef __cplusplus
}
#endif

#endif /* __LV_GPU_DRAW_UTILS_H__ */
