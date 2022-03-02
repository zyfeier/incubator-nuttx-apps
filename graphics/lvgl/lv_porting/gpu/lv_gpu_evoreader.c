/****************************************************************************
 * apps/graphics/lvgl/lv_porting/gpu/lv_gpu_evoreader.c
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
#include "lv_gpu_evoreader.h"
#include <debug.h>
#include <stdio.h>

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* These structs that starts with evoh are used to extract and record eternal
 * structures of evo files and only used within this file. */
struct evoh_paint_s {
  float cx;
  float cy;
  float radial;
  float fx;
  float fy;
  unsigned int spread_mode;
};

struct evoh_egoheader_s {
  unsigned int version; /* useless(x)*/
  unsigned int elm_object_type; /* ?? */
  float transform[3][3]; /* drawFunc.matrix */
  unsigned int object_count; /* process */

  /* For marking file structure, disabled for read file convenience */
#if 0
    unsigned int *sizes_of_evoh_data;   /* Array, width=int*object_count*/
    unsigned int *offsets_of_evoh_data; /* Array, width=int*object_count*/
#endif
};

struct evoh_path_s {
  float bounds[4]; /* vg_lite_path.bounding_box*/
  unsigned int format; /* vg_lite_path.vg_lite_format*/
  unsigned int length; /* vg_lite_path.path_length*/
  unsigned data_offset; /* process*/
  float transform[3][3]; /* matrix*/
  unsigned int quality; /* vg_lite_path.vg_lite_quality*/
  unsigned int fill_rule; /* drawFunc.vg_lite_fill*/
  unsigned int blending_mode; /* drawFunc.vg_lite_blend*/
  unsigned int paint_type;
  unsigned int color; /* drawfunc.vg_lite_color_t*/
};
struct evoh_grad_data_s {
  float gradient_transform[3][3]; /* vg_lite_linear_gradient.matrix*/
  unsigned int gradient_stop_count; /* vg_lite_linear_gradient.count*/
  unsigned int stop_offset; /* process*/
  unsigned int color_offset; /* process*/
  /* For marking file structure, disabled for read file convenience */
#if 0
    float *gradient_stops;         /* array, width=gradient_stop_count vg_lite_linear_gradient*/
    unsigned int *gradient_colors; /* array width=gradient_color_count vg_lite_linear_gradient*/
    void *path_data;               /* vg_lite_path.path*/
#endif
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/
static inline lv_fs_res_t init_grad_buff(vg_lite_buffer_t* dst);

/****************************************************************************
 * Private Functions
 ****************************************************************************/
static inline lv_fs_res_t init_grad_buff(vg_lite_buffer_t* dst)
{
  dst->format = VG_LITE_BGRA8888;
  dst->tiled = VG_LITE_LINEAR;
  dst->image_mode = VG_LITE_NORMAL_IMAGE_MODE;
  dst->transparency_mode = VG_LITE_IMAGE_OPAQUE;

  dst->width = VLC_GRADBUFFER_WIDTH;
  dst->height = 1;
  dst->stride = VLC_GRADBUFFER_WIDTH * 4;

  lv_memset_00(&dst->yuv, sizeof(dst->yuv));

  dst->memory = lv_mem_alloc(dst->stride * dst->height);
  if (!dst->memory) {
    LV_LOG_ERROR("Gradient buffer allocation failed!\n");
    return LV_FS_RES_OUT_OF_MEM;
  }
  dst->address = (uint32_t)dst->memory;
  dst->handle = NULL;

  return LV_FS_RES_OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: evo_read
 *
 * Description:
 *   Reads in the evo file pointed by fp into the struct pointer. Corresponding
 *   error code would be returned by error_t should any error occurr. Does not
 *   take gpu memory.
 *
 * Input Parameters:
 *
 *   lv_fs_file_t* fp: LVGL file descriptor of evo file.
 *   evo_fcontent_t* ret: Pointer to the pre-allocated fcontent file
 *
 * Returned Value:
 *   error_t with error number denoted in lv_fs.h .
 *
 ****************************************************************************/
lv_fs_res_t evo_read(lv_fs_file_t* fp, evo_fcontent_t* ret)
{
  /* local variables */

  struct evoh_egoheader_s header;
  int lineargrad_path_count = 0;
  int radgrad_path_count = 0;
  float gstops_buf[16];
  uint32_t gcolor_buf[16];
  uint32_t stopconv[16];
  struct evoh_path_s tmppath;
  struct evoh_grad_data_s gdata;
  lv_fs_res_t readres;
  unsigned int r, g, b;

  // Sanity checks
  lv_memset_00(ret, sizeof(evo_fcontent_t));

  /* Read and process header */

  readres = lv_fs_read(fp, &header, sizeof(struct evoh_egoheader_s), NULL);
  if (readres != LV_FS_RES_OK) {
    LV_LOG_ERROR("read error");
    evo_clear(ret);
    return readres;
  }
  ret->pathcount = header.object_count;
  LV_LOG_WARN("Evo resolving start:");
  LV_LOG_WARN("path count: \t%d", ret->pathcount);
  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 3; j++) {
      ret->transform.m[i][j] = (vg_lite_float_t)header.transform[i][j];
    }
  }

  /* Skip the size and offset data useless to us */

  lv_fs_seek(fp, sizeof(unsigned int) * 2 * ret->pathcount, LV_FS_SEEK_CUR);

  /* lv_mem_alloc fields within ret object. */
  ret->evo_path_dsc = lv_mem_alloc(sizeof(evo_path_dsc_t) * ret->pathcount);

  if (!ret->evo_path_dsc) {
    LV_LOG_ERROR("lv_mem_alloc error!");
    evo_clear(ret);
    return LV_FS_RES_OUT_OF_MEM;
  }
  lv_memset_00(ret->evo_path_dsc, sizeof(evo_path_dsc_t) * ret->pathcount);

  /* Read in individual paths. */

  for (int i = 0; i < ret->pathcount; i++) {
    LV_LOG_WARN("Processing Path #%d", i);
    readres = lv_fs_read(fp, &tmppath, sizeof(struct evoh_path_s), NULL);
    if (readres != LV_FS_RES_OK) {
      LV_LOG_ERROR("read error");
      evo_clear(ret);
      return readres;
    }

    /* Filling in corresponding fields */
    ret->evo_path_dsc[i].fill_rule = tmppath.fill_rule;
    ret->evo_path_dsc[i].blending_mode = tmppath.blending_mode;
    if ((tmppath.color & 0xFF000000) != 0xFF000000) {
      b = tmppath.color & 0x000000FF;
      tmppath.color >>= 8;
      g = tmppath.color & 0x000000FF;
      tmppath.color >>= 8;
      r = tmppath.color & 0x000000FF;
      tmppath.color >>= 8;
      b *= tmppath.color;
      g *= tmppath.color;
      r *= tmppath.color;
      tmppath.color <<= 24;
      tmppath.color |= (r << 8) & 0x00FF0000;
      tmppath.color |= g & 0x0000FF00;
      tmppath.color |= b >> 8;
    }
    ret->evo_path_dsc[i].color = tmppath.color;
    /* Filling in elements under each vg_lite_path_t function. */

    lv_memcpy(&ret->evo_path_dsc[i].pathtransform, &tmppath.transform, 9 * sizeof(float));

    if (tmppath.length & 0x80000000) {
      tmppath.length &= 0x7FFFFFFF;
    }
    ret->evo_path_dsc[i].vpath.path_changed = 1;
    ret->evo_path_dsc[i].vpath.path = lv_mem_alloc(tmppath.length);
    if (!ret->evo_path_dsc[i].vpath.path) {
      LV_LOG_ERROR("lv_mem_alloc error");
      evo_clear(ret);
      return LV_FS_RES_OUT_OF_MEM;
    }

    /* Determining path type and assign corresponding memory. */
    switch (tmppath.paint_type) {

    /* SOLID_PAINT */
    case 0:
      ret->evo_path_dsc[i].path_type = 0;
      break;

    /* LINEAR_GRADIENT */
    case 2:
      ret->evo_path_dsc[i].lin_gradient = lv_mem_alloc(sizeof(vg_lite_linear_gradient_t));
      if (!ret->evo_path_dsc[i].lin_gradient) {
        LV_LOG_ERROR("lv_mem_alloc error");
        evo_clear(ret);
        return LV_FS_RES_OUT_OF_MEM;
      }
      lv_memset_00(ret->evo_path_dsc[i].lin_gradient, sizeof(vg_lite_linear_gradient_t));
      init_grad_buff(&ret->evo_path_dsc[i].lin_gradient->image);
      ret->evo_path_dsc[i].path_type = ++lineargrad_path_count;
      break;

    /* RADIAL_GRADIENT */
    case 3:
      ret->evo_path_dsc[i].rad_gradient = lv_mem_alloc(sizeof(vg_lite_radial_gradient_t));
      if (!ret->evo_path_dsc[i].rad_gradient) {
        LV_LOG_ERROR("lv_mem_alloc error");
        evo_clear(ret);
        return LV_FS_RES_OUT_OF_MEM;
      }
      lv_memset_00(ret->evo_path_dsc[i].rad_gradient, sizeof(vg_lite_radial_gradient_t));
      init_grad_buff(&ret->evo_path_dsc[i].rad_gradient->image);

      /* Reading in the extra data for radial gradients. */
      readres = lv_fs_read(fp, &ret->evo_path_dsc[i].rad_gradient->radialGradient, sizeof(vg_lite_radial_gradient_parameter_t), NULL);
      if (readres != LV_FS_RES_OK) {
        LV_LOG_ERROR("read error");
        evo_clear(ret);
        return readres;
      }
      readres = lv_fs_read(fp, &ret->evo_path_dsc[i].rad_gradient->SpreadMode, sizeof(unsigned int), NULL);
      if (readres != LV_FS_RES_OK) {
        LV_LOG_ERROR("read error");
        evo_clear(ret);
        return readres;
      }
      ret->evo_path_dsc[i].path_type = -(++radgrad_path_count);
      break;

    default:
      LV_LOG_ERROR("Unsupported paint type %d !", tmppath.paint_type);
      ret->evo_path_dsc[i].path_type = 0;
      break;
    }
    readres = lv_fs_read(fp, &gdata, sizeof(struct evoh_grad_data_s), NULL);
    if (readres != LV_FS_RES_OK) {
      LV_LOG_ERROR("read error");
      evo_clear(ret);
      return readres;
    }

    /* If gradient data of any form is present, process it. */
    if (gdata.gradient_stop_count) {
      readres = lv_fs_read(fp, gstops_buf, 4 * gdata.gradient_stop_count, NULL);
      if (readres != LV_FS_RES_OK) {
        LV_LOG_ERROR("read error");
        evo_clear(ret);
        return readres;
      }
      // TODOs?
      readres = lv_fs_read(fp, gcolor_buf, 4 * gdata.gradient_stop_count, NULL);
      if (readres != LV_FS_RES_OK) {
        LV_LOG_ERROR("read error");
        evo_clear(ret);
        return readres;
      }
      LV_LOG_WARN("%f,%08X\n", gstops_buf[0], gcolor_buf[0]);
      /* Treating gradient data differently depending on gradient type. */
      if (ret->evo_path_dsc[i].path_type > 0) {
        // Process linear gradient.
        for (int j = 0; j < gdata.gradient_stop_count; j++) {
          stopconv[j] = (unsigned int)(gstops_buf[j] * 255.0f);
          if ((gcolor_buf[j] & 0xFF000000) != 0xFF000000) {
            b = gcolor_buf[j] & 0x000000FF;
            gcolor_buf[j] >>= 8;
            g = gcolor_buf[j] & 0x000000FF;
            gcolor_buf[j] >>= 8;
            r = gcolor_buf[j] & 0x000000FF;
            gcolor_buf[j] >>= 8;
            b *= gcolor_buf[j];
            g *= gcolor_buf[j];
            r *= gcolor_buf[j];
            gcolor_buf[j] |= (r << 8) & 0x00FF0000;
            gcolor_buf[j] <<= 24;
            gcolor_buf[j] |= g & 0x0000FF00;
            gcolor_buf[j] |= b >> 8;
          }
          LV_LOG_WARN("%ld,%08X\n", stopconv[j], gcolor_buf[j]);
        }
        vg_lite_linear_gradient_t* lgrad = ret->evo_path_dsc[i].lin_gradient;
        lv_memcpy(&lgrad->matrix, &gdata.gradient_transform, sizeof(vg_lite_matrix_t));
        lgrad->count = gdata.gradient_stop_count;
        vg_lite_set_grad(lgrad, gdata.gradient_stop_count, gcolor_buf, stopconv);
        vg_lite_update_grad(lgrad);
      } else if (ret->evo_path_dsc[i].path_type < 0) {
        // Process radial gradient.
        vg_lite_radial_gradient_t* rgrad = ret->evo_path_dsc[i].rad_gradient;
        for (int j = 0; j < gdata.gradient_stop_count; j++) {
          rgrad->vgColorRamp[j].blue = gcolor_buf[j] & 0x000000FF;
          gcolor_buf[j] >>= 8;
          rgrad->vgColorRamp[j].green = gcolor_buf[j] & 0x000000FF;
          gcolor_buf[j] >>= 8;
          rgrad->vgColorRamp[j].red = gcolor_buf[j] & 0x000000FF;
          gcolor_buf[j] >>= 8;
          rgrad->vgColorRamp[j].alpha = gcolor_buf[j];
          rgrad->vgColorRamp[j].stop = gstops_buf[j];
        }
        rgrad->count = gdata.gradient_stop_count;
        vg_lite_error_t vres = vg_lite_set_rad_grad(rgrad, gdata.gradient_stop_count, rgrad->vgColorRamp, rgrad->radialGradient, rgrad->SpreadMode, 1);
        vres |= vg_lite_update_rad_grad(rgrad);
        if (vres) {
          LV_LOG_ERROR("Radial gradiant setup error.");
          evo_clear(ret);
          return LV_FS_RES_HW_ERR;
        }
        lv_memcpy(&rgrad->matrix, &gdata.gradient_transform, sizeof(vg_lite_matrix_t));
      }
    }
    /* Read in tmppath.length bytes long of path data */
    readres = lv_fs_read(fp, ret->evo_path_dsc[i].vpath.path, tmppath.length, NULL);
    if (readres != LV_FS_RES_OK) {
      LV_LOG_ERROR("read error");
      evo_clear(ret);
      return readres;
    }
    vg_lite_init_arc_path(&ret->evo_path_dsc[i].vpath, tmppath.format, tmppath.quality, tmppath.length, ret->evo_path_dsc[i].vpath.path, tmppath.bounds[0], tmppath.bounds[1], tmppath.bounds[2], tmppath.bounds[3]);
  }
  return 0;
}

/****************************************************************************
 * Name: evo_clear
 *
 * Description:
 *   Clears the evo_fcontent_s struct given by evo_read, will restore the ptr
 *   to null.
 *
 ****************************************************************************/
void evo_clear(evo_fcontent_t* content)
{
  if (content->evo_path_dsc) {
    for (int i = 0; i < content->pathcount; i++) {
      if (content->evo_path_dsc[i].lin_gradient) {
        if (content->evo_path_dsc[i].lin_gradient->image.memory)
          lv_mem_free(content->evo_path_dsc[i].lin_gradient->image.memory);
        lv_mem_free(content->evo_path_dsc[i].lin_gradient);
      }
      if (content->evo_path_dsc[i].rad_gradient) {
        lv_mem_free(content->evo_path_dsc[i].rad_gradient->image.memory);
        lv_mem_free(content->evo_path_dsc[i].rad_gradient);
      }
      if (content->evo_path_dsc[i].vpath.path) {
        lv_mem_free(content->evo_path_dsc[i].vpath.path);
      }
    }
    lv_mem_free(content->evo_path_dsc);
  }
}

/****************************************************************************
 * Name: evo_matmult
 *
 * Description:
 *    Implementation of a commonly used operation when displaying vector imgs
 *    loaded from .evo files. This is used to apply transformations (scaling,
 *    translation, rotaion, etc.) to path matrices.
 *    Equals: C = A x B
 *
 * Note:
 *    Extracted and modified from vglite.c.
 *
 ****************************************************************************/
void evo_matmult(const vg_lite_matrix_t* a, const vg_lite_matrix_t* b, vg_lite_matrix_t* c)
{
  vg_lite_matrix_t temp;
  int row, column;

  /* Process all rows. */
  for (row = 0; row < 3; row++) {
    /* Process all columns. */
    for (column = 0; column < 3; column++) {
      /* Compute matrix entry. */
      temp.m[row][column] = (a->m[row][0] * b->m[0][column]) + (a->m[row][1] * b->m[1][column]) + (a->m[row][2] * b->m[2][column]);
    }
  }

  /* Copy temporary matrix into result. */
  lv_memcpy(c, &temp, sizeof(temp));
}
