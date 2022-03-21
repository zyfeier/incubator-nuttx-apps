/****************************************************************************
 * apps/graphics/lvgl/lv_porting/lv_gpu_draw_utils.c
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

#include "lv_gpu_draw_utils.h"
#include "lv_color.h"
#include "lv_gpu_decoder.h"
#include "src/lv_conf_internal.h"
#include "vg_lite.h"
#include <math.h>
#include <nuttx/cache.h>
#include <stdio.h>
#include <stdlib.h>

/****************************************************************************
 * Preprocessor Definitions
 ****************************************************************************/

/****************************************************************************
 * Macros
 ****************************************************************************/

#define SIGN(x) ((x) > 0 ? 1 : ((x < 0) ? -1 : 0))

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* Magic number from https://spencermortensen.com/articles/bezier-circle/ */
static const float arc_magic = 0.55191502449351f;
static uint32_t grad_mem[VLC_GRADBUFFER_WIDTH];
static uint32_t last_grad_hash = 0;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static inline float get_angle(lv_point_t* p)
{
  lv_point_t u = { p[0].x - p[1].x, p[0].y - p[1].y };
  lv_point_t v = { p[2].x - p[1].x, p[2].y - p[1].y };
  float det = u.x * v.y - u.y * v.x;
  float denom = sqrtf((u.x * u.x + u.y * u.y) * (v.x * v.x + v.y * v.y));
  float angle = asinf(det / denom);
  return fabsf(angle);
}

static inline float get_angle_f(lv_fpoint_t* p)
{
  lv_fpoint_t u = { p[0].x - p[1].x, p[0].y - p[1].y };
  lv_fpoint_t v = { p[2].x - p[1].x, p[2].y - p[1].y };
  float det = u.x * v.y - u.y * v.x;
  float denom = sqrtf((u.x * u.x + u.y * u.y) * (v.x * v.x + v.y * v.y));
  float angle = asinf(det / denom);
  return fabsf(angle);
}

static inline void update_area(lv_area_t* a, lv_point_t p)
{
  if (p.x < a->x1) {
    a->x1 = p.x;
  } else if (p.x > a->x2) {
    a->x2 = p.x;
  }
  if (p.y < a->y1) {
    a->y1 = p.y;
  } else if (p.y > a->y2) {
    a->y2 = p.y;
  }
}

static inline void update_area_f(lv_area_t* a, lv_fpoint_t p)
{
  if (p.x < a->x1) {
    a->x1 = (lv_coord_t)p.x;
  } else if (p.x > a->x2) {
    a->x2 = (lv_coord_t)p.x;
  }
  if (p.y < a->y1) {
    a->y1 = (lv_coord_t)p.y;
  } else if (p.y > a->y2) {
    a->y2 = (lv_coord_t)p.y;
  }
}

static inline uint32_t calc_curve_length(lv_gpu_curve_t* curve)
{
  uint32_t sum = 1; /* VLC_OP_END */
  for (uint32_t i = 0; i < curve->num; i++) {
    /* the value of curve_op is designed to be the same as actual size */
    if (curve->op[i] >= CURVE_ARC_90) {
      sum += CURVE_CUBIC;
    } else {
      sum += curve->op[i];
    }
  }
  if (curve->op[curve->num - 1] != CURVE_CLOSE) {
    sum += 3;
  }
  return sum * sizeof(float);
}

static inline void fill_curve_path(float* path, lv_gpu_curve_t* curve,
    lv_area_t* area)
{
  uint32_t i = 0;
  int32_t dx1, dy1, dx2, dy2;
  *(uint8_t*)path++ = VLC_OP_MOVE;
  *path++ = curve->points[0].x;
  *path++ = curve->points[0].y;
  if (area) {
    area->x1 = area->x2 = curve->points[0].x;
    area->y1 = area->y2 = curve->points[0].y;
  }
  while (i < curve->num) {
    switch (curve->op[i]) {
    case CURVE_END:
      *(uint8_t*)path++ = VLC_OP_END;
      i = curve->num;
      break;
    case CURVE_LINE:
      if (i < curve->num - 1) {
        *(uint8_t*)path++ = VLC_OP_LINE;
        *path++ = curve->points[++i].x;
        *path++ = curve->points[i].y;
        if (area) {
          update_area(area, curve->points[i]);
        }
      } else {
        i = curve->num;
      }
      break;
    case CURVE_CLOSE:
      *(uint8_t*)path++ = VLC_OP_CLOSE;
      if (i++ < curve->num - 3) {
        *(uint8_t*)path++ = VLC_OP_MOVE;
        *path++ = curve->points[i].x;
        *path++ = curve->points[i].y;
        if (area) {
          update_area(area, curve->points[i]);
        }
      } else {
        i = curve->num;
      }
      break;
    case CURVE_QUAD:
      if (i < curve->num - 2) {
        *(uint8_t*)path++ = VLC_OP_QUAD;
        *path++ = curve->points[++i].x;
        *path++ = curve->points[i].y;
        *path++ = curve->points[++i].x;
        *path++ = curve->points[i].y;
        if (area) {
          update_area(area, curve->points[i]);
          update_area(area, curve->points[i - 1]);
        }
      } else {
        i = curve->num;
      }
      break;
    case CURVE_CUBIC:
      if (i < curve->num - 2) {
        *(uint8_t*)path++ = VLC_OP_CUBIC;
        *path++ = curve->points[++i].x;
        *path++ = curve->points[i].y;
        *path++ = curve->points[++i].x;
        *path++ = curve->points[i].y;
        *path++ = curve->points[++i].x;
        *path++ = curve->points[i].y;
        if (area) {
          update_area(area, curve->points[i]);
          update_area(area, curve->points[i - 1]);
          update_area(area, curve->points[i - 2]);
        }
      } else {
        i = curve->num;
      }
      break;
    case CURVE_ARC_90:
      if (i < curve->num - 2) {
        *(uint8_t*)path++ = VLC_OP_CUBIC;
        dx1 = curve->points[i + 1].x - curve->points[i].x;
        dy1 = curve->points[i + 1].y - curve->points[i].y;
        dx2 = curve->points[i + 2].x - curve->points[i + 1].x;
        dy2 = curve->points[i + 2].y - curve->points[i + 1].y;
        *path++ = curve->points[i].x + arc_magic * dy1;
        *path++ = curve->points[i].y - arc_magic * dx1;
        *path++ = curve->points[i + 2].x + arc_magic * dy2;
        *path++ = curve->points[i + 2].y - arc_magic * dx2;
        *path++ = curve->points[i + 2].x;
        *path++ = curve->points[i + 2].y;
        if (area) {
          lv_point_t ctl0 = {
            .x = (lv_coord_t) * (path - 6),
            .y = (lv_coord_t) * (path - 5)
          };
          lv_point_t ctl1 = {
            .x = (lv_coord_t) * (path - 4),
            .y = (lv_coord_t) * (path - 3)
          };
          update_area(area, ctl0);
          update_area(area, ctl1);
          update_area(area, curve->points[i + 2]);
        }
        i += 2;
      } else {
        i = curve->num;
      }
      break;
    case CURVE_ARC_ACUTE:
      if (i < curve->num - 2) {
        *(uint8_t*)path++ = VLC_OP_CUBIC;
        float theta = get_angle(&curve->points[i]);
        float c = 1.3333333f * tanf(theta * 0.25f);
        dx1 = curve->points[i + 1].x - curve->points[i].x;
        dy1 = curve->points[i + 1].y - curve->points[i].y;
        dx2 = curve->points[i + 2].x - curve->points[i + 1].x;
        dy2 = curve->points[i + 2].y - curve->points[i + 1].y;
        *path++ = curve->points[i].x + c * dy1;
        *path++ = curve->points[i].y - c * dx1;
        *path++ = curve->points[i + 2].x + c * dy2;
        *path++ = curve->points[i + 2].y - c * dx2;
        *path++ = curve->points[i + 2].x;
        *path++ = curve->points[i + 2].y;
        if (area) {
          lv_point_t ctl0 = {
            .x = (lv_coord_t) * (path - 6),
            .y = (lv_coord_t) * (path - 5)
          };
          lv_point_t ctl1 = {
            .x = (lv_coord_t) * (path - 4),
            .y = (lv_coord_t) * (path - 3)
          };
          update_area(area, ctl0);
          update_area(area, ctl1);
          update_area(area, curve->points[i + 2]);
        }
        i += 2;
      } else {
        i = curve->num;
      }
      break;

      break;
    default:
      break;
    }
  }
  *(uint8_t*)path++ = VLC_OP_END;
}

static inline void fill_curve_path_f(float* path, lv_gpu_curve_t* curve,
    lv_area_t* area)
{
  uint32_t i = 0;
  int32_t dx1, dy1, dx2, dy2;
  float c;
  *(uint8_t*)path++ = VLC_OP_MOVE;
  *path++ = curve->fpoints[0].x;
  *path++ = curve->fpoints[0].y;
  if (area) {
    area->x1 = area->x2 = curve->fpoints[0].x;
    area->y1 = area->y2 = curve->fpoints[0].y;
  }
  while (i < curve->num) {
    switch (curve->op[i]) {
    case CURVE_END:
      *(uint8_t*)path++ = VLC_OP_END;
      i = curve->num;
      break;
    case CURVE_LINE:
      if (i < curve->num - 1) {
        *(uint8_t*)path++ = VLC_OP_LINE;
        *path++ = curve->fpoints[++i].x;
        *path++ = curve->fpoints[i].y;
        if (area) {
          update_area_f(area, curve->fpoints[i]);
        }
      } else {
        i = curve->num;
      }
      break;
    case CURVE_CLOSE:
      *(uint8_t*)path++ = VLC_OP_CLOSE;
      if (i++ < curve->num - 3) {
        *(uint8_t*)path++ = VLC_OP_MOVE;
        *path++ = curve->fpoints[i].x;
        *path++ = curve->fpoints[i].y;
        if (area) {
          update_area_f(area, curve->fpoints[i]);
        }
      } else {
        i = curve->num;
      }
      break;
    case CURVE_QUAD:
      if (i < curve->num - 2) {
        *(uint8_t*)path++ = VLC_OP_QUAD;
        *path++ = curve->fpoints[++i].x;
        *path++ = curve->fpoints[i].y;
        *path++ = curve->fpoints[++i].x;
        *path++ = curve->fpoints[i].y;
        if (area) {
          update_area_f(area, curve->fpoints[i]);
          update_area_f(area, curve->fpoints[i - 1]);
        }
      } else {
        i = curve->num;
      }
      break;
    case CURVE_CUBIC:
      if (i < curve->num - 2) {
        *(uint8_t*)path++ = VLC_OP_CUBIC;
        *path++ = curve->fpoints[++i].x;
        *path++ = curve->fpoints[i].y;
        *path++ = curve->fpoints[++i].x;
        *path++ = curve->fpoints[i].y;
        *path++ = curve->fpoints[++i].x;
        *path++ = curve->fpoints[i].y;
        if (area) {
          update_area_f(area, curve->fpoints[i]);
          update_area_f(area, curve->fpoints[i - 1]);
          update_area_f(area, curve->fpoints[i - 2]);
        }
      } else {
        i = curve->num;
      }
      break;
    case CURVE_ARC_90:
      if (i < curve->num - 2) {
        *(uint8_t*)path++ = VLC_OP_CUBIC;
        dx1 = curve->fpoints[i + 1].x - curve->fpoints[i].x;
        dy1 = curve->fpoints[i + 1].y - curve->fpoints[i].y;
        dx2 = curve->fpoints[i + 2].x - curve->fpoints[i + 1].x;
        dy2 = curve->fpoints[i + 2].y - curve->fpoints[i + 1].y;
        c = SIGN(dx1 * dy2 - dx2 * dy1) * arc_magic;
        *path++ = curve->fpoints[i].x - c * dy1;
        *path++ = curve->fpoints[i].y + c * dx1;
        *path++ = curve->fpoints[i + 2].x - c * dy2;
        *path++ = curve->fpoints[i + 2].y + c * dx2;
        *path++ = curve->fpoints[i + 2].x;
        *path++ = curve->fpoints[i + 2].y;
        if (area) {
          lv_point_t ctl0 = {
            .x = (lv_coord_t) * (path - 6),
            .y = (lv_coord_t) * (path - 5)
          };
          lv_point_t ctl1 = {
            .x = (lv_coord_t) * (path - 4),
            .y = (lv_coord_t) * (path - 3)
          };
          update_area(area, ctl0);
          update_area(area, ctl1);
          update_area_f(area, curve->fpoints[i + 2]);
        }
        i += 2;
      } else {
        i = curve->num;
      }
      break;
    case CURVE_ARC_ACUTE:
      if (i < curve->num - 2) {
        *(uint8_t*)path++ = VLC_OP_CUBIC;
        float theta = get_angle_f(&curve->fpoints[i]);
        dx1 = curve->fpoints[i + 1].x - curve->fpoints[i].x;
        dy1 = curve->fpoints[i + 1].y - curve->fpoints[i].y;
        dx2 = curve->fpoints[i + 2].x - curve->fpoints[i + 1].x;
        dy2 = curve->fpoints[i + 2].y - curve->fpoints[i + 1].y;
        c = SIGN(dx1 * dy2 - dx2 * dy1) * 1.3333333f * tanf(theta * 0.25f);
        *path++ = curve->fpoints[i].x - c * dy1;
        *path++ = curve->fpoints[i].y + c * dx1;
        *path++ = curve->fpoints[i + 2].x - c * dy2;
        *path++ = curve->fpoints[i + 2].y + c * dx2;
        *path++ = curve->fpoints[i + 2].x;
        *path++ = curve->fpoints[i + 2].y;
        if (area) {
          lv_point_t ctl0 = {
            .x = (lv_coord_t) * (path - 6),
            .y = (lv_coord_t) * (path - 5)
          };
          lv_point_t ctl1 = {
            .x = (lv_coord_t) * (path - 4),
            .y = (lv_coord_t) * (path - 3)
          };
          update_area(area, ctl0);
          update_area(area, ctl1);
          update_area_f(area, curve->fpoints[i + 2]);
        }
        i += 2;
      } else {
        i = curve->num;
      }
      break;

      break;
    default:
      break;
    }
  }
  *(uint8_t*)path++ = VLC_OP_END;
}

static inline uint32_t calc_grad_hash(const lv_grad_dsc_t* grad)
{
  uint32_t hash = lv_color_to16(grad->stops[0].color) ^ grad->stops[0].frac;
  return hash << 16
      | (lv_color_to16(grad->stops[1].color) ^ grad->stops[1].frac);
}

/****************************************************************************
 * Global Functions
 ****************************************************************************/

/****************************************************************************
 * Name: gpu_draw_curve
 *
 * Description:
 *   Draw any curve with GPU
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
    const lv_gpu_buffer_t* gpu_buf)
{
  if (!curve || !curve->op || (!curve->points && !curve->fpoints)) {
    GPU_ERROR("Invalid argument");
    return LV_RES_INV;
  }
  vg_lite_error_t vgerr;
  lv_coord_t w = gpu_buf->w;
  lv_coord_t h = gpu_buf->h;
  lv_img_cf_t cf = gpu_buf->cf;
  void* buf = gpu_buf->buf;
  uint8_t bpp = lv_img_cf_get_px_size(cf);
  /* Init destination vglite buffer */
  vg_lite_buffer_t dst_vgbuf;
  LV_ASSERT(init_vg_buf(&dst_vgbuf, w, h, w * bpp >> 3, buf,
                BPP_TO_VG_FMT(bpp), false)
      == LV_RES_OK);
  uint32_t path_length = calc_curve_length(curve);
  float* path = lv_mem_alloc(path_length);
  if (!path) {
    GPU_ERROR("out of memory");
    return LV_RES_INV;
  }
  lv_memset_00(path, path_length);
  /* Convert to vglite path */
  lv_gpu_curve_fill_type_t type = curve->fill->type;
  lv_area_t grad_area;
  if (CURVE_FILL_TYPE(type) == CURVE_FILL_LINEAR_GRADIENT) {
    if (curve->points) {
      fill_curve_path(path, curve, &grad_area);
    } else {
      fill_curve_path_f(path, curve, &grad_area);
    }
  } else {
    if (curve->points) {
      fill_curve_path(path, curve, NULL);
    } else {
      fill_curve_path_f(path, curve, NULL);
    }
  }

  lv_area_t clip_area = { 0, 0, w - 1, h - 1 };
  if (gpu_buf->clip_area) {
    _lv_area_intersect(&clip_area, &clip_area, gpu_buf->clip_area);
  }
  vg_lite_path_t vpath;
  lv_memset_00(&vpath, sizeof(vpath));
  CHECK_ERROR(vg_lite_init_path(&vpath, VG_LITE_FP32, VG_LITE_HIGH, path_length,
      path, clip_area.x1, clip_area.y1, clip_area.x2, clip_area.y2));
  vg_lite_matrix_t imat;
  vg_lite_identity(&imat);

  lv_opa_t opa = curve->fill->opa;
  vg_lite_blend_t blend = VG_LITE_BLEND_SRC_OVER;
  vg_lite_filter_t filter = VG_LITE_FILTER_BI_LINEAR;
  vg_lite_fill_t fill = CURVE_FILL_RULE(type);
  vg_lite_pattern_mode_t pattern = CURVE_FILL_PATTERN(type);

  type = CURVE_FILL_TYPE(type);

  if (type == CURVE_FILL_COLOR) {
    vg_lite_color_t color = BGRA_TO_RGBA(lv_color_to32(curve->fill->color));
    if (opa < LV_OPA_MAX) {
      color = ((uint32_t)opa) << 24 | /* A */
          (((color & 0xFF0000) * opa & 0xFF000000) | /* B */
              ((color & 0xFF00) * opa & 0xFF0000) | /* G */
              ((color & 0xFF) * opa)) /* R */
              >> 8;
    }
    CHECK_ERROR(vg_lite_draw(&dst_vgbuf, &vpath, fill, &imat, blend, color));
    CHECK_ERROR(vg_lite_finish());

  } else if (type == CURVE_FILL_IMAGE) {
    const uint8_t* img_data = curve->fill->img->img_dsc->data;
    lv_img_header_t* img_header = &curve->fill->img->img_dsc->header;
    vg_lite_buffer_t* vgbuf = lv_gpu_get_vgbuf((void*)img_data);
    vg_lite_buffer_t src_vgbuf;
    bool allocated_src = false;
    if (!vgbuf) {
      if (lv_gpu_load_vgbuf(img_data, img_header, &src_vgbuf, NULL)
          != LV_RES_OK) {
        lv_mem_free(path);
        return LV_RES_INV;
      }
      allocated_src = true;
      vgbuf = &src_vgbuf;
    }
    lv_draw_img_dsc_t* draw_dsc = curve->fill->img->draw_dsc;
    lv_area_t* coords = curve->fill->img->coords;
    vg_lite_matrix_t matrix;
    gpu_set_tf(&matrix, draw_dsc, coords);
    vg_lite_color_t color = opa;
    if (opa < LV_OPA_MAX) {
      color |= color << 8;
      color |= color << 16;
      vg_lite_set_multiply_color(color);
      vgbuf->image_mode = VG_LITE_MULTIPLY_IMAGE_MODE;
    } else {
      vgbuf->image_mode = VG_LITE_NORMAL_IMAGE_MODE;
    }
    color = BGRA_TO_RGBA(lv_color_to32(curve->fill->color));
    CHECK_ERROR(vg_lite_draw_pattern(&dst_vgbuf, &vpath, fill, &imat, vgbuf,
        &matrix, blend, pattern, color, filter));
    CHECK_ERROR(vg_lite_finish());
    if (allocated_src) {
      lv_mem_free(vgbuf->memory);
    }

  } else if (type == CURVE_FILL_LINEAR_GRADIENT) {
    vg_lite_linear_gradient_t grad;
    init_vg_buf(&grad.image, VLC_GRADBUFFER_WIDTH, 1,
        VLC_GRADBUFFER_WIDTH * sizeof(uint32_t), grad_mem,
        VG_LITE_BGRA8888, false);
    lv_grad_dsc_t* lv_grad = curve->fill->grad;
    grad.count = lv_grad->stops_count;
    for (int_fast16_t i = 0; i < grad.count; i++) {
      lv_color_t color = lv_grad->stops[i].color;
      if (opa < LV_OPA_MAX) {
        color = lv_color_mix(color, lv_color_black(), opa);
        LV_COLOR_SET_A(color, opa);
      }
      grad.colors[i] = lv_color_to32(color);
      grad.stops[i] = lv_grad->stops[i].frac;
    }
    uint32_t grad_hash = calc_grad_hash(lv_grad);
    if (grad_hash != last_grad_hash) {
      vg_lite_update_grad(&grad);
      last_grad_hash = grad_hash;
    }

    vg_lite_identity(&grad.matrix);
    vg_lite_translate(grad_area.x1, grad_area.y1, &grad.matrix);
    if (lv_grad->dir == LV_GRAD_DIR_VER) {
      vg_lite_scale(1.0f, lv_area_get_height(&grad_area) / 256.0f, &grad.matrix);
      vg_lite_rotate(90.0f, &grad.matrix);
    } else {
      vg_lite_scale(lv_area_get_width(&grad_area) / 256.0f, 1.0f, &grad.matrix);
    }
    vg_lite_draw_gradient(&dst_vgbuf, &vpath, fill, &imat, &grad, blend);
    CHECK_ERROR(vg_lite_finish());

  } else if (type == CURVE_FILL_RADIAL_GRADIENT) {
    GPU_ERROR("Radial gradient unsupported at the moment");
    lv_mem_free(path);
    return LV_RES_INV;
  }
  lv_mem_free(path);
  if (IS_CACHED(dst_vgbuf.memory)) {
    up_invalidate_dcache((uintptr_t)dst_vgbuf.memory,
        (uintptr_t)dst_vgbuf.memory + dst_vgbuf.height * dst_vgbuf.stride);
  }
  return LV_RES_OK;
}

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
LV_ATTRIBUTE_FAST_MEM lv_res_t gpu_set_tf(void* vg_matrix,
    const lv_draw_img_dsc_t* dsc, const lv_area_t* coords)
{
  vg_lite_matrix_t* matrix = vg_matrix;
  vg_lite_identity(matrix);
  if (!dsc) {
    return LV_RES_OK;
  }
  uint16_t angle = dsc->angle;
  uint16_t zoom = dsc->zoom;
  lv_point_t pivot = dsc->pivot;
  vg_lite_translate(coords->x1, coords->y1, matrix);
  bool transformed = (angle != 0) || (zoom != LV_IMG_ZOOM_NONE);
  if (transformed) {
    vg_lite_translate(pivot.x, pivot.y, matrix);
    vg_lite_float_t scale = zoom * 1.0f / LV_IMG_ZOOM_NONE;
    if (zoom != LV_IMG_ZOOM_NONE) {
      vg_lite_scale(scale, scale, matrix);
    }
    if (angle != 0) {
      vg_lite_rotate(angle / 10.0f, matrix);
    }
    vg_lite_translate(-pivot.x, -pivot.y, matrix);
  }
  return LV_RES_OK;
}

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
void* lv_gpu_get_buf_from_cache(void* src, lv_color_t recolor,
    int32_t frame_id)
{
  _lv_img_cache_entry_t* cdsc = _lv_img_cache_open(src, recolor, frame_id);
  vg_lite_buffer_t* vgbuf = lv_gpu_get_vgbuf((void*)cdsc->dec_dsc.img_data);
  return vgbuf ? vgbuf->memory : NULL;
}
