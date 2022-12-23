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

#include "../lv_gpu_interface.h"
#include <lvgl/lvgl.h>
#ifdef CONFIG_LV_GPU_DRAW_LINE
#include "vglite/lv_gpu_draw_line.h"
#endif
#ifdef CONFIG_LV_GPU_DRAW_ARC
#include "vglite/lv_gpu_draw_arc.h"
#endif
#ifdef CONFIG_LV_GPU_DRAW_POLYGON
#include "vglite/lv_gpu_draw_polygon.h"
#endif
#ifdef CONFIG_LV_GPU_DRAW_RECT
#include "vglite/lv_gpu_draw_rect.h"
#endif
#ifdef CONFIG_LV_GPU_DRAW_IMG
#include "vglite/lv_gpu_draw_img.h"
#endif
#include "mve/lv_gpu_draw_blend.h"

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
#define ANGLE_RES 0.01f

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
#define CURVE_FILL_TYPE(x) ((x) & ~1)

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

typedef struct {
  lv_draw_arc_dsc_t dsc;
  float radius;
  float start_angle;
  float end_angle;
} gpu_arc_dsc_t;

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
LV_ATTRIBUTE_FAST_MEM lv_res_t gpu_draw_path(float* path, lv_coord_t length,
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
 * Name: gpu_calc_path_len
 *
 * Description:
 *   Calculate path length needed. Currently only support GPU_ARC_PATH.
 *
 * Input Parameters:
 * @param type type of curve
 * @param dsc curve descriptor
 *
 * Returned Value:
 * @return path length needed in bytes
 *
 ****************************************************************************/
LV_ATTRIBUTE_FAST_MEM uint16_t gpu_calc_path_len(gpu_fill_path_type_t type,
    const void* dsc);

/****************************************************************************
 * Name: gpu_img_alloc
 *
 * Description:
 *   Allocate memory for an image with specified size and color format
 *
 * @param w width of image
 * @param h height of image
 * @param cf a color format (`LV_IMG_CF_...`)
 * @param len pointer to save allocated buffer length
 *
 * @return pointer to the memory, which should free using gpu_img_free
 *
 ****************************************************************************/
void* gpu_img_alloc(lv_coord_t w, lv_coord_t h, lv_img_cf_t cf, uint32_t* len);

/****************************************************************************
 * Name: gpu_img_free
 *
 * Description:
 *   Free image memory allocated using gpu_img_alloc
 *
 * @param img pointer to the memory
 *
 ****************************************************************************/
void gpu_img_free(void* img);

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

/****************************************************************************
 * Name: recolor_palette
 *
 * Description:
 *   Apply recolor to index/alpha format palette
 *
 * @param dst destination palette buffer
 * @param src palette source, if NULL treated as alpha format
 * @param size palette color count, only 16 and 256 supported
 * @param recolor recolor value in ARGB8888 format
 *
 * @return None
 *
 ****************************************************************************/
LV_ATTRIBUTE_FAST_MEM void recolor_palette(lv_color32_t* dst,
    const lv_color32_t* src, uint16_t size, uint32_t recolor);

/****************************************************************************
 * Name: gpu_set_area
 *
 * Description:
 *   Set last GPU work area
 *
 * @param area current GPU draw area
 *
 * @return None
 *
 ****************************************************************************/
LV_ATTRIBUTE_FAST_MEM void gpu_set_area(const lv_area_t* area);

/****************************************************************************
 * Name: gpu_wait_area
 *
 * Description:
 *   Wait for last GPU operation to complete if current area overlaps with
 *   the last one
 *
 * @param area current GPU draw area
 *
 * @return None
 *
 ****************************************************************************/
LV_ATTRIBUTE_FAST_MEM void gpu_wait_area(const lv_area_t* area);

/****************************************************************************
 * Name: convert_argb8565_to_8888
 *
 * Description:
 *   Convert ARGB8565 to ARGB8888 format
 *
 * @param px_buf destination buffer
 * @param buf_stride destination buffer stride in bytes
 * @param px_map source buffer
 * @param map_stride source buffer stride in bytes
 * @param header LVGL source image header
 * @param recolor recolor in lv_color32_t
 *
 * @return None
 *
 ****************************************************************************/
void convert_argb8565_to_8888(uint8_t* px_buf, uint32_t buf_stride,
    const uint8_t* px_map, uint32_t map_stride, lv_img_header_t* header,
    lv_color32_t recolor);

/****************************************************************************
 * Name: convert_rgb565_to_gpu
 *
 * Description:
 *   Process RGB565 before GPU could blit it. Opaque RGB565 images will stay
 *   16bpp, while chroma-keyed images will be converted to ARGB8888.
 *
 * @param px_buf destination buffer
 * @param buf_stride destination buffer stride in bytes
 * @param px_map source buffer
 * @param map_stride source buffer stride in bytes
 * @param header LVGL source image header
 * @param recolor recolor to apply
 * @param ckey color key for transparent pixel
 *
 * @return None
 *
 ****************************************************************************/
void convert_rgb565_to_gpu(uint8_t* px_buf, uint32_t buf_stride,
    const uint8_t* px_map, uint32_t map_stride, lv_img_header_t* header,
    lv_color32_t recolor, uint32_t ckey);

/****************************************************************************
 * Name: convert_rgb888_to_gpu
 *
 * Description:
 *   Process RGB888 before GPU could blit it.
 *
 * @param px_buf destination buffer
 * @param buf_stride destination buffer stride in bytes
 * @param px_map source buffer
 * @param map_stride source buffer stride in bytes
 * @param header LVGL source image header
 * @param recolor recolor to apply
 * @param ckey color key for transparent pixel
 *
 * @return None
 *
 ****************************************************************************/
void convert_rgb888_to_gpu(uint8_t* px_buf, uint32_t buf_stride,
    const uint8_t* px_map, uint32_t map_stride, lv_img_header_t* header,
    lv_color32_t recolor, uint32_t ckey);

/****************************************************************************
 * Name: convert_argb8888_to_gpu
 *
 * Description:
 *   Process ARGB8888 before GPU could blit it. The image may be preprocessed
 *   and came here for recolor.
 *
 * @param px_buf destination buffer
 * @param buf_stride destination buffer stride in bytes
 * @param px_map source buffer
 * @param map_stride source buffer stride in bytes
 * @param header LVGL source image header
 * @param recolor recolor to apply
 * @param preprocessed mark if the image has already been pre-multiplied
 *
 * @return None
 *
 ****************************************************************************/
void convert_argb8888_to_gpu(uint8_t* px_buf, uint32_t buf_stride,
    const uint8_t* px_map, uint32_t map_stride, lv_img_header_t* header,
    lv_color32_t recolor, bool preprocessed);

/****************************************************************************
 * Name: convert_indexed8_to_argb8888
 *
 * Description:
 *   Convert indexed8 to ARGB8888. Target width must be larger than
 *   ALIGN_UP(header->w, 4).
 *
 * @param px_buf destination buffer
 * @param buf_stride destination buffer stride in bytes
 * @param px_map source buffer
 * @param map_stride source buffer stride in bytes
 * @param header LVGL source image header
 *
 * @return None
 *
 ****************************************************************************/
void convert_indexed8_to_argb8888(uint8_t* px_buf, uint32_t buf_stride,
    const uint8_t* px_map, uint32_t map_stride, const uint32_t* palette,
    lv_img_header_t* header);

#if LV_COLOR_DEPTH == 32
/****************************************************************************
 * Name: pre_zoom_gaussian_filter
 *
 * Description:
 *   Apply r=1 gaussian filter on src and save to dst. GPU format file (with
 *   gpu data header beginning at src) will be handled if ext == "gpu".
 *
 * @param dst destination buffer
 * @param src source buffer
 * @param header LVGL source image header
 * @param ext source file extension (only "gpu" will be handled atm)
 *
 * @return LV_RES_OK on success, LV_RES_INV on failure.
 *
 ****************************************************************************/
lv_res_t pre_zoom_gaussian_filter(uint8_t* dst, const uint8_t* src,
    lv_img_header_t* header, const char* ext);

/****************************************************************************
 * Name: get_filtered_image_path
 *
 * Description:
 *   Generate filtered image cache path from source path. The returned path
 *   is malloc'd and the caller is responsible for freeing it.
 *
 * @param src source path
 *
 * @return destination path
 *
 ****************************************************************************/
const char* get_filtered_image_path(const char* src);

/****************************************************************************
 * Name: generate_filtered_image
 *
 * Description:
 *   Image file at src will be read and filtered, then saved to
 *   CONFIG_GPU_IMG_CACHE_PATH and return destination path.
 *
 * @param src source path
 *
 * @return destination path
 *
 ****************************************************************************/
const char* generate_filtered_image(const char* src);
#endif /* LV_COLOR_DEPTH == 32 */

#ifdef CONFIG_ARM_HAVE_MVE
/****************************************************************************
 * Name: blend_ARGB
 *
 * Description:
 *   MVE-accelerated non-transformed ARGB image BLIT
 *
 * @param dst destination buffer
 * @param dst_stride destination buffer stride in bytes
 * @param src source image buffer
 * @param src_stride source buffer stride in bytes
 * @param draw_area target area
 * @param opa extra opacity to apply to source
 * @param premult mark if the image has already been pre-multiplied
 *
 * @return None
 *
 ****************************************************************************/
LV_ATTRIBUTE_FAST_MEM void blend_ARGB(uint8_t* dst, lv_coord_t dst_stride,
    const uint8_t* src, lv_coord_t src_stride, const lv_area_t* draw_area,
    lv_opa_t opa, bool premult);

/****************************************************************************
 * Name: blend_transform
 *
 * Description:
 *   MVE-accelerated transformed ARGB image BLIT
 *
 * @param dst destination buffer
 * @param draw_area target area
 * @param dst_stride destination buffer stride in bytes
 * @param src source image buffer
 * @param src_area source area
 * @param src_stride source buffer stride in bytes
 * @param dsc draw image descriptor
 * @param premult mark if the image has already been pre-multiplied
 *
 * @return None
 *
 ****************************************************************************/
LV_ATTRIBUTE_FAST_MEM void blend_transform(uint8_t* dst,
    const lv_area_t* draw_area, lv_coord_t dst_stride, const uint8_t* src,
    const lv_area_t* src_area, lv_coord_t src_stride,
    const lv_draw_img_dsc_t* dsc, bool premult);
#endif

#undef EXTERN
#ifdef __cplusplus
}
#endif

#endif /* __LV_GPU_DRAW_UTILS_H__ */
