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
#include "fast_gaussian_blur.h"
#include "lv_color.h"
#include "lv_gpu_decoder.h"
#include "src/misc/lv_gc.h"
#include "vg_lite.h"
#include <math.h>
#include <stdlib.h>
#include <sys/stat.h>
#ifdef CONFIG_ARM_HAVE_MVE
#include "arm_mve.h"
#endif

/****************************************************************************
 * Preprocessor Definitions
 ****************************************************************************/

#define ARC_MAX_POINTS 25
#define PI_DEG (M_PI / 180.0f)

/****************************************************************************
 * Macros
 ****************************************************************************/

#define __PF(x, y) ((lv_fpoint_t) { (x), (y) }) /* float */
#define __PT(p, dx, dy) __PF((p)->x + (dy), (p)->y + (dx)) /* top */
#define __PB(p, dx, dy) __PF((p)->x - (dy), (p)->y - (dx)) /* bottom */
#define __PL(p, dx, dy) __PF((p)->x - (dx), (p)->y + (dy)) /* left */
#define __PR(p, dx, dy) __PF((p)->x + (dx), (p)->y - (dy)) /* right */
#define __PO(p, dx, dy) __PF((p)->x + (dx), (p)->y + (dy)) /* offset */
#define __PP(p, r, c, s) __PF((p)->x + (r) * (c), (p)->y + (r) * (s)) /*polar*/

#define __SIGN(x) ((x) > 0 ? 1 : ((x < 0) ? -1 : 0))

#define SINF(deg) sinf((deg)*PI_DEG)
#define COSF(deg) cosf((deg)*PI_DEG)

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* Magic number from https://spencermortensen.com/articles/bezier-circle/ */
static const float arc_magic = 0.55191502449351f;
static uint32_t grad_mem[VLC_GRADBUFFER_WIDTH];
static uint32_t last_grad_hash = 0;
static lv_fpoint_t arc_points[ARC_MAX_POINTS];
static lv_gpu_curve_op_t arc_op[ARC_MAX_POINTS];
static lv_gpu_curve_t arc_curve = {
  .fpoints = arc_points,
  .op = arc_op,
  .num = 0
};
static lv_area_t gpu_area = { 0 };

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

static inline lv_fpoint_t get_rotated(const lv_point_t* center,
    float radius, float cos, float sin, uint8_t j)
{
  switch (j & 3) {
  case 3:
    return __PP(center, radius, sin, -cos);
  case 2:
    return __PP(center, radius, -cos, -sin);
  case 1:
    return __PP(center, radius, -sin, cos);
  case 0:
  default:
    return __PP(center, radius, cos, sin);
  }
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

LV_ATTRIBUTE_FAST_MEM static uint16_t fill_arc_path(float* path,
    const lv_point_t* center, const gpu_arc_dsc_t* dsc)
{
  float start_angle = dsc->start_angle;
  float end_angle = dsc->end_angle;
  float radius = dsc->radius;
  lv_fpoint_t* points = arc_points;
  lv_gpu_curve_op_t* op = arc_op;
  lv_memset_00(arc_op, sizeof(arc_op));
  float angle = end_angle - start_angle;
  if (fabs(angle) < ANGLE_RES) {
    op[0] = CURVE_ARC_90;
    points[0] = __PF(center->x + radius, center->y);
    points[1] = __PF(center->x, center->y);
    op[2] = CURVE_ARC_90;
    points[2] = __PF(center->x, center->y + radius);
    points[3] = points[1];
    op[4] = CURVE_ARC_90;
    points[4] = __PF(center->x - radius, center->y);
    points[5] = points[1];
    op[6] = CURVE_ARC_90;
    points[6] = __PF(center->x, center->y - radius);
    points[7] = points[1];
    op[8] = CURVE_CLOSE;
    points[8] = points[0];
    arc_curve.num = 8;
    if (dsc->dsc.width - radius < ANGLE_RES) {
      float inner_radius = radius - dsc->dsc.width;
      op[9] = CURVE_ARC_90;
      points[9] = __PF(center->x + inner_radius, center->y);
      points[10] = points[1];
      op[11] = CURVE_ARC_90;
      points[11] = __PF(center->x, center->y + inner_radius);
      points[12] = points[1];
      op[13] = CURVE_ARC_90;
      points[13] = __PF(center->x - inner_radius, center->y);
      points[14] = points[1];
      op[15] = CURVE_ARC_90;
      points[15] = __PF(center->x, center->y - inner_radius);
      points[16] = points[1];
      op[17] = CURVE_CLOSE;
      points[17] = points[9];
      arc_curve.num = 18;
    }
  } else {
    float st_sin = SINF(start_angle);
    float st_cos = COSF(start_angle);
    float ed_sin = SINF(end_angle);
    float ed_cos = COSF(end_angle);
    float width = LV_MIN(dsc->dsc.width, radius);
    points[0] = __PP(center, radius - width, st_cos, st_sin);
    op[0] = CURVE_LINE;
    lv_coord_t i = 1;
    if (dsc->dsc.rounded) {
      op[i - 1] = CURVE_ARC_90;
      points[i++] = __PP(center, radius - width * 0.5f, st_cos, st_sin);
      lv_fpoint_t* mid = &points[i - 1];
      op[i] = CURVE_ARC_90;
      points[i++] = __PO(mid, width * 0.5f * st_sin, -width * 0.5f * st_cos);
      points[i++] = *mid;
    }
    uint8_t j = 0;
    while (angle > -ANGLE_RES) {
      op[i] = angle < 90.0f ? CURVE_ARC_ACUTE : CURVE_ARC_90;
      points[i++] = get_rotated(center, radius, st_cos, st_sin, j++);
      points[i++] = __PF(center->x, center->y);
      angle -= 90.0f;
    }
    op[i] = CURVE_LINE;
    points[i++] = __PP(center, radius, ed_cos, ed_sin);
    if (dsc->dsc.rounded) {
      op[i - 1] = CURVE_ARC_90;
      points[i++] = __PP(center, radius - width * 0.5f, ed_cos, ed_sin);
      lv_fpoint_t* mid = &points[i - 1];
      op[i] = CURVE_ARC_90;
      points[i++] = __PO(mid, -width * 0.5f * ed_sin, width * 0.5f * ed_cos);
      points[i++] = *mid;
    }
    if (dsc->dsc.width < radius) {
      angle = end_angle - start_angle;
      op[i] = angle < 90.0f ? CURVE_ARC_ACUTE : CURVE_ARC_90;
      j = 4;
      while (angle > -ANGLE_RES) {
        op[i] = angle < 90.0f ? CURVE_ARC_ACUTE : CURVE_ARC_90;
        points[i++] = get_rotated(center, radius - width, ed_cos, ed_sin, j--);
        points[i++] = __PF(center->x, center->y);
        angle -= 90.0f;
      }
    }
    op[i] = CURVE_CLOSE;
    points[i++] = __PP(center, radius - width, st_cos, st_sin);
    arc_curve.num = i;
  }
  fill_curve_path_f(path, &arc_curve, NULL);

  return calc_curve_length(&arc_curve) / sizeof(float);
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
LV_ATTRIBUTE_FAST_MEM lv_res_t gpu_draw_path(float* path, lv_coord_t length,
    lv_gpu_curve_fill_t* gpu_fill, const lv_gpu_buffer_t* gpu_buf)
{
  if (!path || !length || !gpu_fill) {
    GPU_ERROR("invalid arguments");
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
    CHECK_ERROR(vg_lite_flush());

  } else if (type == CURVE_FILL_IMAGE) {
    lv_gpu_image_dsc_t* img = gpu_fill->img;
    if (!img) {
      *p_lastop = original_op;
      GPU_ERROR("no img dsc");
      return LV_RES_INV;
    }
    const uint8_t* img_data = img->img_dsc->data;
    lv_img_header_t* img_header = &img->img_dsc->header;
    lv_draw_img_dsc_t* draw_dsc = img->draw_dsc;
    if (!draw_dsc) {
      *p_lastop = original_op;
      GPU_ERROR("no draw img dsc");
      return LV_RES_INV;
    }
    vg_lite_buffer_t* vgbuf = lv_gpu_get_vgbuf((void*)img_data);
    vg_lite_buffer_t src_vgbuf;
    bool allocated_src = false;
    if (!vgbuf) {
      lv_color32_t recolor = {
        .full = lv_color_to32(draw_dsc->recolor)
      };
      GPU_WARN("get vgbuf failed");
      LV_COLOR_SET_A32(recolor, draw_dsc->recolor_opa);
      if (lv_gpu_load_vgbuf(img_data, img_header, &src_vgbuf, NULL, recolor, false)
          != LV_RES_OK) {
        *p_lastop = original_op;
        GPU_ERROR("load vgbuf error");
        return LV_RES_INV;
      }
      allocated_src = true;
      vgbuf = &src_vgbuf;
    }
    if (vgbuf->format >= VG_LITE_INDEX_1 && vgbuf->format <= VG_LITE_INDEX_8) {
      uint32_t* palette = (uint32_t*)(img_data + sizeof(gpu_data_header_t)
          + vgbuf->stride * vgbuf->height);
      uint8_t px_size = VG_FMT_TO_BPP(vgbuf->format);
      uint16_t palette_size = 1 << px_size;
      vg_lite_set_CLUT(palette_size, palette);
    }
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
    CHECK_ERROR(vg_lite_flush());
    if (allocated_src) {
      lv_mem_free(vgbuf->memory);
    }

  } else if (type == CURVE_FILL_LINEAR_GRADIENT) {
    lv_gpu_grad_dsc_t* ggrad = gpu_fill->grad;
    if (!ggrad) {
      *p_lastop = original_op;
      GPU_ERROR("no grad dsc");
      return LV_RES_INV;
    }
    vg_lite_linear_gradient_t grad;
    init_vg_buf(&grad.image, VLC_GRADBUFFER_WIDTH, 1,
        VLC_GRADBUFFER_WIDTH * sizeof(uint32_t), grad_mem,
        VG_LITE_BGRA8888, false);
    lv_grad_dsc_t* lv_grad = ggrad->grad_dsc;
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
    lv_area_t* grad_area = ggrad->coords;
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
    CHECK_ERROR(vg_lite_flush());

  } else if (type == CURVE_FILL_RADIAL_GRADIENT) {
    *p_lastop = original_op;
    GPU_ERROR("Radial gradient unsupported at the moment");
    return LV_RES_INV;
  }

  *p_lastop = original_op;
  gpu_set_area(&clip_area);
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
      vg_lite_rotate(angle * 0.1f, matrix);
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
  } else if (type == GPU_ARC_PATH) {
    /* arc path */
    const gpu_arc_dsc_t* arc_dsc = dsc;
    len = fill_arc_path(path, points, arc_dsc);
  } else {
    /* TODO: add other path type fill function as needed */
  }
  return len;
}

/****************************************************************************
 * Name: gpu_calc_path_len
 *
 * Description:
 *   Pre-calculate the path length. Currently only support GPU_ARC_PATH.
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
    const void* dsc)
{
  uint16_t len = 0;
  if (type == GPU_ARC_PATH) {
    gpu_arc_dsc_t* arc_dsc = (gpu_arc_dsc_t*)dsc;
    float angle = arc_dsc->end_angle - arc_dsc->start_angle;
    while (angle > 360.0f) {
      angle -= 360.0f;
    }
    while (angle < -ANGLE_RES) {
      angle += 360.0f;
    }
    bool circle = fabs(angle) < ANGLE_RES;
    uint32_t right_angles = (uint32_t)floorf(angle / 90.0f + 1);
    len = circle ? 65
                 : 11 + arc_dsc->dsc.rounded * 22 + right_angles * 14;
  } else {
    /* TODO: add other path type calculation as needed */
  }
  return len << 2;
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
void* gpu_img_alloc(lv_coord_t w, lv_coord_t h, lv_img_cf_t cf, uint32_t* len)
{
  /*Get image data size*/
  uint32_t data_size = gpu_img_buf_get_img_size(w, h, cf);
  if (data_size == 0) {
    return NULL;
  }

  /*Allocate raw buffer*/
  void* data = gpu_heap_aligned_alloc(64, data_size);
  if (data == NULL) {
    return NULL;
  }

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
  gpu_heap_free(img);
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
  ((gpu_data_header_t*)dsc->data)->magic = 0;

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
  if (!dsc)
    return;
  if (dsc->data) {
    gpu_img_free((void*)dsc->data);
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
LV_ATTRIBUTE_FAST_MEM void gpu_pre_multiply(lv_color_t* dst,
    const lv_color_t* src, uint32_t count)
{
#if defined(CONFIG_ARM_HAVE_MVE) && LV_COLOR_DEPTH == 32
  __asm volatile(
      "   .p2align 2                                                  \n"
      "   wlstp.8                 lr, %[loopCnt], 1f                  \n"
      "   2:                                                          \n"
      "   vldrb.8                 q0, [%[pSource]], #16               \n"
      "   vsri.32                 q1, q0, #8                          \n"
      "   vsri.32                 q1, q0, #16                         \n"
      "   vsri.32                 q1, q0, #24                         \n"
      /* pre-multiply alpha to all channels */
      "   vrmulh.u8               q0, q0, q1                          \n"
      "   vsli.32                 q0, q1, #24                         \n"
      "   vstrb.8                 q0, [%[pTarget]], #16               \n"
      "   letp                    lr, 2b                              \n"
      "   1:                                                          \n"
      : [pSource] "+r"(src), [pTarget] "+r"(dst)
      : [loopCnt] "r"(count << 2)
      : "q0", "q1", "lr", "memory");
#else
  while (count--) {
    dst->ch.red = LV_UDIV255(src->ch.red * src->ch.alpha);
    dst->ch.green = LV_UDIV255(src->ch.green * src->ch.alpha);
    dst->ch.blue = LV_UDIV255(src->ch.blue * src->ch.alpha);
    (dst++)->ch.alpha = (src++)->ch.alpha;
  }
#endif
}

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
    const lv_color32_t* src, uint16_t size, uint32_t recolor)
{
  lv_opa_t opa = recolor >> 24;
  if (opa == LV_OPA_TRANSP) {
    if (!src) {
      GPU_WARN("alpha recolor unspecified, default to black");
      recolor = 0xFFFFFFFF;
    } else {
      gpu_pre_multiply(dst, src, size);
      return;
    }
  }
#ifdef CONFIG_ARM_HAVE_MVE
  int32_t blkCnt = size << 2;
  uint32_t* pwTarget = (uint32_t*)dst;
  uint32_t* phwSource = (uint32_t*)src;
  if (src != NULL) {
    __asm volatile(
        "   .p2align 2                                                  \n"
        "   vdup.32                 q0, %[pRecolor]                     \n"
        "   vdup.8                  q1, %[opa]                          \n"
        "   vrmulh.u8               q0, q0, q1                          \n"
        "   vmvn                    q1, q1                              \n"
        "   wlstp.8                 lr, %[loopCnt], 1f                  \n"
        "   2:                                                          \n"
        "   vldrb.8                 q2, [%[pSource]], #16               \n"
        "   vsri.32                 q3, q2, #8                          \n"
        "   vsri.32                 q3, q2, #16                         \n"
        "   vsri.32                 q3, q2, #24                         \n"
        "   vrmulh.u8               q2, q2, q1                          \n"
        "   vadd.i8                 q2, q2, q0                          \n"
        /* pre-multiply */
        "   vrmulh.u8               q2, q2, q3                          \n"
        "   vsli.32                 q2, q3, #24                         \n"
        "   vstrb.8                 q2, [%[pTarget]], #16               \n"
        "   letp                    lr, 2b                              \n"
        "   1:                                                          \n"
        : [pSource] "+r"(phwSource), [pTarget] "+r"(pwTarget),
        [pRecolor] "+r"(recolor)
        : [loopCnt] "r"(blkCnt), [opa] "r"(opa)
        : "q0", "q1", "q2", "q3", "lr", "memory");
  } else {
    __asm volatile(
        "   .p2align 2                                                  \n"
        "   vdup.32                 q0, %[pSource]                      \n"
        "   vdup.8                  q1, %[opa]                          \n"
        "   vrmulh.u8               q0, q0, q1                          \n"
        "   mov                     r0, #0                              \n"
        "   vidup.u32               q1, r0, #1                          \n"
        "   cmp                     %[size], #16                        \n"
        "   itte                    eq                                  \n"
        "   moveq                   r1, #0x11                           \n"
        "   moveq                   r0, #0x44                           \n"
        "   movne                   r1, #0x1                            \n"
        "   vsli.32                 q1, q1, #8                          \n"
        "   vsli.32                 q1, q1, #16                         \n"
        "   vmul.i8                 q1, q1, r1                          \n"
        "   1:                                                          \n"
        "   wlstp.8                 lr, %[loopCnt], 3f                  \n"
        "   2:                                                          \n"
        "   vrmulh.u8               q2, q0, q1                          \n"
        "   vsli.32                 q2, q1, #24                         \n"
        "   vstrb.8                 q2, [%[pTarget]], #16               \n"
        "   vadd.i8                 q1, q1, r0                          \n"
        "   letp                    lr, 2b                              \n"
        "   3:                                                          \n"
        : [pSource] "+r"(recolor), [pTarget] "+r"(pwTarget)
        : [loopCnt] "r"(blkCnt), [opa] "r"(opa), [size] "r"(size)
        : "r0", "r1", "q0", "q1", "q2", "lr", "memory");
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
LV_ATTRIBUTE_FAST_MEM void gpu_set_area(const lv_area_t* area)
{
  if (!area) {
    gpu_area.y1 = INT16_MIN;
    return;
  }
  lv_area_copy(&gpu_area, area);
}

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
LV_ATTRIBUTE_FAST_MEM void gpu_wait_area(const lv_area_t* area)
{
  if (gpu_area.x1 == INT16_MIN) {
    return;
  }
  lv_area_t tmp_area;
  if (!area || gpu_area.y1 == INT16_MIN
      || _lv_area_intersect(&tmp_area, &gpu_area, area)) {
    vg_lite_finish();
    gpu_area.x1 = INT16_MIN;
    return;
  }
}

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
 *
 * @return None
 *
 ****************************************************************************/
void convert_argb8565_to_8888(uint8_t* px_buf, uint32_t buf_stride,
    const uint8_t* px_map, uint32_t map_stride, lv_img_header_t* header)
{
#ifdef CONFIG_ARM_HAVE_MVE
  int32_t blkCnt;
  const uint32_t _maskA[4] = { 0xff000000, 0xff0000, 0xff00, 0xff };
  const uint32_t _maskRB[4] = { 0xf8000000, 0xf80000, 0xf800, 0xf8 };
  const uint32_t _maskG[4] = { 0xfc000000, 0xfc0000, 0xfc00, 0xfc };
  const uint32_t _shiftC[4] = { 0x1, 0x100, 0x10000, 0x1000000 };

  for (int_fast16_t y = 0; y < header->h; y++) {
    const uint8_t* phwSource = px_map;
    uint32_t* pwTarget = (uint32_t*)px_buf;

    blkCnt = header->w;
    while (!IS_ALIGNED(phwSource, 4)) {
      lv_color32_t* c32 = (lv_color32_t*)pwTarget;
      const lv_color16_t* c16 = (const lv_color16_t*)phwSource;
      c32->ch.red = c16->ch.red << 3 | c16->ch.red >> 2;
      c32->ch.green = c16->ch.green << 2 | c16->ch.green >> 4;
      c32->ch.blue = c16->ch.blue << 3 | c16->ch.blue >> 2;
      c32->ch.alpha = phwSource[LV_IMG_PX_SIZE_ALPHA_BYTE - 1];
      phwSource += 3;
      pwTarget++;
      blkCnt--;
    }
/* (disabled due to intrinsics being much slower than hand-written asm) */
// #define USE_MVE_INTRINSICS
#ifdef USE_MVE_INTRINSICS
    uint32x4_t maskA = vldrwq_u32(_maskA);
    uint32x4_t maskRB = vldrwq_u32(_maskRB);
    uint32x4_t maskG = vldrwq_u32(_maskG);
    uint32x4_t shiftC = vldrwq_u32(_shiftC);
    do {
      mve_pred16_t tailPred = vctp32q(blkCnt);

      /* load a vector of 4 argb8565 pixels
       * (residuals are processed in the next loop) */
      uint32x4_t vecIn = vld1q_z_u32((const uint32_t*)phwSource, tailPred);
      /* extract individual channels and place them in high 8bits
       * (P=GlB, Q=RGh) */

      uint32_t carry = 0;
      vecIn = vshlcq(vecIn, &carry, 8); /* |***A|QPAQ|PAQP|AQP0| */
      uint32x4_t vecA = vandq(vecIn, maskA); /* |000A|00A0|0A00|A000| */
      vecA = vmulq(vecA, shiftC); /* |A000|A000|A000|A000| */
      vecIn = vshlcq(vecIn, &carry, 8); /* |**AQ|PAQP|AQPA|QP**| */
      uint32x4_t vecR = vandq(vecIn, maskRB); /* |000R|00R0|0R00|R000| */
      vecR = vmulq(vecR, shiftC); /* |R000|R000|R000|R000| */
      vecIn = vshlcq(vecIn, &carry, 5); /* Similar operation on G channel */
      uint32x4_t vecG = vandq(vecIn, maskG);
      vecG = vmulq(vecG, shiftC);
      vecIn = vshlcq(vecIn, &carry, 6);
      uint32x4_t vecB = vandq(vecIn, maskRB);
      vecB = vmulq(vecB, shiftC);
      /* pre-multiply alpha to all channels */
      vecR = vmulhq(vecR, vecA);
      vecG = vmulhq(vecG, vecA);
      vecB = vmulhq(vecB, vecA);
      /* merge channels */
      vecG = vsriq(vecG, vecB, 8);
      vecR = vsriq(vecR, vecG, 8);
      vecA = vsriq(vecA, vecR, 8);
      /* store a vector of 4 bgra8888 pixels */
      vst1q_p(pwTarget, vecA, tailPred);
      phwSource += 6;
      pwTarget += 4;
      blkCnt -= 4;
    } while (blkCnt > 0);
#else
    __asm volatile(
        "   .p2align 2                                                  \n"
        "   wlstp.32                lr, %[loopCnt], 1f                  \n"
        "   2:                                                          \n"
        /* load a vector of 4 argb8565 pixels */
        "   vldrw.32                q0, [%[pSource]], #12               \n"
        /* q0 => |****|AQPA|QPAQ|PAQP| */
        "   vshlc                   q0, r0, #8                          \n"
        /* q0 => |***A|QPAQ|PAQP|AQP0| */
        "   vldrw.32                q1, [%[maskA]]                      \n"
        "   vand                    q2, q0, q1                          \n"
        /* q2 => |000A|00A0|0A00|A000| */
        "   vldrw.32                q4, [%[shiftC]]                     \n"
        "   vmul.i32                q1, q2, q4                          \n"
        /* q1 => |A000|A000|A000|A000|, use q1 as final output */
        "   vshlc                   q0, r0, #8                          \n"
        /* q0 => |**AQ|PAQP|AQPA|QP**| */
        "   vldrw.32                q3, [%[maskRB]]                     \n"
        "   vand                    q2, q0, q3                          \n"
        /* q2 => |000r|00r0|0r00|r000| */
        "   vmul.i32                q3, q2, q4                          \n"
        /* q3 => |r000|r000|r000|r000| */
        "   vsri.32                 q1, q3, #8                          \n"
        /* q1 => |Ar00|Ar00|Ar00|Ar00| */
        "   vsri.32                 q1, q3, #13                         \n"
        /* q1 => |AR*0|AR*0|AR*0|AR*0| */
        "   vshlc                   q0, r0, #5                          \n"
        /* Similar operation on G channel */
        "   vldrw.32                q3, [%[maskG]]                      \n"
        "   vand                    q2, q0, q3                          \n"
        /* q2 => |000g|00g0|0g00|g000| */
        "   vmul.i32                q3, q2, q4                          \n"
        /* q3 => |g000|g000|g000|g000| */
        "   vsri.32                 q1, q3, #16                         \n"
        /* q1 => |ARg0|ARg0|ARg0|ARg0| */
        "   vsri.32                 q1, q3, #22                         \n"
        /* q1 => |ARG*|ARG*|ARG*|ARG*| */
        "   vshlc                   q0, r0, #6                          \n"
        /* Similar operation on B channel */
        "   vldrw.32                q3, [%[maskRB]]                     \n"
        "   vand                    q2, q0, q3                          \n"
        /* q2 => |000b|00b0|0b00|b000| */
        "   vmul.i32                q3, q2, q4                          \n"
        /* q3 => |b000|b000|b000|b000| */
        "   vsri.32                 q1, q3, #24                         \n"
        /* q1 => |ARGb|ARGb|ARGb|ARGb| */
        "   vsri.32                 q1, q3, #29                         \n"
        /* q1 => |ARGB|ARGB|ARGB|ARGB| */
        "   vsri.32                 q3, q1, #8                          \n"
        "   vsri.32                 q3, q1, #16                         \n"
        "   vsri.32                 q3, q1, #24                         \n"
        /* pre-multiply alpha to all channels */
        "   vrmulh.u8               q2, q1, q3                          \n"
        "   vsli.32                 q2, q3, #24                         \n"
        /* store a vector of 4 bgra8888 pixels */
        "   vstrw.32                q2, [%[pTarget]], #16               \n"
        "   letp                    lr, 2b                              \n"
        "   1:                                                          \n"

        : [pSource] "+r"(phwSource), [pTarget] "+r"(pwTarget)
        : [loopCnt] "r"(blkCnt), [maskA] "r"(_maskA), [shiftC] "r"(_shiftC),
        [maskRB] "r"(_maskRB), [maskG] "r"(_maskG)
        : "q0", "q1", "q2", "q3", "q4", "r0", "lr", "memory");
#endif
    px_map += map_stride;
    px_buf += buf_stride;
  }
#else
  lv_opa_t opa = recolor.ch.alpha;
  lv_opa_t mix = 255 - opa;
  uint32_t recolor_premult[3];
  if (opa != LV_OPA_TRANSP) {
    recolor_premult[0] = recolor.ch.red * opa;
    recolor_premult[1] = recolor.ch.green * opa;
    recolor_premult[2] = recolor.ch.blue * opa;
  }
  for (int_fast16_t i = 0; i < header->h; i++) {
    for (int_fast16_t j = 0; j < header->w; j++) {
      lv_color32_t* c32 = (lv_color32_t*)px_buf + j;
      lv_color16_t* c16 = (const lv_color16_t*)px_map;
      c32->ch.alpha = px_map[LV_IMG_PX_SIZE_ALPHA_BYTE - 1];
      c32->ch.red = (c16->ch.red * 263 + 7) * c32->ch.alpha >> 13;
      c32->ch.green = (c16->ch.green * 259 + 3) * c32->ch.alpha >> 14;
      c32->ch.blue = (c16->ch.blue * 263 + 7) * c32->ch.alpha >> 13;
      if (opa != LV_OPA_TRANSP) {
        c32->ch.red = LV_UDIV255(c32->ch.red * mix + recolor_premult[0]);
        c32->ch.green = LV_UDIV255(c32->ch.green * mix + recolor_premult[1]);
        c32->ch.blue = LV_UDIV255(c32->ch.blue * mix + recolor_premult[2]);
      }
      px_map += LV_IMG_PX_SIZE_ALPHA_BYTE;
    }
    lv_memset_00(px_buf + (header->w << 2), buf_stride - (header->w << 2));
    px_buf += buf_stride;
  }
#endif
}

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
    lv_color32_t recolor, uint32_t ckey)
{
  lv_opa_t opa = recolor.ch.alpha;
  lv_opa_t mix = 255 - opa;
  uint32_t recolor_premult[3] = {
    recolor.ch.red * opa, recolor.ch.green * opa, recolor.ch.blue * opa
  };
  if (ckey == 0) {
    if (opa == LV_OPA_TRANSP) {
      if (map_stride == buf_stride) {
        lv_memcpy(px_buf, px_map, buf_stride * header->h);
      } else {
        for (int_fast16_t i = 0; i < header->h; i++) {
          lv_memcpy(px_buf, px_map, map_stride);
        }
        lv_memset_00(px_buf + map_stride, buf_stride - map_stride);
        px_map += map_stride;
        px_buf += buf_stride;
      }
    } else {
      for (int_fast16_t i = 0; i < header->h; i++) {
        const lv_color_t* src = (const lv_color_t*)px_map;
        lv_color_t* dst = (lv_color_t*)px_buf;
        for (int_fast16_t j = 0; j < header->w; j++) {
          dst[j].ch.red = LV_UDIV255(src[j].ch.red * mix + (recolor_premult[0] >> 3));
          dst[j].ch.green = LV_UDIV255(src[j].ch.green * mix + (recolor_premult[1] >> 2));
          dst[j].ch.blue = LV_UDIV255(src[j].ch.blue * mix + (recolor_premult[2] >> 3));
        }
        lv_memset_00(px_buf + map_stride, buf_stride - map_stride);
        px_map += map_stride;
        px_buf += buf_stride;
      }
    }
  } else if (opa == LV_OPA_TRANSP) {
    /* chroma keyed, target is argb32 */
    for (int_fast16_t i = 0; i < header->h; i++) {
      const lv_color_t* src = (const lv_color_t*)px_map;
      lv_color32_t* dst = (lv_color32_t*)px_buf;
      for (int_fast16_t j = 0; j < header->w; j++) {
        dst[j].full = (src[j].full == ckey) ? 0 : lv_color_to32(src[j]);
      }
      lv_memset_00(px_buf + map_stride, buf_stride - map_stride);
      px_map += map_stride;
      px_buf += buf_stride;
    }
  } else {
    for (int_fast16_t i = 0; i < header->h; i++) {
      const lv_color_t* src = (const lv_color_t*)px_map;
      lv_color32_t* dst = (lv_color32_t*)px_buf;
      for (int_fast16_t j = 0; j < header->w; j++) {
        if (src[j].full == ckey) {
          dst[j].full = 0;
        } else {
          dst[j].ch.alpha = 0xFF;
          dst[j].ch.red = LV_UDIV255(((src[j].ch.red * 263 + 7) >> 5) * mix + recolor_premult[0]);
          dst[j].ch.green = LV_UDIV255(((src[j].ch.green * 259 + 3) >> 6) * mix + recolor_premult[1]);
          dst[j].ch.blue = LV_UDIV255(((src[j].ch.blue * 263 + 7) >> 5) * mix + recolor_premult[2]);
        }
        lv_memset_00(px_buf + map_stride, buf_stride - map_stride);
        px_map += map_stride;
        px_buf += buf_stride;
      }
    }
  }
}

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
    lv_color32_t recolor, uint32_t ckey)
{
#ifdef CONFIG_ARM_HAVE_MVE
  uint32_t ww = ALIGN_UP(header->w, 4) << 2;
  int32_t map_offset = map_stride - ww;
  int32_t buf_offset = buf_stride - ww;
  if (recolor.ch.alpha != LV_OPA_TRANSP) {
    if (ckey != 0) {
      /* recolor && chroma keyed */
      __asm volatile(
          "   .p2align 2                                                  \n"
          "   movs                    r0, %[recolor], lsr #24             \n"
          "   vdup.32                 q0, %[recolor]                      \n"
          "   vdup.8                  q1, r0                              \n"
          "   vrmulh.u8               q0, q0, q1                          \n" /* q0 = recolor_premult */
          "   vmvn                    q1, q1                              \n"
          "   vmov.i32                q3, #0                              \n"
          "   1:                                                          \n"
          "   wlstp.8                 lr, %[w], 3f                        \n"
          "   2:                                                          \n"
          "   vldrb.8                 q2, [%[pSource]], #16               \n"
          "   vorr.i32                q2, #0xFF000000                     \n"
          "   vcmp.i32                eq, q2, %[ckey]                     \n"
          "   vrmulh.u8               q2, q2, q1                          \n"
          "   vadd.i8                 q2, q2, q0                          \n"
          "   vorr.i32                q2, #0xFF000000                     \n" /* set alpha to 0xFF */
          "   vpst                                                        \n"
          "   vmovt.i32               q2, #0                              \n"
          "   vstrb.8                 q2, [%[pTarget]], #16               \n"
          "   letp                    lr, 2b                              \n"
          "   3:                                                          \n"
          "   wlstp.8                 lr, %[dst_offset], 5f               \n"
          "   4:                                                          \n"
          "   vstrb.8                 q3, [%[pTarget]], #16               \n"
          "   letp                    lr, 4b                              \n"
          "   5:                                                          \n"
          "   adds                    %[pSource], %[src_offset]           \n"
          "   subs                    %[h], #1                            \n"
          "   bne                     1b                                  \n"
          : [pSource] "+r"(px_map), [pTarget] "+r"(px_buf), [h] "+r"(header->h)
          : [w] "r"(header->w << 2), [recolor] "r"(recolor), [ckey] "r"(ckey),
          [src_offset] "r"(map_offset), [dst_offset] "r"(buf_offset)
          : "q0", "q1", "q2", "q3", "r0", "lr", "memory");
    } else {
      /* recolor, no chroma key */
      __asm volatile(
          "   .p2align 2                                                  \n"
          "   movs                    r0, %[recolor], lsr #24             \n"
          "   vdup.32                 q0, %[recolor]                      \n"
          "   vdup.8                  q1, r0                              \n"
          "   vrmulh.u8               q0, q0, q1                          \n" /* q0 = recolor_premult */
          "   vmvn                    q1, q1                              \n" /* q1 = ~recolor_opa */
          "   vmov.i32                q3, #0                              \n"
          "   1:                                                          \n"
          "   wlstp.8                 lr, %[w], 3f                        \n"
          "   2:                                                          \n"
          "   vldrb.8                 q2, [%[pSource]], #16               \n"
          "   vrmulh.u8               q2, q2, q1                          \n"
          "   vadd.i8                 q2, q2, q0                          \n"
          "   vorr.i32                q2, #0xFF000000                     \n" /* set alpha to 0xFF */
          "   vstrb.8                 q2, [%[pTarget]], #16               \n"
          "   letp                    lr, 2b                              \n"
          "   3:                                                          \n"
          "   wlstp.8                 lr, %[dst_offset], 5f               \n"
          "   4:                                                          \n"
          "   vstrb.8                 q3, [%[pTarget]], #16               \n"
          "   letp                    lr, 4b                              \n"
          "   5:                                                          \n"
          "   adds                    %[pSource], %[src_offset]           \n"
          "   subs                    %[h], #1                            \n"
          "   bne                     1b                                  \n"
          : [pSource] "+r"(px_map), [pTarget] "+r"(px_buf), [h] "+r"(header->h)
          : [w] "r"(header->w << 2), [recolor] "r"(recolor),
          [src_offset] "r"(map_offset), [dst_offset] "r"(buf_offset)
          : "q0", "q1", "q2", "q3", "r0", "lr", "memory");
    }
  } else if (ckey != 0) {
    /* no recolor, chroma keyed */
    __asm volatile(
        "   .p2align 2                                                  \n"
        "   vmov.i32                q1, #0                              \n"
        "   1:                                                          \n"
        "   wlstp.8                 lr, %[w], 3f                        \n"
        "   2:                                                          \n"
        "   vldrb.8                 q0, [%[pSource]], #16               \n"
        "   vorr.i32                q0, #0xFF000000                     \n"
        "   vcmp.i32                eq, q0, %[ckey]                     \n"
        "   vpst                                                        \n"
        "   vmovt.i32               q0, #0                              \n"
        "   vstrb.8                 q0, [%[pTarget]], #16               \n"
        "   letp                    lr, 2b                              \n"
        "   3:                                                          \n"
        "   wlstp.8                 lr, %[dst_offset], 5f               \n"
        "   4:                                                          \n"
        "   vstrb.8                 q1, [%[pTarget]], #16               \n"
        "   letp                    lr, 4b                              \n"
        "   5:                                                          \n"
        "   adds                    %[pSource], %[src_offset]           \n"
        "   subs                    %[h], #1                            \n"
        "   bne                     1b                                  \n"
        : [pSource] "+r"(px_map), [pTarget] "+r"(px_buf), [h] "+r"(header->h)
        : [w] "r"(header->w << 2), [ckey] "r"(ckey),
        [src_offset] "r"(map_offset), [dst_offset] "r"(buf_offset)
        : "q0", "q1", "lr", "memory");
  } else if (map_offset || buf_offset) {
    /* no recolor, no chroma key */
    __asm volatile(
        "   .p2align 2                                                  \n"
        "   vmov.i32                q1, #0                              \n"
        "   1:                                                          \n"
        "   wlstp.8                 lr, %[w], 3f                        \n"
        "   2:                                                          \n"
        "   vldrb.8                 q0, [%[pSource]], #16               \n"
        "   vorr.i32                q0, #0xFF000000                     \n" /* set alpha to 0xFF */
        "   vstrb.8                 q0, [%[pTarget]], #16               \n"
        "   letp                    lr, 2b                              \n"
        "   3:                                                          \n"
        "   wlstp.8                 lr, %[dst_offset], 5f               \n"
        "   4:                                                          \n"
        "   vstrb.8                 q1, [%[pTarget]], #16               \n"
        "   letp                    lr, 4b                              \n"
        "   5:                                                          \n"
        "   adds                    %[pSource], %[src_offset]           \n"
        "   subs                    %[h], #1                            \n"
        "   bne                     1b                                  \n"
        : [pSource] "+r"(px_map), [pTarget] "+r"(px_buf), [h] "+r"(header->h)
        : [w] "r"(header->w << 2), [src_offset] "r"(map_offset),
        [dst_offset] "r"(buf_offset)
        : "q0", "q1", "lr", "memory");
  } else {
    /* map_stride and buf_stride matches width, just copy 'em all */
    __asm volatile(
        "   .p2align 2                                                  \n"
        "   wlstp.8                 lr, %[size], 2f                     \n"
        "   1:                                                          \n"
        "   vldrb.8                 q0, [%[pSource]], #16               \n"
        "   vorr.i32                q0, #0xFF000000                     \n" /* set alpha to 0xFF */
        "   vstrb.8                 q0, [%[pTarget]], #16               \n"
        "   letp                    lr, 1b                              \n"
        "   2:                                                          \n"
        : [pSource] "+r"(px_map), [pTarget] "+r"(px_buf)
        : [size] "r"(ww * header->h)
        : "q0", "lr", "memory");
  }
#else
  lv_opa_t opa = recolor.ch.alpha;
  lv_opa_t mix = 255 - opa;
  uint32_t recolor_premult[3] = {
    recolor.ch.red * opa, recolor.ch.green * opa, recolor.ch.blue * opa
  };
  for (int_fast16_t i = 0; i < header->h; i++) {
    const lv_color32_t* src = (const lv_color32_t*)px_map;
    lv_color32_t* dst = (lv_color32_t*)px_buf;
    for (int_fast16_t j = 0; j < header->w; j++) {
      if (src[j].full == ckey) {
        dst[j].full = 0;
      } else {
        dst[j].ch.alpha = 0xFF;
        dst[j].ch.red = LV_UDIV255(src[j].ch.red * mix + recolor_premult[0]);
        dst[j].ch.green = LV_UDIV255(src[j].ch.green * mix + recolor_premult[1]);
        dst[j].ch.blue = LV_UDIV255(src[j].ch.blue * mix + recolor_premult[2]);
      }
    }
    lv_memset_00(px_buf + map_stride, buf_stride - map_stride);
    px_map += map_stride;
    px_buf += buf_stride;
  }
#endif
}

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
    lv_color32_t recolor, bool preprocessed)
{
#ifdef CONFIG_ARM_HAVE_MVE
  uint32_t ww = ALIGN_UP(header->w, 4) << 2;
  int32_t map_offset = map_stride - ww;
  int32_t buf_offset = buf_stride - ww;
  if (recolor.ch.alpha != LV_OPA_TRANSP) {
    if (!preprocessed) {
      /* recolor && need premult */
      __asm volatile(
          "   .p2align 2                                                  \n"
          "   movs                    r0, %[recolor], lsr #24             \n"
          "   vdup.32                 q0, %[recolor]                      \n"
          "   vdup.8                  q1, r0                              \n"
          "   vrmulh.u8               q0, q0, q1                          \n" /* q0 = recolor_premult */
          "   vmvn                    q1, q1                              \n" /* q1 = ~recolor_opa */
          "   1:                                                          \n"
          "   wlstp.8                 lr, %[w], 3f                        \n"
          "   2:                                                          \n"
          "   vldrb.8                 q2, [%[pSource]], #16               \n"
          "   vsri.32                 q3, q2, #8                          \n"
          "   vsri.32                 q3, q2, #16                         \n"
          "   vsri.32                 q3, q2, #24                         \n"
          "   vrmulh.u8               q2, q2, q1                          \n"
          "   vadd.i8                 q2, q2, q0                          \n"
          "   vrmulh.u8               q2, q2, q3                          \n"
          "   vsli.32                 q2, q3, #24                         \n"
          "   vstrb.8                 q2, [%[pTarget]], #16               \n"
          "   letp                    lr, 2b                              \n"
          "   3:                                                          \n"
          "   vmov.i32                q3, #0                              \n"
          "   wlstp.8                 lr, %[dst_offset], 5f               \n"
          "   4:                                                          \n"
          "   vstrb.8                 q3, [%[pTarget]], #16               \n"
          "   letp                    lr, 4b                              \n"
          "   5:                                                          \n"
          "   adds                    %[pSource], %[src_offset]           \n"
          "   subs                    %[h], #1                            \n"
          "   bne                     1b                                  \n"
          : [pSource] "+r"(px_map), [pTarget] "+r"(px_buf), [h] "+r"(header->h)
          : [w] "r"(header->w << 2), [recolor] "r"(recolor),
          [src_offset] "r"(map_offset), [dst_offset] "r"(buf_offset)
          : "q0", "q1", "q2", "q3", "r0", "lr", "memory");
    } else {
      /* recolor, need not premult */
      __asm volatile(
          "   .p2align 2                                                  \n"
          "   movs                    r0, %[recolor], lsr #24             \n"
          "   vdup.32                 q0, %[recolor]                      \n"
          "   vdup.8                  q1, r0                              \n"
          "   vrmulh.u8               q0, q0, q1                          \n" /* q0 = recolor_premult */
          "   vmvn                    q1, q1                              \n" /* q1 = ~recolor_opa */
          "   1:                                                          \n"
          "   wlstp.8                 lr, %[w], 3f                        \n"
          "   2:                                                          \n"
          "   vldrb.8                 q2, [%[pSource]], #16               \n"
          "   vsri.32                 q3, q2, #8                          \n"
          "   vsri.32                 q3, q2, #16                         \n"
          "   vsri.32                 q3, q2, #24                         \n"
          "   vrmulh.u8               q2, q2, q1                          \n"
          "   vrmulh.u8               q4, q0, q3                          \n"
          "   vadd.i8                 q2, q2, q4                          \n"
          "   vsli.32                 q2, q3, #24                         \n"
          "   vstrb.8                 q2, [%[pTarget]], #16               \n"
          "   letp                    lr, 2b                              \n"
          "   3:                                                          \n"
          "   vmov.i32                q3, #0                              \n"
          "   wlstp.8                 lr, %[dst_offset], 5f               \n"
          "   4:                                                          \n"
          "   vstrb.8                 q3, [%[pTarget]], #16               \n"
          "   letp                    lr, 4b                              \n"
          "   5:                                                          \n"
          "   adds                    %[pSource], %[src_offset]           \n"
          "   subs                    %[h], #1                            \n"
          "   bne                     1b                                  \n"
          : [pSource] "+r"(px_map), [pTarget] "+r"(px_buf), [h] "+r"(header->h)
          : [w] "r"(header->w << 2), [recolor] "r"(recolor),
          [src_offset] "r"(map_offset), [dst_offset] "r"(buf_offset)
          : "q0", "q1", "q2", "q3", "q4", "r0", "lr", "memory");
    }
  } else if (!preprocessed) {
    /* no recolor, need premult */
    __asm volatile(
        "   .p2align 2                                                  \n"
        "   vmov.i32                q2, #0                              \n"
        "   1:                                                          \n"
        "   wlstp.8                 lr, %[w], 3f                        \n"
        "   2:                                                          \n"
        "   vldrb.8                 q0, [%[pSource]], #16               \n"
        "   vsri.32                 q1, q0, #8                          \n"
        "   vsri.32                 q1, q0, #16                         \n"
        "   vsri.32                 q1, q0, #24                         \n"
        "   vrmulh.u8               q0, q0, q1                          \n"
        "   vsli.32                 q0, q1, #24                         \n"
        "   vstrb.8                 q0, [%[pTarget]], #16               \n"
        "   letp                    lr, 2b                              \n"
        "   3:                                                          \n"
        "   wlstp.8                 lr, %[dst_offset], 5f               \n"
        "   4:                                                          \n"
        "   vstrb.8                 q2, [%[pTarget]], #16               \n"
        "   letp                    lr, 4b                              \n"
        "   5:                                                          \n"
        "   adds                    %[pSource], %[src_offset]           \n"
        "   subs                    %[h], #1                            \n"
        "   bne                     1b                                  \n"
        : [pSource] "+r"(px_map), [pTarget] "+r"(px_buf), [h] "+r"(header->h)
        : [w] "r"(header->w << 2), [src_offset] "r"(map_offset),
        [dst_offset] "r"(buf_offset)
        : "q0", "q1", "q2", "lr", "memory");
  } else if (map_offset || buf_offset) {
    /* no recolor, need not premult */
    __asm volatile(
        "   .p2align 2                                                  \n"
        "   vmov.i32                q1, #0                              \n"
        "   1:                                                          \n"
        "   wlstp.8                 lr, %[w], 3f                        \n"
        "   2:                                                          \n"
        "   vldrb.8                 q0, [%[pSource]], #16               \n"
        "   vstrb.8                 q0, [%[pTarget]], #16               \n"
        "   letp                    lr, 2b                              \n"
        "   3:                                                          \n"
        "   wlstp.8                 lr, %[dst_offset], 5f               \n"
        "   4:                                                          \n"
        "   vstrb.8                 q1, [%[pTarget]], #16               \n"
        "   letp                    lr, 4b                              \n"
        "   5:                                                          \n"
        "   adds                    %[pSource], %[src_offset]           \n"
        "   subs                    %[h], #1                            \n"
        "   bne                     1b                                  \n"
        : [pSource] "+r"(px_map), [pTarget] "+r"(px_buf), [h] "+r"(header->h)
        : [w] "r"(header->w << 2), [src_offset] "r"(map_offset),
        [dst_offset] "r"(buf_offset)
        : "q0", "q1", "lr", "memory");
  } else {
    /* map_stride and buf_stride matches width, just copy 'em all */
    __asm volatile(
        "   .p2align 2                                                  \n"
        "   wlstp.8                 lr, %[size], 2f                     \n"
        "   1:                                                          \n"
        "   vldrb.8                 q0, [%[pSource]], #16               \n"
        "   vstrb.8                 q0, [%[pTarget]], #16               \n"
        "   letp                    lr, 1b                              \n"
        "   2:                                                          \n"
        : [pSource] "+r"(px_map), [pTarget] "+r"(px_buf)
        : [size] "r"(ww * header->h)
        : "q0", "lr", "memory");
  }
#else
  lv_opa_t opa = recolor.ch.alpha;
  lv_opa_t mix = 255 - opa;
  uint32_t recolor_premult[3];
  if (opa != LV_OPA_TRANSP) {
    recolor_premult[0] = recolor.ch.red * opa;
    recolor_premult[1] = recolor.ch.green * opa;
    recolor_premult[2] = recolor.ch.blue * opa;
  }
  if (!preprocessed) {
    if (opa != LV_OPA_TRANSP) {
      for (int_fast16_t i = 0; i < header->h; i++) {
        const lv_color32_t* src = (const lv_color32_t*)px_map;
        lv_color32_t* dst = (lv_color32_t*)px_buf;
        for (int_fast16_t j = 0; j < header->w; j++) {
          if (src[j].ch.alpha == 0) {
            dst[j].full = 0;
            continue;
          }
          dst[j].ch.alpha = src[j].ch.alpha;
          dst[j].ch.red = LV_UDIV255(src[j].ch.red * mix + recolor_premult[0]);
          dst[j].ch.green = LV_UDIV255(src[j].ch.green * mix + recolor_premult[1]);
          dst[j].ch.blue = LV_UDIV255(src[j].ch.blue * mix + recolor_premult[2]);
          if (src[j].ch.alpha != 0xFF) {
            dst[j].ch.red = LV_UDIV255(dst[j].ch.red * src[j].ch.alpha);
            dst[j].ch.green = LV_UDIV255(dst[j].ch.green * src[j].ch.alpha);
            dst[j].ch.blue = LV_UDIV255(dst[j].ch.blue * src[j].ch.alpha);
          }
        }
        lv_memset_00(px_buf + map_stride, buf_stride - map_stride);
        px_map += map_stride;
        px_buf += buf_stride;
      }
    } else {
      for (int_fast16_t i = 0; i < header->h; i++) {
        const lv_color32_t* src = (const lv_color32_t*)px_map;
        lv_color32_t* dst = (lv_color32_t*)px_buf;
        for (int_fast16_t j = 0; j < header->w; j++) {
          if (src[j].ch.alpha == 0) {
            dst[j].full = 0;
          } else if (src[j].ch.alpha < 0xFF) {
            dst[j].ch.alpha = src[j].ch.alpha;
            dst[j].ch.red = LV_UDIV255(src[j].ch.red * src[j].ch.alpha);
            dst[j].ch.green = LV_UDIV255(src[j].ch.green * src[j].ch.alpha);
            dst[j].ch.blue = LV_UDIV255(src[j].ch.blue * src[j].ch.alpha);
          } else {
            dst[j] = src[j];
          }
        }
        lv_memset_00(px_buf + map_stride, buf_stride - map_stride);
        px_map += map_stride;
        px_buf += buf_stride;
      }
    }
  } else if (opa != LV_OPA_TRANSP) {
    /* have been pre-multiplied */
    for (int_fast16_t i = 0; i < header->h; i++) {
      const lv_color32_t* src = (const lv_color32_t*)px_map;
      lv_color32_t* dst = (lv_color32_t*)px_buf;
      for (int_fast16_t j = 0; j < header->w; j++) {
        if (src[j].ch.alpha == 0) {
          dst[j].full = 0;
          continue;
        }
        dst[j].ch.alpha = src[j].ch.alpha;
        if (src[j].ch.alpha < 0xFF) {
          dst[j].ch.red = LV_UDIV255(src[j].ch.red * mix + LV_UDIV255(recolor_premult[0]) * src[j].ch.alpha);
          dst[j].ch.green = LV_UDIV255(src[j].ch.green * mix + LV_UDIV255(recolor_premult[1]) * src[j].ch.alpha);
          dst[j].ch.blue = LV_UDIV255(src[j].ch.blue * mix + LV_UDIV255(recolor_premult[2]) * src[j].ch.alpha);
        } else {
          dst[j].ch.red = LV_UDIV255(src[j].ch.red * mix + recolor_premult[0]);
          dst[j].ch.green = LV_UDIV255(src[j].ch.green * mix + recolor_premult[1]);
          dst[j].ch.blue = LV_UDIV255(src[j].ch.blue * mix + recolor_premult[2]);
        }
      }
      lv_memset_00(px_buf + map_stride, buf_stride - map_stride);
      px_map += map_stride;
      px_buf += buf_stride;
    }
  } else if (map_stride != buf_stride) {
    for (int_fast16_t i = 0; i < header->h; i++) {
      lv_memcpy(px_buf, px_map, header->w << 2);
      lv_memset_00(px_buf + map_stride, buf_stride - map_stride);
      px_map += map_stride;
      px_buf += buf_stride;
    }
  } else {
    lv_memcpy(px_buf, px_map, header->h * map_stride);
  }
#endif
}

void convert_indexed8_to_argb8888(uint8_t* px_buf, uint32_t buf_stride,
    const uint8_t* px_map, uint32_t map_stride, const uint32_t* palette,
    lv_img_header_t* header)
{
#ifdef CONFIG_ARM_HAVE_MVE
  int32_t map_offset = map_stride - header->w;
  int32_t buf_offset = buf_stride - (header->w << 2);
  if (map_offset || buf_offset) {
    __asm volatile(
        "   .p2align 2                                                  \n"
        "   vmov.i32                q2, #0                              \n"
        "   1:                                                          \n"
        "   wlstp.32                lr, %[w], 3f                        \n"
        "   2:                                                          \n"
        "   vldrb.u32               q0, [%[pSource]], #4                \n"
        "   vldrw.u32               q1, [%[palette], q0, uxtw #2]       \n"
        "   vstrb.8                 q1, [%[pTarget]], #16               \n"
        "   letp                    lr, 2b                              \n"
        "   3:                                                          \n"
        "   wlstp.8                 lr, %[dst_offset], 5f               \n"
        "   4:                                                          \n"
        "   vstrb.8                 q2, [%[pTarget]], #16               \n"
        "   letp                    lr, 4b                              \n"
        "   5:                                                          \n"
        "   adds                    %[pSource], %[src_offset]           \n"
        "   subs                    %[h], #1                            \n"
        "   bne                     1b                                  \n"
        : [pSource] "+r"(px_map), [pTarget] "+r"(px_buf), [h] "+r"(header->h)
        : [w] "r"(header->w), [src_offset] "r"(map_offset),
        [dst_offset] "r"(buf_offset), [palette] "r"(palette)
        : "q0", "q1", "q2", "lr", "memory");
  } else {
    __asm volatile(
        "   .p2align 2                                                  \n"
        "   wlstp.32                lr, %[size], 2f                     \n"
        "   1:                                                          \n"
        "   vldrb.u32               q0, [%[pSource]], #4                \n"
        "   vldrw.u32               q1, [%[palette], q0, uxtw #2]       \n"
        "   vstrb.8                 q1, [%[pTarget]], #16               \n"
        "   letp                    lr, 1b                              \n"
        "   2:                                                          \n"
        : [pSource] "+r"(px_map), [pTarget] "+r"(px_buf)
        : [size] "r"(header->w * header->h), [palette] "r"(palette)
        : "q0", "q1", "lr", "memory");
  }
#else
  for (int i = 0; i < header->h; i++) {
    uint32_t* dst = (uint32_t*)px_buf;
    for (int j = 0; j < header->w; j++) {
      dst[j] = palette[px_map[j]];
    }
    lv_memset_00(px_buf + (map_stride << 2), buf_stride - (map_stride << 2));
    px_map += map_stride;
    px_buf += buf_stride;
  }
#endif
}

lv_res_t pre_zoom_gaussian_filter(uint8_t* dst, const uint8_t* src,
    lv_img_header_t* header, const char* ext)
{
  lv_img_cf_t cf = header->cf;
  uint32_t stride = header->w;
  lv_area_t area = { 0, 0, header->w - 1, header->h - 1 };
  bool gpu_format = (strcmp(ext, "gpu") == 0);
  if (gpu_format) {
    memcpy(dst, src, sizeof(gpu_data_header_t));
    gpu_data_header_t* gpu_header = (gpu_data_header_t*)dst;
    GPU_ERROR("vgbuf:%d(%ld,%ld)", gpu_header->vgbuf.format, gpu_header->vgbuf.width, gpu_header->vgbuf.height);
    stride = gpu_header->vgbuf.width;
    if (cf == LV_IMG_CF_INDEXED_8BIT) {
      gpu_header->vgbuf.format = VG_LITE_BGRA8888;
    }
    src += sizeof(gpu_data_header_t);
    dst += sizeof(gpu_data_header_t);
  }
  if (cf == LV_IMG_CF_TRUE_COLOR || cf == LV_IMG_CF_TRUE_COLOR_ALPHA) {
    fast_gaussian_blur((void*)dst, stride, (void*)src, stride, &area, 1);
  } else if (cf == LV_IMG_CF_INDEXED_8BIT) {
    const uint32_t* palette = (const uint32_t*)src;
    if (gpu_format) {
      palette += header->h * stride;
    } else {
      src += 1024;
    }
    convert_indexed8_to_argb8888((void*)dst, stride << 2, (void*)src, stride,
        palette, header);
    fast_gaussian_blur((void*)dst, stride, (void*)dst, stride, &area, 1);
  } else {
    GPU_ERROR("Filter does not support this color format!");
    return LV_RES_INV;
  }
  return LV_RES_OK;
}

const char* generate_filtered_image(const char* src)
{
    char mander[PATH_MAX];
    char path[PATH_MAX];
    strncpy(mander, src, PATH_MAX - 1);
    char* name = (char*)strrchr(mander, '/');
    while (name) {
        *name = '_';
        name = (char*)strrchr(mander, '/');
    }
    name = mander;
    int16_t name_len = strlen(name);
    int16_t path_length = name_len + sizeof(CONFIG_GPU_IMG_CACHE_PATH);
    name += LV_MAX(0, path_length - PATH_MAX);
    snprintf(path, path_length, CONFIG_GPU_IMG_CACHE_PATH "%s", name);
    char* dst = strdup(path);
    if (!dst) {
        GPU_ERROR("malloc failed\n");
        return src;
    }
    if (access(CONFIG_GPU_IMG_CACHE_PATH, F_OK) != 0) {
      mkdir(CONFIG_GPU_IMG_CACHE_PATH, 0755);
    } else if (access(dst, F_OK) == 0) {
      GPU_ERROR("%s exists!", dst);
      return dst;
    }
    lv_fs_file_t file;
    lv_fs_res_t res;
    uint32_t bytes_done;
    lv_img_header_t header;
    uint8_t* data = NULL;
    uint8_t* dst_buf = NULL;
    res = lv_fs_open(&file, src, LV_FS_MODE_RD);
    if (res != LV_FS_RES_OK) {
        GPU_ERROR("open %s failed\n", src);
        goto Error;
    }
    res = lv_fs_read(&file, &header, 4, &bytes_done);
    if (res != LV_FS_RES_OK || bytes_done != 4) {
        GPU_ERROR("read header failed\n");
        goto Error_file;
    }
    uint32_t dst_size = header.w * header.h << 2;
    uint32_t data_size = (header.cf == LV_IMG_CF_INDEXED_8BIT)
        ? header.w * header.h + 1024
        : dst_size;
    const char* ext = lv_fs_get_ext(mander);
    if (strcmp(ext, "gpu") == 0) {
        data_size = gpu_img_buf_get_img_size(header.w, header.h, header.cf);
        dst_size = gpu_img_buf_get_img_size(header.w, header.h,
                                            LV_IMG_CF_TRUE_COLOR_ALPHA);
    }
    if (header.cf == LV_IMG_CF_INDEXED_8BIT) {
        dst_buf = malloc(dst_size);
        if (!dst_buf) {
            GPU_ERROR("malloc failed");
            goto Error_file;
        }
    } else if (header.cf != LV_IMG_CF_TRUE_COLOR
               && header.cf != LV_IMG_CF_TRUE_COLOR_ALPHA) {
        GPU_ERROR("unsupported format");
        goto Error_file;
    }
    data = malloc(data_size);
    if (!data) {
        GPU_ERROR("malloc data failed\n");
        goto Error_file;
    }
    res = lv_fs_read(&file, data, data_size, &bytes_done);
    if (res != LV_FS_RES_OK || bytes_done != data_size) {
        GPU_ERROR("read data failed\n");
        goto Error_file;
    }
    lv_fs_close(&file);
    res = lv_fs_open(&file, dst, LV_FS_MODE_WR);
    if (res != LV_FS_RES_OK) {
        GPU_ERROR("open %s failed\n", dst);
        goto Error;
    }
    lv_img_header_t new_header = header;
    new_header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA;
    res = lv_fs_write(&file, &new_header, 4, &bytes_done);
    if (res != LV_FS_RES_OK || bytes_done != 4) {
        GPU_ERROR("write header failed\n");
        goto Error_file;
    }
    if (!dst_buf) {
        dst_buf = data;
    }
    pre_zoom_gaussian_filter(dst_buf, data, &header, ext);
    res = lv_fs_write(&file, dst_buf, dst_size, &bytes_done);
    if (dst_buf == data) {
        dst_buf = NULL;
    }
    if (res != LV_FS_RES_OK || bytes_done != dst_size) {
        GPU_ERROR("write data failed\n");
        goto Error_file;
    }
    lv_fs_close(&file);
    free(data);
    return dst;
Error_file:
    lv_fs_close(&file);
Error:
    free(dst);
    if (data) {
        free(data);
    }
    if (dst_buf) {
        free(dst_buf);
    }
    return src;
}

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
    lv_opa_t opa, bool premult)
{
  lv_coord_t w = lv_area_get_width(draw_area);
  lv_coord_t h = lv_area_get_height(draw_area);
  for (lv_coord_t i = 0; i < h; i++) {
    uint8_t* phwSource = (uint8_t*)src;
    uint8_t* pwTarget = dst;
    uint32_t blkCnt = w;
    if (premult) {
      __asm volatile(
          "   .p2align 2                                                  \n"
          "   bics                    r0, %[loopCnt], 0xF                 \n"
          "   wlstp.8                 lr, r0, 1f                          \n"
          "   2:                                                          \n"
          "   vld40.8                 {q0, q1, q2, q3}, [%[pSrc]]         \n"
          "   vld41.8                 {q0, q1, q2, q3}, [%[pSrc]]         \n"
          "   vld42.8                 {q0, q1, q2, q3}, [%[pSrc]]         \n"
          "   vld43.8                 {q0, q1, q2, q3}, [%[pSrc]]!        \n"
          "   vld40.8                 {q4, q5, q6, q7}, [%[pDst]]         \n"
          "   vld41.8                 {q4, q5, q6, q7}, [%[pDst]]         \n"
          "   vld42.8                 {q4, q5, q6, q7}, [%[pDst]]         \n"
          "   vld43.8                 {q4, q5, q6, q7}, [%[pDst]]         \n"
          "   vdup.8                  q7, %[opa]                          \n"
          "   vrmulh.u8               q0, q0, q7                          \n"
          "   vrmulh.u8               q1, q1, q7                          \n"
          "   vrmulh.u8               q2, q2, q7                          \n"
          "   vrmulh.u8               q3, q3, q7                          \n"
          "   vmvn.i32                q7, #0                              \n"
          "   vmvn                    q3, q3                              \n"
          "   vrmulh.u8               q4, q4, q3                          \n"
          "   vrmulh.u8               q5, q5, q3                          \n"
          "   vrmulh.u8               q6, q6, q3                          \n"
          "   vadd.i8                 q4, q0, q4                          \n"
          "   vadd.i8                 q5, q1, q5                          \n"
          "   vadd.i8                 q6, q2, q6                          \n"
          "   vst40.8                 {q4, q5, q6, q7}, [%[pDst]]         \n"
          "   vst41.8                 {q4, q5, q6, q7}, [%[pDst]]         \n"
          "   vst42.8                 {q4, q5, q6, q7}, [%[pDst]]         \n"
          "   vst43.8                 {q4, q5, q6, q7}, [%[pDst]]!        \n"
          "   letp                    lr, 2b                              \n"
          "   1:                                                          \n"
          "   ands                    r0, %[loopCnt], 0xF                 \n"
          "   movs                    r0, r0, lsl #2                      \n"
          "   vdup.8                  q2, %[opa]                          \n"
          "   vmvn.i32                q5, #0                              \n"
          "   wlstp.8                 lr, r0, 3f                          \n"
          "   4:                                                          \n"
          "   vldrb.8                 q0, [%[pDst]]                       \n"
          "   vldrb.8                 q1, [%[pSrc]], #16                  \n"
          "   vsri.32                 q3, q1, #8                          \n"
          "   vsri.32                 q3, q1, #16                         \n"
          "   vsri.32                 q3, q1, #24                         \n"
          "   vrmulh.u8               q3, q3, q2                          \n"
          "   vmvn                    q4, q3                              \n"
          "   vrmulh.u8               q0, q0, q4                          \n"
          "   vrmulh.u8               q1, q1, q2                          \n"
          "   vadd.i8                 q0, q0, q1                          \n"
          "   vsli.32                 q0, q5, #24                         \n"
          "   vstrb.8                 q0, [%[pDst]], #16                  \n"
          "   letp                    lr, 4b                              \n"
          "   3:                                                          \n"
          : [pSrc] "+r"(phwSource), [pDst] "+r"(pwTarget)
          : [opa] "r"(opa), [loopCnt] "r"(blkCnt)
          : "q0", "q1", "q2", "q3", "q4", "q5", "q6", "q7", "r0", "lr",
          "memory");
    } else {
      __asm volatile(
          "   .p2align 2                                                  \n"
          "   bics                    r0, %[loopCnt], 0xF                 \n"
          "   wlstp.8                 lr, %[loopCnt], 1f                  \n"
          "   2:                                                          \n"
          "   vld40.8                 {q0, q1, q2, q3}, [%[pSrc]]         \n"
          "   vld41.8                 {q0, q1, q2, q3}, [%[pSrc]]         \n"
          "   vld42.8                 {q0, q1, q2, q3}, [%[pSrc]]         \n"
          "   vld43.8                 {q0, q1, q2, q3}, [%[pSrc]]!        \n"
          "   vld40.8                 {q4, q5, q6, q7}, [%[pDst]]         \n"
          "   vld41.8                 {q4, q5, q6, q7}, [%[pDst]]         \n"
          "   vld42.8                 {q4, q5, q6, q7}, [%[pDst]]         \n"
          "   vld43.8                 {q4, q5, q6, q7}, [%[pDst]]         \n"
          "   vdup.8                  q7, %[opa]                          \n"
          "   vrmulh.u8               q3, q3, q7                          \n"
          "   vrmulh.u8               q0, q0, q3                          \n"
          "   vrmulh.u8               q1, q1, q3                          \n"
          "   vrmulh.u8               q2, q2, q3                          \n"
          "   vmvn.i32                q7, #0                              \n"
          "   vsub.i8                 q3, q7, q3                          \n"
          "   vrmulh.u8               q4, q4, q3                          \n"
          "   vrmulh.u8               q5, q5, q3                          \n"
          "   vrmulh.u8               q6, q6, q3                          \n"
          "   vadd.i8                 q4, q0, q4                          \n"
          "   vadd.i8                 q5, q1, q5                          \n"
          "   vadd.i8                 q6, q2, q6                          \n"
          "   vst40.8                 {q4, q5, q6, q7}, [%[pDst]]         \n"
          "   vst41.8                 {q4, q5, q6, q7}, [%[pDst]]         \n"
          "   vst42.8                 {q4, q5, q6, q7}, [%[pDst]]         \n"
          "   vst43.8                 {q4, q5, q6, q7}, [%[pDst]]!        \n"
          "   letp                    lr, 2b                              \n"
          "   1:                                                          \n"
          "   ands                    r0, %[loopCnt], 0xF                 \n"
          "   movs                    r0, r0, lsl #2                      \n"
          "   vdup.8                  q2, %[opa]                          \n"
          "   vmvn.i32                q5, #0                              \n"
          "   wlstp.8                 lr, r0, 3f                          \n"
          "   4:                                                          \n"
          "   vldrb.8                 q0, [%[pDst]]                       \n"
          "   vldrb.8                 q1, [%[pSrc]], #16                  \n"
          "   vsri.32                 q3, q1, #8                          \n"
          "   vsri.32                 q3, q1, #16                         \n"
          "   vsri.32                 q3, q1, #24                         \n"
          "   vrmulh.u8               q3, q3, q2                          \n"
          "   vmvn                    q4, q3                              \n"
          "   vrmulh.u8               q0, q0, q4                          \n"
          "   vrmulh.u8               q1, q1, q3                          \n"
          "   vadd.i8                 q0, q0, q1                          \n"
          "   vsli.32                 q0, q5, #24                         \n"
          "   vstrb.8                 q0, [%[pDst]], #16                  \n"
          "   letp                    lr, 4b                              \n"
          "   3:                                                          \n"
          : [pSrc] "+r"(phwSource), [pDst] "+r"(pwTarget)
          : [opa] "r"(opa), [loopCnt] "r"(blkCnt)
          : "q0", "q1", "q2", "q3", "q4", "q5", "q6", "q7", "r0", "lr",
          "memory");
    }
    src += src_stride;
    dst += dst_stride;
  }
}

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
    const lv_draw_img_dsc_t* dsc, bool premult)
{
  uint8_t* phwSource = (uint8_t*)src;
  uint8_t* pwTarget = dst + ((draw_area->y1 * dst_stride + draw_area->x1) << 2);
  lv_coord_t draw_w = lv_area_get_width(draw_area);
  lv_coord_t draw_h = lv_area_get_height(draw_area);
  lv_coord_t src_w = lv_area_get_width(src_area);
  lv_coord_t src_h = lv_area_get_height(src_area);
  float angle = dsc->angle * 0.1f * PI_DEG;
  lv_point_t start = {
    .x = draw_area->x1 - src_area->x1 - dsc->pivot.x,
    .y = draw_area->y1 - src_area->y1 - dsc->pivot.y
  };
  lv_coord_t w_aligned_up_4 = ALIGN_UP(draw_w, 4);
  float cosma = cosf(angle);
  float sinma = sinf(angle);
  struct {
    float index_buf[8]; // used as temporary stack
    float rot_vec[4]; // #32
    float xoxo[4]; // #48
    uint32_t oyoy[4]; // #64
    float pivot_f32[4]; // #80
    float xyxy[4]; // #96
    uint32_t ratio_buf[8]; // #112
    uint32_t loop_x; // #144
    uint32_t loop_y_with_aa_flag; // #148
    uint32_t dst_offset; // #152
    float zoom_inv; // #156
    uint32_t src_x_max; // #160
    uint32_t src_y_max; // #164
    float ff; // #168
    uint32_t opa_with_flag; // #172
    uint32_t premult; // #176
    uint32_t tail; // #180
  } args = {
    .index_buf = { start.x, start.y, start.x + 1, start.y,
        start.x + 2, start.y, start.x + 3, start.y },
    .rot_vec = { cosma, -sinma, cosma, -sinma },
    .xoxo = { 4, 0, 4, 0 },
    .oyoy = { 0, src_stride, 0, src_stride },
    .pivot_f32 = { dsc->pivot.x, dsc->pivot.y, dsc->pivot.x, dsc->pivot.y },
    .xyxy = { -w_aligned_up_4, 1, -w_aligned_up_4, 1 },
    .loop_x = w_aligned_up_4 >> 2,
    .loop_y_with_aa_flag = (draw_h - 1) << 1 | dsc->antialias,
    .dst_offset = dst_stride - w_aligned_up_4,
    .zoom_inv = 256.0f / dsc->zoom,
    .src_x_max = src_w,
    .src_y_max = src_h,
    .ff = 255,
    .opa_with_flag = dsc->opa,
    .premult = premult ? 0xFFFF : 0,
    .tail = ((draw_w - 1) & 3) + 1
  };
  void* argp = &args;
  __asm volatile(
      "   .p2align 2                                                  \n"
      "   vldrw.32                q0, [%[arg]]                        \n" /* q0 = [x0, y0, x1, y1] */
      "   vldrw.32                q1, [%[arg], #32]                   \n" /* rotation vector (cos+isin) */
      "   vldrw.32                q5, [%[arg], #16]                   \n" /* q5 = [x2, y2, x3, y3] */
      "   ldr                     r3, [%[arg], #148]                  \n" /* y loop start */
      "   .yloop:                                                     \n"
      "   ldr                     r1, [%[arg], #144]                  \n" /* x loop start */
      "   wls                     lr, r1, 1f                          \n"
      "   2:                                                          \n"
      "   ldr                     r2, [%[arg], #156]                  \n" /* r2 = zoom_inv */
      "   vldrw.32                q3, [%[arg], #80]                   \n" /* add pivot offset */
      "   vcmul.f32               q2, q0, q1, #0                      \n"
      "   vcmul.f32               q6, q5, q1, #0                      \n"
      "   ldr                     r0, [%[arg], #160]                  \n" /* r0 = x limit */
      "   vcmla.f32               q2, q0, q1, #90                     \n" /* X = x*cos-y*sin  Y = x*sin+y*cos */
      "   vmov                    q4, q3                              \n" /* backup q3 for next use */
      "   vfma.f32                q3, q2, r2                          \n" /* q3 = [x0' y0' x1' y1'] */
      "   vcmla.f32               q6, q5, q1, #90                     \n"
      "   vstrw.32                q3, [%[arg]]                        \n"
      "   vfma.f32                q4, q6, r2                          \n" /* q4 = [x2' y2' x3' y3'] */
      "   vstrw.32                q4, [%[arg], #16]                   \n" /* interleaved-save transformed coordinates */
      "   ldr                     r1, [%[arg], #164]                  \n" /* r1 = y limit */
      "   vld20.32                {q3, q4}, [%[arg]]                  \n" /* q3 = X'(0,1,2,3) */
      "   vld21.32                {q3, q4}, [%[arg]]                  \n" /* q4 = Y'(0,1,2,3) */
      "   vrintm.f32              q6, q3                              \n"
      "   vrintm.f32              q7, q4                              \n"
      "   vsub.f32                q3, q3, q6                          \n" /* q3 = X' decimals */
      "   vsub.f32                q4, q4, q7                          \n" /* q4 = Y' decimals */
      "   vcvt.s32.f32            q6, q6                              \n" /* q6 = floored X' */
      "   vcvt.s32.f32            q7, q7                              \n" /* q7 = floored Y' */
      "   vcmp.s32                lt, q6, r0                          \n" /* x < w */
      "   vmrs                    r2, p0                              \n"
      "   vcmp.s32                lt, q7, r1                          \n" /* y < h */
      "   vmrs                    r5, p0                              \n"
      "   movs                    r4, #0                              \n"
      "   ands                    r2, r5                              \n"
      "   vcmp.s32                ge, q6, r4                          \n" /* x >= 0 */
      "   vmrs                    r5, p0                              \n"
      "   ands                    r2, r5                              \n"
      "   vcmp.s32                ge, q7, r4                          \n" /* y >= 0 */
      "   vmrs                    r5, p0                              \n"
      "   ldr                     r4, [%[arg], #68]                   \n" /* r4 = src_stride */
      "   ands                    r2, r5                              \n" /* r2 = 0<=x<w && 0<=y<h */
      "   subs                    r0, #1                              \n"
      "   subs                    r1, #1                              \n"
      "   vcmp.s32                lt, q6, r0                          \n" /* x1 < w */
      "   vmrs                    r0, p0                              \n" /* r0 = x1 mask */
      "   vcmp.s32                lt, q7, r1                          \n" /* y1 < h */
      "   vmrs                    r1, p0                              \n" /* r1 = y1 mask */
      "   vmla.s32                q6, q7, r4                          \n" /* q6 = offset */
      "   vmsr                    p0, r2                              \n" /* predicate */
      "   vpst                                                        \n"
      "   vldrwt.u32              q2, [%[pSrc], q6, uxtw #2]          \n" /* load src pixels into q2 */
      "   ands                    r5, r3, #1                          \n" /* check aa flag */
      "   beq                     .output                             \n" /* no alias then done */
      "   ldr                     r5, [%[arg], #168]                  \n" /* r5 = 255f */
      "   ands                    r0, r2                              \n" /* apply src_00 mask */
      "   ands                    r1, r2                              \n"
      "   vmul.f32                q3, q3, r5                          \n"
      "   vmul.f32                q4, q4, r5                          \n"
      "   movs                    r2, #0x01010101                     \n"
      "   vcvt.s32.f32            q3, q3                              \n"
      "   vcvt.s32.f32            q4, q4                              \n"
      "   vmul.u32                q3, q3, r2                          \n"
      "   vmul.u32                q4, q4, r2                          \n"
      "   vmvn                    q3, q3                              \n" /* q3 = ~[x0 x1 x2 x3] */
      "   vmvn                    q4, q4                              \n" /* q4 = ~[y0 y1 y2 y3] */
      "   vrmulh.u8               q2, q2, q3                          \n"
      "   movs                    r2, #1                              \n"
      "   vrmulh.u8               q2, q2, q4                          \n" /* q2 = src_00 * (1-x0) * (1-y0) */
      "   vmsr                    p0, r0                              \n" /* predicate x1 */
      "   vpstt                                                       \n"
      "   vaddt.i32               q6, q6, r2                          \n"
      "   vldrwt.u32              q7, [%[pSrc], q6, uxtw #2]          \n" /* load src_01 pixels into q7 */
      "   vmvn                    q3, q3                              \n" /* q3 = [x0 x1 x2 x3] */
      "   vrmulh.u8               q7, q7, q4                          \n"
      "   vrmulh.u8               q7, q7, q3                          \n" /* q7 = src_01 * x0 * (1-y0) */
      "   vadd.i8                 q2, q2, q7                          \n" /* q2 += q7 */
      "   vmsr                    p0, r1                              \n" /* predicate y1 */
      "   vpstt                                                       \n"
      "   vaddt.i32               q6, q6, r4                          \n"
      "   vldrwt.u32              q7, [%[pSrc], q6, uxtw #2]          \n" /* load src_11 pixels into q7 */
      "   vmvn                    q4, q4                              \n" /* q4 = [y0 y1 y2 y3] */
      "   vrmulh.u8               q7, q7, q3                          \n"
      "   vrmulh.u8               q7, q7, q4                          \n" /* q7 = src_11 * x0 * y0 */
      "   vadd.i8                 q2, q2, q7                          \n" /* q2 += q7 */
      "   vmsr                    p0, r0                              \n" /* predicate x1 */
      "   vpstt                                                       \n"
      "   vsubt.i32               q6, q6, r2                          \n"
      "   vldrwt.u32              q7, [%[pSrc], q6, uxtw #2]          \n" /* load src_01 pixels into q7 */
      "   vmvn                    q3, q3                              \n" /* q3 = ~[x0 x1 x2 x3] */
      "   vrmulh.u8               q7, q7, q4                          \n"
      "   vrmulh.u8               q7, q7, q3                          \n" /* q7 = src_10 * (1-x0) * y0 */
      "   vadd.i8                 q2, q2, q7                          \n" /* q2 += q7 */
      "   .output:                                                    \n"
      "   ldr                     r0, [%[arg], #172]                  \n" /* opa */
      "   vmov                    q6, q2                              \n"
      "   vdup.8                  q4, r0                              \n" /* q4 = [opa(8x16)] */
      "   vsri.32                 q6, q6, #8                          \n" /* create vector of Sa */
      "   vldrw.32                q3, [%[pDst]]                       \n" /* q3 = D */
      "   vsri.32                 q6, q6, #16                         \n"
      "   ldr                     r1, [%[arg], #176]                  \n" /* premult as vpr.p0 */
      "   vrmulh.u8               q6, q6, q4                          \n" /* q6 = Sa' = Sa*opa */
      "   vrmulh.u8               q7, q2, q4                          \n" /* q7 = S*opa */
      "   vmvn                    q4, q6                              \n"
      "   vrmulh.u8               q2, q2, q6                          \n" /* q2 = S*Sa*opa */
      "   vrmulh.u8               q3, q3, q4                          \n" /* q3 = D*(1-Sa*opa) */
      "   vmsr                    p0, r1                              \n"
      "   vpsel                   q2, q7, q2                          \n" /* select q2 based on premult */
      "   cmp                     lr, #1                              \n" /* tail predication */
      "   ite                     eq                                  \n"
      "   ldreq                   r2, [%[arg], #180]                  \n" /* r2 = tail */
      "   movne                   r2, #4                              \n" /* 4 for all */
      "   vadd.i8                 q2, q2, q3                          \n" /* D' = S*Sa' + D*(1-Sa') */
      "   vldrw.32                q7, [%[arg], #48]                   \n" /* q7 = xoxo */
      "   vctp.32                 r2                                  \n"
      "   vpst                                                        \n"
      "   vstrwt.32               q2, [%[pDst]], #16                  \n" /* save q2 with tail predication */
      "   vadd.f32                q0, q0, q7                          \n" /* X0X1 update */
      "   vadd.f32                q5, q5, q7                          \n" /* X2X3 update */
      "   le                      lr, 2b                              \n"
      "   1:                                                          \n" /* xloop end */
      "   vldrw.32                q4, [%[arg], #96]                   \n" /* q4 = xyxy */
      "   vadd.f32                q0, q0, q4                          \n" /* X0X1 update */
      "   ldr                     r0, [%[arg], #152]                  \n" /* r0 = dst_offset */
      "   vadd.f32                q5, q5, q4                          \n" /* X2X3 update */
      "   adds                    %[pDst], %[pDst], r0, lsl #2        \n"
      "   subs                    r3, #2                              \n"
      "   bmi                     .loopEnd                            \n"
      "   b                       .yloop                              \n"
      "   .loopEnd:                                                   \n"
      : [pDst] "+r"(pwTarget)
      : [pSrc] "r"(phwSource), [arg] "r"(argp)
      : "q0", "q1", "q2", "q3", "q4", "q5", "q6", "q7", "r0", "r1", "r2", "r3", "r4", "r5",
      "memory");
}

#endif
