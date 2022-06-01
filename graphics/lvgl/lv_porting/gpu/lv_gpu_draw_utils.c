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
#include "src/misc/lv_gc.h"
#include "vg_lite.h"
#include <math.h>
#include <nuttx/cache.h>
#include <stdlib.h>
#ifdef CONFIG_ARM_HAVE_MVE
#include "arm_mve.h"
#endif

/****************************************************************************
 * Preprocessor Definitions
 ****************************************************************************/

/****************************************************************************
 * Macros
 ****************************************************************************/

#define __SIGN(x) ((x) > 0 ? 1 : ((x < 0) ? -1 : 0))
#define __PR(p, dx, dy) ((lv_fpoint_t) { (p)->x + (dx), (p)->y - (dy) })
#define __PL(p, dx, dy) ((lv_fpoint_t) { (p)->x - (dx), (p)->y + (dy) })
#define __PB(p, dx, dy) ((lv_fpoint_t) { (p)->x - (dy), (p)->y - (dx) })
#define __PT(p, dx, dy) ((lv_fpoint_t) { (p)->x + (dy), (p)->y + (dx) })

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

static float inv_sqrt(float number)
{
  long i;
  float x2, y;
  const float threehalfs = 1.5F;

  x2 = number * 0.5F;
  y = number;
  i = *(long*)&y; // evil floating point bit level hacking
  i = 0x5f3759df - (i >> 1); // what the fuck?
  y = *(float*)&i;
  y = y * (threehalfs - (x2 * y * y)); // 1st iteration

  return y;
}

LV_ATTRIBUTE_FAST_MEM static float get_angle(float ux, float uy, float vx,
    float vy)
{
  float det = ux * vy - uy * vx;
  float norm2 = (ux * ux + uy * uy) * (vx * vx + vy * vy);
  float angle = asinf(det * inv_sqrt(norm2));
  return angle;
}

LV_ATTRIBUTE_FAST_MEM static void update_area(lv_area_t* a, float x, float y)
{
  if (x < a->x1) {
    a->x1 = (lv_coord_t)x;
  } else if (x > a->x2) {
    a->x2 = (lv_coord_t)x;
  }
  if (y < a->y1) {
    a->y1 = (lv_coord_t)y;
  } else if (y > a->y2) {
    a->y2 = (lv_coord_t)y;
  }
}

LV_ATTRIBUTE_FAST_MEM static uint32_t calc_curve_length(lv_gpu_curve_t* curve)
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

LV_ATTRIBUTE_FAST_MEM static void fill_curve_path(float* path,
    lv_gpu_curve_t* curve, lv_area_t* area)
{
  uint32_t i = 0;
  int32_t dx1, dy1, dx2, dy2;
  float c;
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
          update_area(area, *(path - 2), *(path - 1));
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
          update_area(area, *(path - 2), *(path - 1));
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
          update_area(area, *(path - 2), *(path - 1));
          update_area(area, *(path - 4), *(path - 3));
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
          update_area(area, *(path - 2), *(path - 1));
          update_area(area, *(path - 4), *(path - 3));
          update_area(area, *(path - 6), *(path - 5));
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
        c = __SIGN(dx1 * dy2 - dx2 * dy1) * arc_magic;
        *path++ = curve->points[i].x - c * dy1;
        *path++ = curve->points[i].y + c * dx1;
        *path++ = curve->points[i + 2].x - c * dy2;
        *path++ = curve->points[i + 2].y + c * dx2;
        *path++ = curve->points[i + 2].x;
        *path++ = curve->points[i + 2].y;
        if (area) {
          update_area(area, *(path - 2), *(path - 1));
          update_area(area, *(path - 4), *(path - 3));
          update_area(area, *(path - 6), *(path - 5));
        }
        i += 2;
      } else {
        i = curve->num;
      }
      break;
    case CURVE_ARC_ACUTE:
      if (i < curve->num - 2) {
        *(uint8_t*)path++ = VLC_OP_CUBIC;
        dx1 = curve->points[i + 1].x - curve->points[i].x;
        dy1 = curve->points[i + 1].y - curve->points[i].y;
        dx2 = curve->points[i + 2].x - curve->points[i + 1].x;
        dy2 = curve->points[i + 2].y - curve->points[i + 1].y;
        float theta = get_angle(dx1, dy1, dx2, dy2);
        c = 1.3333333f * tanf(theta * 0.25f);
        *path++ = curve->points[i].x - c * dy1;
        *path++ = curve->points[i].y + c * dx1;
        *path++ = curve->points[i + 2].x - c * dy2;
        *path++ = curve->points[i + 2].y + c * dx2;
        *path++ = curve->points[i + 2].x;
        *path++ = curve->points[i + 2].y;
        if (area) {
          update_area(area, *(path - 2), *(path - 1));
          update_area(area, *(path - 4), *(path - 3));
          update_area(area, *(path - 6), *(path - 5));
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

LV_ATTRIBUTE_FAST_MEM static void fill_curve_path_f(float* path,
    lv_gpu_curve_t* curve, lv_area_t* area)
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
          update_area(area, *(path - 2), *(path - 1));
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
          update_area(area, *(path - 2), *(path - 1));
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
          update_area(area, *(path - 2), *(path - 1));
          update_area(area, *(path - 4), *(path - 3));
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
          update_area(area, *(path - 2), *(path - 1));
          update_area(area, *(path - 4), *(path - 3));
          update_area(area, *(path - 6), *(path - 5));
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
        c = __SIGN(dx1 * dy2 - dx2 * dy1) * arc_magic;
        *path++ = curve->fpoints[i].x - c * dy1;
        *path++ = curve->fpoints[i].y + c * dx1;
        *path++ = curve->fpoints[i + 2].x - c * dy2;
        *path++ = curve->fpoints[i + 2].y + c * dx2;
        *path++ = curve->fpoints[i + 2].x;
        *path++ = curve->fpoints[i + 2].y;
        if (area) {
          update_area(area, *(path - 2), *(path - 1));
          update_area(area, *(path - 4), *(path - 3));
          update_area(area, *(path - 6), *(path - 5));
        }
        i += 2;
      } else {
        i = curve->num;
      }
      break;
    case CURVE_ARC_ACUTE:
      if (i < curve->num - 2) {
        *(uint8_t*)path++ = VLC_OP_CUBIC;
        dx1 = curve->fpoints[i + 1].x - curve->fpoints[i].x;
        dy1 = curve->fpoints[i + 1].y - curve->fpoints[i].y;
        dx2 = curve->fpoints[i + 2].x - curve->fpoints[i + 1].x;
        dy2 = curve->fpoints[i + 2].y - curve->fpoints[i + 1].y;
        float theta = get_angle(dx1, dy1, dx2, dy2);
        c = 1.3333333f * tanf(theta * 0.25f);
        *path++ = curve->fpoints[i].x - c * dy1;
        *path++ = curve->fpoints[i].y + c * dx1;
        *path++ = curve->fpoints[i + 2].x - c * dy2;
        *path++ = curve->fpoints[i + 2].y + c * dx2;
        *path++ = curve->fpoints[i + 2].x;
        *path++ = curve->fpoints[i + 2].y;
        if (area) {
          update_area(area, *(path - 2), *(path - 1));
          update_area(area, *(path - 4), *(path - 3));
          update_area(area, *(path - 6), *(path - 5));
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

LV_ATTRIBUTE_FAST_MEM static uint32_t calc_grad_hash(const lv_grad_dsc_t* grad)
{
  uint32_t hash = lv_color_to16(grad->stops[0].color) ^ grad->stops[0].frac;
  return hash << 16
      | (lv_color_to16(grad->stops[1].color) ^ grad->stops[1].frac);
}

LV_ATTRIBUTE_FAST_MEM static uint16_t fill_round_rect_path(float* path,
    const lv_area_t* rect, lv_coord_t radius)
{
  if (!radius) {
    *(uint8_t*)path = VLC_OP_MOVE;
    *(uint8_t*)(path + 3) = *(uint8_t*)(path + 6) = *(uint8_t*)(path + 9)
        = VLC_OP_LINE;
    *(uint8_t*)(path + 12) = VLC_OP_CLOSE;
    path[1] = path[4] = rect->x1;
    path[7] = path[10] = rect->x2 + 1;
    path[2] = path[11] = rect->y1;
    path[5] = path[8] = rect->y2 + 1;
    return GPU_RECT_PATH_LEN;
  }
  float r = radius;
  float c = arc_magic * r;
  float cx0 = rect->x1 + r;
  float cx1 = rect->x2 - r;
  float cy0 = rect->y1 + r;
  float cy1 = rect->y2 - r;
  *(uint8_t*)path++ = VLC_OP_MOVE;
  *path++ = cx0 - r;
  *path++ = cy0;
  *(uint8_t*)path++ = VLC_OP_CUBIC;
  *path++ = cx0 - r;
  *path++ = cy0 - c;
  *path++ = cx0 - c;
  *path++ = cy0 - r;
  *path++ = cx0;
  *path++ = cy0 - r;
  *(uint8_t*)path++ = VLC_OP_LINE;
  *path++ = cx1;
  *path++ = cy0 - r;
  *(uint8_t*)path++ = VLC_OP_CUBIC;
  *path++ = cx1 + c;
  *path++ = cy0 - r;
  *path++ = cx1 + r;
  *path++ = cy0 - c;
  *path++ = cx1 + r;
  *path++ = cy0;
  *(uint8_t*)path++ = VLC_OP_LINE;
  *path++ = cx1 + r;
  *path++ = cy1;
  *(uint8_t*)path++ = VLC_OP_CUBIC;
  *path++ = cx1 + r;
  *path++ = cy1 + c;
  *path++ = cx1 + c;
  *path++ = cy1 + r;
  *path++ = cx1;
  *path++ = cy1 + r;
  *(uint8_t*)path++ = VLC_OP_LINE;
  *path++ = cx0;
  *path++ = cy1 + r;
  *(uint8_t*)path++ = VLC_OP_CUBIC;
  *path++ = cx0 - c;
  *path++ = cy1 + r;
  *path++ = cx0 - r;
  *path++ = cy1 + c;
  *path++ = cx0 - r;
  *path++ = cy1;
  *(uint8_t*)path++ = VLC_OP_CLOSE;
  return GPU_POINT_PATH_LEN;
}

LV_ATTRIBUTE_FAST_MEM static uint16_t fill_line_path(float* path,
    const lv_fpoint_t* points, const lv_draw_line_dsc_t* dsc)
{
  float dx = points[1].x - points[0].x;
  float dy = points[1].y - points[0].y;
  float dl_inv = inv_sqrt(dx * dx + dy * dy);
  float r = dsc->width * 0.5f;
  float tmp = r * dl_inv;
  float w2_dx = tmp * dy;
  float w2_dy = tmp * dx;
  float c_dx = arc_magic * w2_dx;
  float c_dy = arc_magic * w2_dy;
  float* p = path;
  lv_fpoint_t tmp_p = __PL(points, w2_dx, w2_dy);
  *(uint8_t*)p++ = VLC_OP_MOVE;
  *(lv_fpoint_t*)p++ = tmp_p;
  p++;
  if (!dsc->round_start) {
    *(uint8_t*)p++ = VLC_OP_LINE;
    *(lv_fpoint_t*)p++ = __PR(points, w2_dx, w2_dy);
    p++;
  } else {
    *(uint8_t*)p++ = VLC_OP_CUBIC;
    *(lv_fpoint_t*)p++ = __PB(&tmp_p, c_dx, c_dy);
    p++;
    tmp_p = __PB(points, w2_dx, w2_dy);
    *(lv_fpoint_t*)p++ = __PL(&tmp_p, c_dx, c_dy);
    p++;
    *(lv_fpoint_t*)p++ = tmp_p;
    p++;
    *(uint8_t*)p++ = VLC_OP_CUBIC;
    *(lv_fpoint_t*)p++ = __PR(&tmp_p, c_dx, c_dy);
    p++;
    tmp_p = __PR(points, w2_dx, w2_dy);
    *(lv_fpoint_t*)p++ = __PB(&tmp_p, c_dx, c_dy);
    p++;
    *(lv_fpoint_t*)p++ = tmp_p;
    p++;
  }
  points++;
  tmp_p = __PR(points, w2_dx, w2_dy);
  *(uint8_t*)p++ = VLC_OP_LINE;
  *(lv_fpoint_t*)p++ = tmp_p;
  p++;
  if (!dsc->round_end) {
    *(uint8_t*)p++ = VLC_OP_LINE;
    *(lv_fpoint_t*)p++ = __PL(points, w2_dx, w2_dy);
    p++;
  } else {
    *(uint8_t*)p++ = VLC_OP_CUBIC;
    *(lv_fpoint_t*)p++ = __PT(&tmp_p, c_dx, c_dy);
    p++;
    tmp_p = __PT(points, w2_dx, w2_dy);
    *(lv_fpoint_t*)p++ = __PR(&tmp_p, c_dx, c_dy);
    p++;
    *(lv_fpoint_t*)p++ = tmp_p;
    p++;
    *(uint8_t*)p++ = VLC_OP_CUBIC;
    *(lv_fpoint_t*)p++ = __PL(&tmp_p, c_dx, c_dy);
    p++;
    tmp_p = __PL(points, w2_dx, w2_dy);
    *(lv_fpoint_t*)p++ = __PT(&tmp_p, c_dx, c_dy);
    p++;
    *(lv_fpoint_t*)p++ = tmp_p;
    p++;
  }
  *(uint8_t*)p++ = VLC_OP_CLOSE;
  return p - path;
}

LV_ATTRIBUTE_FAST_MEM static uint16_t fill_polygon_path(float* path,
    const lv_point_t* points, lv_coord_t num)
{
  *(uint8_t*)path++ = VLC_OP_MOVE;
  *path++ = points[0].x;
  *path++ = points[0].y;
  for (lv_coord_t i = 1; i < num; i++) {
    *(uint8_t*)path++ = VLC_OP_LINE;
    *path++ = points[i].x;
    *path++ = points[i].y;
  }
  *(uint8_t*)path++ = VLC_OP_CLOSE;
  return num * 3 + 1;
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
  if (!gpu_buf || !gpu_buf->buf || !curve || !curve->op
      || (!curve->points && !curve->fpoints) || !curve->fill) {
    GPU_ERROR("Invalid argument");
    return LV_RES_INV;
  }
  if (curve->fill->type == CURVE_FILL_LINEAR_GRADIENT && !curve->fill->grad) {
    GPU_ERROR("Invalid gradient argument");
    return LV_RES_INV;
  }
  lv_gpu_curve_fill_t fill = *curve->fill;

  uint32_t path_length = calc_curve_length(curve);
  float* path = lv_mem_buf_get(path_length);
  if (!path) {
    GPU_ERROR("out of memory");
    return LV_RES_INV;
  }
  lv_memset_00(path, path_length);
  /* Convert to vglite path */
  lv_area_t grad_area;
  if (CURVE_FILL_TYPE(fill.type) == CURVE_FILL_LINEAR_GRADIENT
      && !curve->fill->grad->coords) {
    if (curve->points) {
      fill_curve_path(path, curve, &grad_area);
    } else {
      fill_curve_path_f(path, curve, &grad_area);
    }
    fill.grad->coords = &grad_area;
  } else {
    if (curve->points) {
      fill_curve_path(path, curve, NULL);
    } else {
      fill_curve_path_f(path, curve, NULL);
    }
  }
  int ret = gpu_draw_path(path, path_length, &fill, gpu_buf);
  lv_mem_buf_release(path);
  return ret;
}

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
    lv_gpu_curve_fill_t* gpu_fill, const lv_gpu_buffer_t* gpu_buf)
{
  if (!path || !length) {
    return LV_RES_INV;
  }
  lv_coord_t w = gpu_buf->w;
  lv_coord_t h = gpu_buf->h;
  lv_img_cf_t cf = gpu_buf->cf;
  void* buf = gpu_buf->buf;
  lv_area_t* disp_area = gpu_buf->buf_area;
  uint8_t bpp = lv_img_cf_get_px_size(cf);
  /* Init destination vglite buffer */
  vg_lite_buffer_t dst_vgbuf;
  LV_ASSERT(init_vg_buf(&dst_vgbuf, w, h, w * bpp >> 3, buf,
                BPP_TO_VG_FMT(bpp), false)
      == LV_RES_OK);
  lv_area_t clip_area = { 0, 0, INT16_MAX, INT16_MAX };
  if (disp_area) {
    lv_memcpy_small(&clip_area, disp_area, sizeof(lv_area_t));
  }
  if (gpu_buf->clip_area) {
    _lv_area_intersect(&clip_area, &clip_area, gpu_buf->clip_area);
  }
  uint8_t* p_lastop = (uint8_t*)(path + length / sizeof(float) - 1);
  uint8_t original_op = *p_lastop;
  *p_lastop = VLC_OP_END;

  vg_lite_error_t vgerr;
  vg_lite_path_t vpath;
  lv_memset_00(&vpath, sizeof(vpath));
  CHECK_ERROR(vg_lite_init_path(&vpath, VG_LITE_FP32, VG_LITE_HIGH, length,
      path, clip_area.x1, clip_area.y1, clip_area.x2 + 1, clip_area.y2 + 1));
  vg_lite_matrix_t gmat;
  vg_lite_identity(&gmat);
  if (disp_area) {
    vg_lite_translate(-disp_area->x1, -disp_area->y1, &gmat);
  }
  lv_opa_t opa = gpu_fill->opa;
  vg_lite_blend_t blend = VG_LITE_BLEND_SRC_OVER;
  vg_lite_filter_t filter = VG_LITE_FILTER_BI_LINEAR;
  vg_lite_fill_t fill = CURVE_FILL_RULE(gpu_fill->type);
  vg_lite_pattern_mode_t pattern = CURVE_FILL_PATTERN(gpu_fill->type);

  lv_gpu_curve_fill_type_t type = CURVE_FILL_TYPE(gpu_fill->type);

  if (type == CURVE_FILL_COLOR) {
    vg_lite_color_t color = BGRA_TO_RGBA(lv_color_to32(gpu_fill->color));
    if (opa < LV_OPA_MAX) {
      color = ((uint32_t)opa) << 24 | /* A */
          (((color & 0xFF0000) * opa & 0xFF000000) | /* B */
              ((color & 0xFF00) * opa & 0xFF0000) | /* G */
              ((color & 0xFF) * opa)) /* R */
              >> 8;
    }
    CHECK_ERROR(vg_lite_draw(&dst_vgbuf, &vpath, fill, &gmat, blend, color));
    CHECK_ERROR(vg_lite_finish());

  } else if (type == CURVE_FILL_IMAGE) {
    lv_gpu_image_dsc_t* img = gpu_fill->img;
    const uint8_t* img_data = img->img_dsc->data;
    lv_img_header_t* img_header = &img->img_dsc->header;
    vg_lite_buffer_t* vgbuf = lv_gpu_get_vgbuf((void*)img_data);
    vg_lite_buffer_t src_vgbuf;
    bool allocated_src = false;
    if (!vgbuf) {
      lv_color32_t recolor = {
        .full = lv_color_to32(img->draw_dsc->recolor)
      };
      LV_COLOR_SET_A32(recolor, img->draw_dsc->recolor_opa);
      if (lv_gpu_load_vgbuf(img_data, img_header, &src_vgbuf, NULL, recolor, false)
          != LV_RES_OK) {
        *p_lastop = original_op;
        return LV_RES_INV;
      }
      allocated_src = true;
      vgbuf = &src_vgbuf;
    }
    lv_draw_img_dsc_t* draw_dsc = gpu_fill->img->draw_dsc;
    lv_area_t coords = { .x1 = gpu_fill->img->coords->x1,
      .y1 = gpu_fill->img->coords->y1 };
    if (disp_area) {
      coords.x1 -= disp_area->x1;
      coords.y1 -= disp_area->y1;
    }
    vg_lite_matrix_t matrix;
    gpu_set_tf(&matrix, draw_dsc, &coords);
    vg_lite_color_t color = opa;
    if (opa < LV_OPA_MAX) {
      color |= color << 8;
      color |= color << 16;
      vg_lite_set_multiply_color(color);
      vgbuf->image_mode = VG_LITE_MULTIPLY_IMAGE_MODE;
    } else {
      vgbuf->image_mode = VG_LITE_NORMAL_IMAGE_MODE;
    }
    color = BGRA_TO_RGBA(lv_color_to32(gpu_fill->color));
    CHECK_ERROR(vg_lite_draw_pattern(&dst_vgbuf, &vpath, fill, &gmat, vgbuf,
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
    lv_grad_dsc_t* lv_grad = gpu_fill->grad->grad_dsc;
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
    lv_area_t* grad_area = gpu_fill->grad->coords;
    vg_lite_identity(&grad.matrix);
    vg_lite_translate(grad_area->x1, grad_area->y1, &grad.matrix);
    if (disp_area) {
      vg_lite_translate(-disp_area->x1, -disp_area->y1, &grad.matrix);
    }
    if (lv_grad->dir == LV_GRAD_DIR_VER) {
      vg_lite_scale(1.0f, lv_area_get_height(grad_area) / 256.0f, &grad.matrix);
      vg_lite_rotate(90.0f, &grad.matrix);
    } else {
      vg_lite_scale(lv_area_get_width(grad_area) / 256.0f, 1.0f, &grad.matrix);
    }
    vg_lite_draw_gradient(&dst_vgbuf, &vpath, fill, &gmat, &grad, blend);
    CHECK_ERROR(vg_lite_finish());

  } else if (type == CURVE_FILL_RADIAL_GRADIENT) {
    GPU_ERROR("Radial gradient unsupported at the moment");
    *p_lastop = original_op;
    return LV_RES_INV;
  }

  *p_lastop = original_op;
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
void* lv_gpu_get_buf_from_cache(void* src, lv_color32_t recolor,
    int32_t frame_id)
{
  _lv_img_cache_entry_t* cdsc = _lv_img_cache_open(src, recolor, frame_id);
  vg_lite_buffer_t* vgbuf = lv_gpu_get_vgbuf((void*)cdsc->dec_dsc.img_data);
  return vgbuf ? vgbuf->memory : NULL;
}

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
bool lv_gpu_draw_mask_apply_path(void* vpath, const lv_area_t* coords)
{
  bool masked = false;
  for (uint8_t i = 0; i < _LV_MASK_MAX_NUM; i++) {
    _lv_draw_mask_common_dsc_t* comm = LV_GC_ROOT(_lv_draw_mask_list[i]).param;
    if (!comm) {
      continue;
    }
    vg_lite_path_t* v = vpath;
    if (comm->type == LV_DRAW_MASK_TYPE_RADIUS) {
      if (masked) {
        GPU_WARN("multiple mask unsupported");
        lv_mem_buf_release(v->path);
        v->path = NULL;
        break;
      }
      lv_draw_mask_radius_param_t* r = (lv_draw_mask_radius_param_t*)comm;
      lv_coord_t w = lv_area_get_width(&r->cfg.rect);
      lv_coord_t h = lv_area_get_height(&r->cfg.rect);
      if ((r->cfg.outer && !_lv_area_is_out(coords, &r->cfg.rect, r->cfg.radius))
          || !_lv_area_is_in(coords, &r->cfg.rect, r->cfg.radius)) {
        masked = true;
        uint16_t length = GPU_POINT_PATH_LEN + r->cfg.outer * GPU_RECT_PATH_LEN;
        uint16_t path_length = length * sizeof(float);
        float* path = lv_mem_buf_get(path_length);
        if (!path) {
          GPU_ERROR("out of memory");
          return false;
        }
        v->path = path;
        v->path_length = path_length;
        lv_coord_t r_short = LV_MIN(w, h) >> 1;
        lv_coord_t radius = LV_MIN(r->cfg.radius, r_short);
        uint16_t len = fill_round_rect_path(path, &r->cfg.rect, radius);

        if (r->cfg.outer) {
          fill_round_rect_path(path + len, coords, 0);
        }
        *(uint8_t*)(path + length - 1) = VLC_OP_END;
      }
    } else {
      GPU_WARN("mask type %d unsupported", comm->type);
      masked = true;
      v->path = NULL;
    }
  }
  return masked;
}

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
    gpu_fill_path_type_t type, const lv_point_t* points, const void* dsc)
{
  if (!path) {
    return 0;
  }
  int16_t len = 0;
  if (type == GPU_POINT_PATH) {
    /* point path */
    gpu_point_dsc_t* point_dsc = (gpu_point_dsc_t*)dsc;
    lv_coord_t dx = point_dsc->w;
    lv_coord_t dy = point_dsc->h;
    lv_area_t point_area = { points[0].x - dx, points[0].y - dy,
      points[1].x + dx + 1, points[1].y + dy + 1 };
    len = fill_round_rect_path(path, &point_area, LV_MIN(dx, dy));
  } else if (type == GPU_LINE_PATH) {
    /* line path */
    const lv_draw_line_dsc_t* line_dsc = dsc;
    lv_fpoint_t fpoints[2] = { __PR(points, 0.5f, 0), __PR(points + 1, 0.5f, 0) };
    len = fill_line_path(path, fpoints, line_dsc);
  } else if (type == GPU_RECT_PATH) {
    /* rect path */
    const lv_draw_rect_dsc_t* rect_dsc = dsc;
    lv_area_t* coords = (lv_area_t*)points;
    lv_coord_t w = lv_area_get_width(coords);
    lv_coord_t h = lv_area_get_height(coords);
    lv_coord_t r_short = LV_MIN(w, h) >> 1;
    lv_coord_t radius = LV_MIN(rect_dsc->radius, r_short);
    len = fill_round_rect_path(path, coords, radius);
  } else if (type == GPU_POLYGON_PATH) {
    /* polygon path */
    const gpu_polygon_dsc_t* poly_dsc = dsc;
    len = fill_polygon_path(path, points, poly_dsc->num);
  } else {
    /* TODO: add other path type fill function as needed */
  }
  return len;
}

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
void* gpu_img_alloc(lv_coord_t w, lv_coord_t h, lv_img_cf_t cf, uint32_t *len)
{
  /*Get image data size*/
  uint32_t data_size = gpu_img_buf_get_img_size(w, h, cf);
  if (data_size == 0) {
    return NULL;
  }

  /*Allocate raw buffer*/
  void* data = aligned_alloc(64, data_size);
  if (data == NULL) {
    return NULL;
  }

#if 1
  /* @todo double-check if we need this. */
  lv_memset_00((uint8_t*)data, data_size);
#endif

  if (len) {
    *len = data_size;
  }

  return data;
}

/****************************************************************************
 * Name: gpu_img_free
 *
 * Description:
 *   Free image memory allocated using gpu_img_alloc
 *
 * @param img pointer to the memory
 *
 ****************************************************************************/
void gpu_img_free(void* img)
{
  free(img);
}

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
lv_img_dsc_t* gpu_img_buf_alloc(lv_coord_t w, lv_coord_t h, lv_img_cf_t cf)
{
  /*Allocate image descriptor*/
  lv_img_dsc_t* dsc = lv_mem_alloc(sizeof(lv_img_dsc_t));
  if (dsc == NULL) {
    return NULL;
  }
  lv_memset_00(dsc, sizeof(lv_img_dsc_t));

  /*Allocate raw buffer*/
  dsc->data = gpu_img_alloc(w, h, cf, &dsc->data_size);
  if (dsc->data == NULL) {
    goto Error_handler;
  }
  lv_memset_00((uint8_t*)dsc->data, dsc->data_size);

  /*Fill in header*/
  dsc->header.always_zero = 0;
  dsc->header.w = w;
  dsc->header.h = h;
  dsc->header.cf = cf;
  return dsc;
Error_handler:
  lv_mem_free(dsc);
  return NULL;
}

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
void gpu_img_buf_free(lv_img_dsc_t* dsc)
{
  if (!dsc) return;
  if (dsc->data) {
    free((void*)dsc->data);
  }
  lv_mem_free(dsc);
}

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
void gpu_data_update(lv_img_dsc_t* dsc)
{
  gpu_data_header_t* header = (gpu_data_header_t*)dsc->data;
  if (header->magic == GPU_DATA_MAGIC) {
    header->vgbuf.address = (uint32_t)dsc->data + sizeof(gpu_data_header_t);
    header->vgbuf.memory = (void*)header->vgbuf.address;
  } else {
    header->magic = GPU_DATA_MAGIC;
    uint8_t bpp = lv_img_cf_get_px_size(dsc->header.cf);
    lv_coord_t w = ALIGN_UP(dsc->header.w, 16);
    init_vg_buf(&header->vgbuf, w, dsc->header.h, w * bpp >> 3,
        (void*)(dsc->data + sizeof(gpu_data_header_t)), BPP_TO_VG_FMT(bpp), 0);
  }
}

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
    lv_img_cf_t cf)
{
  if (cf == LV_IMG_CF_EVO) {
    return sizeof(gpu_data_header_t);
  }
  w = (cf == LV_IMG_CF_INDEXED_1BIT)   ? ALIGN_UP(w, 64)
      : (cf == LV_IMG_CF_INDEXED_2BIT) ? ALIGN_UP(w, 32)
                                       : ALIGN_UP(w, 16);
  uint8_t px_size = lv_img_cf_get_px_size(cf);
  bool indexed = cf >= LV_IMG_CF_INDEXED_1BIT && cf <= LV_IMG_CF_INDEXED_8BIT;
  bool alpha = cf >= LV_IMG_CF_ALPHA_1BIT && cf <= LV_IMG_CF_ALPHA_8BIT;
  uint32_t palette_size = indexed || alpha ? 1 << px_size : 0;
  uint32_t data_size = w * h * px_size >> 3;
  return sizeof(gpu_data_header_t) + data_size
      + palette_size * sizeof(uint32_t);
}

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
void* gpu_data_get_buf(lv_img_dsc_t* dsc)
{
  gpu_data_header_t* header = (gpu_data_header_t*)dsc->data;
  if (header->magic == GPU_DATA_MAGIC) {
    return header->vgbuf.memory;
  }
  return NULL;
}

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
uint32_t gpu_data_get_buf_size(lv_img_dsc_t* dsc)
{
  gpu_data_header_t* header = (gpu_data_header_t*)dsc->data;
  if (header->magic == GPU_DATA_MAGIC) {
    return header->vgbuf.height * header->vgbuf.stride;
  }
  return 0;
}

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
    const lv_color32_t* src, uint32_t count)
{
#ifdef CONFIG_ARM_HAVE_MVE
  if (IS_ALIGNED(src, 4)) {
    __asm volatile(
        "   .p2align 2                                                  \n"
        "   wlstp.32                lr, %[loopCnt], 1f                  \n"
        "   2:                                                          \n"
        "   vldrw.32                q0, [%[pSource]], #16               \n"
        "   vsri.32                 q1, q0, #8                          \n"
        "   vsri.32                 q1, q0, #16                         \n"
        "   vsri.32                 q1, q0, #24                         \n"
        /* pre-multiply alpha to all channels */
        "   vrmulh.u8               q0, q0, q1                          \n"
        "   vsli.32                 q0, q1, #24                         \n"
        "   vstrw.32                q0, [%[pTarget]], #16               \n"
        "   letp                    lr, 2b                              \n"
        "   1:                                                          \n"
        : [pSource] "+r"(src), [pTarget] "+r"(dst)
        : [loopCnt] "r"(count)
        : "q0", "q1", "lr", "memory");
  } else {
#endif
  while (count--) {
    dst->ch.red = LV_UDIV255(src->ch.red * src->ch.alpha);
    dst->ch.green = LV_UDIV255(src->ch.green * src->ch.alpha);
    dst->ch.blue = LV_UDIV255(src->ch.blue * src->ch.alpha);
    (dst++)->ch.alpha = (src++)->ch.alpha;
  }
#ifdef CONFIG_ARM_HAVE_MVE
  }
#endif
}

LV_ATTRIBUTE_FAST_MEM void recolor_palette(lv_color32_t* dst,
    const lv_color32_t* src, uint16_t size, uint32_t recolor)
{
  lv_opa_t opa = recolor >> 24;
  if (opa == LV_OPA_TRANSP) {
    if (!src) {
      GPU_ERROR("no src && no recolor");
      lv_memset_00(dst, size * sizeof(lv_color32_t));
      return;
    }
    gpu_pre_multiply(dst, src, size);
    return;
  }
#ifdef CONFIG_ARM_HAVE_MVE
  int32_t blkCnt = size;
  uint32_t* pwTarget = (uint32_t*)dst;
  uint32_t* phwSource = (uint32_t*)src;
  lv_opa_t mix = 255 - opa;
  if (src != NULL) {
    __asm volatile(
        "   .p2align 2                                                  \n"
        "   vdup.32                 q0, %[pRecolor]                     \n"
        "   vdup.8                  q1, %[opa]                          \n"
        "   vrmulh.u8               q0, q0, q1                          \n"
        "   vdup.8                  q1, %[mix]                          \n"
        "   wlstp.32                lr, %[loopCnt], 1f                  \n"
        "   2:                                                          \n"
        "   vldrw.32                q2, [%[pSource]], #16               \n"
        "   vsri.32                 q3, q2, #8                          \n"
        "   vsri.32                 q3, q2, #16                         \n"
        "   vsri.32                 q3, q2, #24                         \n"
        "   vrmulh.u8               q2, q2, q1                          \n"
        "   vadd.i8                 q2, q2, q0                          \n"
        /* pre-multiply */
        "   vrmulh.u8               q2, q2, q3                          \n"
        "   vsli.32                 q2, q3, #24                         \n"
        "   vstrw.32                q2, [%[pTarget]], #16               \n"
        "   letp                    lr, 2b                              \n"
        "   1:                                                          \n"
        : [pSource] "+r"(phwSource), [pTarget] "+r"(pwTarget),
        [pRecolor] "+r"(recolor)
        : [loopCnt] "r"(blkCnt), [opa] "r"(opa), [mix] "r"(mix)
        : "q0", "q1", "q2", "q3", "lr", "memory");
  } else {
    uint32_t inits[4] = { 0x0, 0x1010101, 0x2020202, 0x3030303 };
    uint32_t step = 4;
    if (size == 16) {
      step = 0x44;
      inits[1] = 0x11111111;
      inits[2] = 0x22222222;
      inits[3] = 0x33333333;
    }
    __asm volatile(
        "   .p2align 2                                                  \n"
        "   vdup.32                 q0, %[pSource]                      \n"
        "   vdup.8                  q1, %[opa]                          \n"
        "   vrmulh.u8               q0, q0, q1                          \n"
        "   vldrw.32                q1, [%[init]]                       \n"
        "   wlstp.32                lr, %[loopCnt], 1f                  \n"
        "   2:                                                          \n"
        "   vrmulh.u8               q2, q0, q1                          \n"
        "   vsli.32                 q2, q1, #24                         \n"
        "   vstrw.32                q2, [%[pTarget]], #16               \n"
        "   vadd.i8                 q1, q1, %[step]                     \n"
        "   letp                    lr, 2b                              \n"
        "   1:                                                          \n"
        : [pSource] "+r"(recolor), [pTarget] "+r"(pwTarget), [opa] "+r"(opa)
        : [loopCnt] "r"(blkCnt), [init] "r"(inits), [step] "r"(step)
        : "q0", "q1", "q2", "lr", "memory");
  }
#else
  uint16_t recolor_premult[3] = { (recolor >> 16 & 0xFF) * opa,
    (recolor >> 8 & 0xFF) * opa, (recolor & 0xFF) * opa };
  for (int_fast16_t i = 0; i < size; i++) {
    if (src != NULL) {
      lv_opa_t mix = 255 - opa;
      /* index recolor */
      if (src[i].ch.alpha == 0) {
        dst[i].full = 0;
      } else {
        LV_COLOR_SET_R32(dst[i],
            LV_UDIV255(recolor_premult[0] + LV_COLOR_GET_R32(src[i]) * mix));
        LV_COLOR_SET_G32(dst[i],
            LV_UDIV255(recolor_premult[1] + LV_COLOR_GET_G32(src[i]) * mix));
        LV_COLOR_SET_B32(dst[i],
            LV_UDIV255(recolor_premult[2] + LV_COLOR_GET_B32(src[i]) * mix));
        LV_COLOR_SET_A32(dst[i], src[i].ch.alpha);
      }
    } else {
      /* fill alpha palette */
      uint32_t opa_i = (size == 256) ? i : i * 0x11;
      LV_COLOR_SET_R32(dst[i], LV_UDIV255(recolor_premult[0]));
      LV_COLOR_SET_G32(dst[i], LV_UDIV255(recolor_premult[1]));
      LV_COLOR_SET_B32(dst[i], LV_UDIV255(recolor_premult[2]));
      LV_COLOR_SET_A32(dst[i], opa_i);
    }
  }
  gpu_pre_multiply(dst, dst, size);
#endif
}
