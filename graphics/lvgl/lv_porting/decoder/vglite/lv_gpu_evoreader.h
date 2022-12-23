/****************************************************************************
 * apps/graphics/lvgl/lv_porting/gpu/lv_gpu_evoreader.h
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

#ifndef __LV_GPU_EVOREADER_H__
#define __LV_GPU_EVOREADER_H__

/****************************************************************************
 * Included Files
 ****************************************************************************/
#include "vg_lite.h"
#include <lvgl/lvgl.h>

/****************************************************************************
 * Public Types
 ****************************************************************************/
typedef struct evo_path_dsc_s {
  /* Denotes whether corresponding path is radial, linear, or non-gradient.
   * positive numbers stands for linear gradient, negative for radial, and
   * zero for non-gradient. */
  int path_type;
  /* The gradient data of path [i] could be accessed by using
   * lin_gradient[path_type[i]] IFF it has a lin gradient. e.g. path_type>0 */
  vg_lite_linear_gradient_t* lin_gradient;
  /* The gradient data of path [i] could be accessed by using
   * rad_gradient[path_type[i]] IFF it has a rad gradient. e.g. path_type<0 */
  vg_lite_radial_gradient_t* rad_gradient;
  uint32_t fill_rule;
  uint32_t blending_mode;
  uint32_t color;
  /* Transformation matrix for the corresponding path stored in paths. */
  vg_lite_matrix_t pathtransform;
  vg_lite_path_t vpath;
} evo_path_dsc_t;

// Holds all information within an .evo file.
typedef struct evo_fcontent_s {
  int pathcount;
  /* A transformation matrix to be applied to ALL paths within this file.
   * note that gradient matrices should NOT be multiplied with this. */
  vg_lite_matrix_t transform;
  evo_path_dsc_t* evo_path_dsc;
} evo_fcontent_t;

/****************************************************************************
 * Public function prototypes
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

lv_fs_res_t evo_read(lv_fs_file_t* fp, evo_fcontent_t* ret);

/****************************************************************************
 * Name: evo_clear
 *
 * Description:
 *   Clears the evo_fcontent_s struct given by evo_read, will restore the ptr
 *   to null.
 *
 ****************************************************************************/

void evo_clear(evo_fcontent_t* content);

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
void evo_matmult(const vg_lite_matrix_t* a, const vg_lite_matrix_t* b,
    vg_lite_matrix_t* c);

#endif /* __LV_GPU_EVOREADER_H__ */
