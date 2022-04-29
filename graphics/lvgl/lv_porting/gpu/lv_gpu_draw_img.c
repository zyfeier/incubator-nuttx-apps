/****************************************************************************
 * apps/graphics/lvgl/lv_porting/gpu/lv_gpu_draw_img.c
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

#include "lv_color.h"
#include "lv_gpu_decoder.h"
#include "lv_gpu_draw_utils.h"
#include "lv_porting/lv_gpu_interface.h"
#include "vg_lite.h"
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

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const vg_lite_matrix_t imat = {
  .m = {
      { 1.0f, 0.0f, 0.0f },
      { 0.0f, 1.0f, 0.0f },
      { 0.0f, 0.0f, 1.0f } }
};

static int16_t rect_path[] = {
  VLC_OP_MOVE, 0, 0,
  VLC_OP_LINE, 0, 0,
  VLC_OP_LINE, 0, 0,
  VLC_OP_LINE, 0, 0,
  VLC_OP_END
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

LV_ATTRIBUTE_FAST_MEM static void recolor_palette(uint32_t* dst,
    const uint32_t* src, uint16_t size, uint32_t recolor, lv_opa_t opa)
{
#ifdef CONFIG_ARM_HAVE_MVE
  int32_t blkCnt = size;
  uint32_t* pwTarget = dst;
  uint32_t* phwSource = (uint32_t*)src;
  if (src != NULL) {
    __asm volatile(
        "   .p2align 2                                                  \n"
        "   vdup.32                 q0, %[recolor]                      \n"
        "   vdup.8                  q1, %[opa]                          \n"
        "   vrmulh.u8               q0, q0, q1                          \n"
        "   vneg.s8                 q1, q1                              \n"
        "   wlstp.32                lr, %[loopCnt], 1f                  \n"
        "   2:                                                          \n"
        "   vldrw.32                q2, [%[pSource]], #16               \n"
        "   vsri.32                 q3, q2, #8                          \n"
        "   vsri.32                 q3, q2, #16                         \n"
        "   vsri.32                 q3, q2, #24                         \n"
        "   vrmulh.u8               q2, q2, q1                          \n"
        "   vadd.i8                 q2, q2, q0                          \n"
        "   vsli.32                 q2, q3, #24                         \n"
        "   vstrw.32                q2, [%[pTarget]], #16               \n"
        "   letp                    lr, 2b                              \n"
        "   1:                                                          \n"
        : [pSource] "+r"(phwSource), [pTarget] "+r"(pwTarget),
        [recolor] "+r"(recolor)
        : [loopCnt] "r"(blkCnt), [opa] "r"(opa)
        : "q0", "q1", "q2", "q3", "lr", "memory");
  } else {
    uint32_t inits[4] = { 0x0, 0x1010101, 0x2020202, 0x3030303 };
    uint32_t step = 0x4;
    if (size == 16) {
      step = 0x44;
      inits[1] = 0x11111111;
      inits[2] = 0x22222222;
      inits[3] = 0x33333333;
    }
    __asm volatile(
        "   .p2align 2                                                  \n"
        "   vdup.32                 q0, %[recolor]                      \n"
        "   vldrw.32                q1, [%[pInit]]                      \n"
        "   wlstp.32                lr, %[loopCnt], 1f                  \n"
        "   2:                                                          \n"
        "   vrmulh.u8               q2, q0, q1                          \n"
        "   vstrw.32                q2, [%[pTarget]], #16               \n"
        "   vadd.i8                 q1, q1, %[step]                     \n"
        "   letp                    lr, 2b                              \n"
        "   1:                                                          \n"
        : [recolor] "+r"(recolor), [pTarget] "+r"(pwTarget)
        : [loopCnt] "r"(blkCnt), [pInit] "r"(inits), [step] "r"(step)
        : "q0", "q1", "q2", "lr", "memory");
  }
#else
  uint16_t recolor_premult[3] = { (recolor >> 16 & 0xFF) * opa,
    (recolor >> 8 & 0xFF) * opa, (recolor & 0xFF) * opa };
  lv_opa_t mix = 255 - opa;
  for (int_fast16_t i = 0; i < size; i++) {
    if (src != NULL) {
      /* index recolor */
      if (src[i] >> 24 == 0) {
        dst[i] = 0;
      } else {
        uint8_t src_coeff = (mix << 8) / (src[i] >> 24);
        dst[i] = 0xFF000000 | /* A */
            (((recolor_premult[0] << 8) + ((src[i] & 0xFF0000) * src_coeff >> 8)) & 0xFF0000) | /* R */
            ((recolor_premult[1] + ((src[i] & 0xFF00) * src_coeff >> 8)) & 0xFF00) | /* G */
            (recolor_premult[2] + (src[i] & 0xFF) * src_coeff) >> 8; /* B */
      }
    } else {
      /* fill alpha palette */
      uint32_t opa_i = (size == 256) ? i : i * 0x11;
      dst[i] = opa_i << 24 | /* A */
          ((uint32_t)recolor_premult[0] * opa_i & 0xFF0000) | /* R */
          ((uint32_t)recolor_premult[1] * opa_i & 0xFF0000) >> 8 | /* G */
          ((uint32_t)recolor_premult[2] * opa_i & 0xFF0000) >> 16; /* B */
    }
  }
#endif
}

LV_ATTRIBUTE_FAST_MEM static void fill_rect_path(int16_t* path, lv_area_t* area)
{
  path[1] = path[4] = area->x1;
  path[7] = path[10] = area->x2 + 1;
  path[2] = path[11] = area->y1;
  path[5] = path[8] = area->y2 + 1;
}

/****************************************************************************
 * Global Functions
 ****************************************************************************/

/****************************************************************************
 * Name: lv_draw_image_decoded_gpu
 *
 * Description:
 *   Copy a transformed map (image) to a display buffer.
 *
 * Input Parameters:
 * @param draw_ctx draw context (refer to LVGL 8.2 changelog)
 * @param dsc draw image description
 * @param coords area of the image  (absolute coordinates)
 * @param map_p pointer to GPU data struct (if decoded by GPU decoder) or
 *              pointer to an image buf (opened by other decoder)
 * @param color_format image format
 *
 * Returned Value:
 * @return LV_RES_OK on success, LV_RES_INV on failure.
 *
 ****************************************************************************/
LV_ATTRIBUTE_FAST_MEM lv_res_t lv_draw_img_decoded_gpu(
    lv_draw_ctx_t* draw_ctx, const lv_draw_img_dsc_t* dsc,
    const lv_area_t* coords, const uint8_t* map_p, lv_img_cf_t color_format)
{
  lv_opa_t opa = dsc->opa;
  if (opa < LV_OPA_MIN) {
    return LV_RES_OK;
  }

  uint16_t angle = dsc->angle;
  uint16_t zoom = dsc->zoom;
  lv_point_t pivot = dsc->pivot;
  lv_blend_mode_t blend_mode = dsc->blend_mode;
  lv_color_t recolor = dsc->recolor;
  lv_opa_t recolor_opa = dsc->recolor_opa;
  vg_lite_buffer_t src_vgbuf;
  bool transformed = (angle != 0) || (zoom != LV_IMG_ZOOM_NONE);
  const lv_area_t* disp_area = draw_ctx->buf_area;
  lv_color_t* disp_buf = draw_ctx->buf;
  int32_t disp_w = lv_area_get_width(disp_area);
  int32_t disp_h = lv_area_get_height(disp_area);
  int32_t map_w = lv_area_get_width(coords);
  int32_t map_h = lv_area_get_height(coords);
  lv_area_t coords_rel;
  lv_area_copy(&coords_rel, coords);
  lv_area_move(&coords_rel, -disp_area->x1, -disp_area->y1);
  vg_lite_buffer_t dst_vgbuf;
  vg_lite_buffer_t* vgbuf;
  vg_lite_error_t vgerr = VG_LITE_SUCCESS;

  LV_ASSERT(init_vg_buf(&dst_vgbuf, disp_w, disp_h,
                disp_w * sizeof(lv_color_t), disp_buf, VGLITE_PX_FMT, false)
      == LV_RES_OK);

  if (*(uint32_t*)map_p == EVO_DATA_MAGIC) {
    evo_fcontent_t* evocontent = &((gpu_data_header_t*)map_p)->evocontent;
    GPU_WARN("evo file w/ %d paths", evocontent->pathcount);
    vg_lite_matrix_t tf;
    gpu_set_tf(&tf, dsc, &coords_rel);
    vg_lite_matrix_t tf0, tfi;
    evo_matmult(&tf, &evocontent->transform, &tf0);
    for (int i = 0; i < evocontent->pathcount; i++) {
      evo_path_dsc_t* dsci = &evocontent->evo_path_dsc[i];
      evo_matmult(&tf0, &dsci->pathtransform, &tfi);
      if (dsci->path_type == 0) { /* Non-gradient */
        CHECK_ERROR(vg_lite_draw(&dst_vgbuf, &dsci->vpath, dsci->fill_rule,
            &tfi, dsci->blending_mode, dsci->color));
      } else if (dsci->path_type > 0) { /* Linear gradient */
        vg_lite_linear_gradient_t* grad = dsci->lin_gradient;
        vg_lite_matrix_t tmp;
        lv_memcpy(&tmp, &grad->matrix, sizeof(vg_lite_matrix_t));
        evo_matmult(&tf, &grad->matrix, &grad->matrix);
        CHECK_ERROR(vg_lite_draw_gradient(&dst_vgbuf, &dsci->vpath,
            dsci->fill_rule, &tfi, grad, dsci->blending_mode));
        lv_memcpy(&grad->matrix, &tmp, sizeof(vg_lite_matrix_t));
      } else { /* Radial gradient */
        CHECK_ERROR(vg_lite_draw_radial_gradient(&dst_vgbuf, &dsci->vpath,
            dsci->fill_rule, &dsci->pathtransform, dsci->rad_gradient,
            dsci->color, dsci->blending_mode, VG_LITE_FILTER_LINEAR));
      }
    }
    CHECK_ERROR(vg_lite_finish());
    return LV_RES_OK;
  }
  uint32_t rect[4] = { 0, 0 };
  lv_area_t map_tf, draw_area;
  lv_area_copy(&draw_area, draw_ctx->clip_area);
  lv_area_move(&draw_area, -disp_area->x1, -disp_area->y1);
  _lv_img_buf_get_transformed_area(&map_tf, map_w, map_h, angle, zoom, &pivot);
  lv_area_move(&map_tf, coords->x1, coords->y1);
  lv_area_move(&map_tf, -disp_area->x1, -disp_area->y1);
  if (_lv_area_intersect(&draw_area, &draw_area, &map_tf) == false) {
    return LV_RES_OK;
  }

  GPU_INFO("fmt:%d map_tf: (%d %d)[%d,%d] draw_area:(%d %d)[%d,%d] zoom:%d\n",
      color_format, map_tf.x1, map_tf.y1, lv_area_get_width(&map_tf),
      lv_area_get_height(&map_tf), draw_area.x1, draw_area.y1,
      lv_area_get_width(&draw_area), lv_area_get_height(&draw_area), zoom);

  bool indexed = false, alpha = false;
  bool allocated_src = false;
  vgbuf = lv_gpu_get_vgbuf((void*)map_p);
  if (vgbuf) {
    indexed = (vgbuf->format >= VG_LITE_INDEX_1)
        && (vgbuf->format <= VG_LITE_INDEX_8);
    alpha = (vgbuf->format == VG_LITE_A4) || (vgbuf->format == VG_LITE_A8);
    lv_memcpy_small(&src_vgbuf, vgbuf, sizeof(src_vgbuf));
  } else {
    vgbuf = NULL;
    GPU_WARN("allocating new vgbuf:(%ld,%ld)", map_w, map_h);
    lv_img_header_t header;
    header.w = map_w;
    header.h = map_h;
    header.cf = color_format;
    if (lv_gpu_load_vgbuf(map_p, &header, &src_vgbuf, NULL) != LV_RES_OK) {
      GPU_ERROR("load failed");
      goto Fallback;
    }
    allocated_src = true;
  }

  const uint32_t* palette = (const uint32_t*)(map_p + sizeof(gpu_data_header_t)
      + src_vgbuf.stride * src_vgbuf.height);

  vg_lite_matrix_t matrix;
  gpu_set_tf(&matrix, dsc, &coords_rel);
  rect[2] = map_w;
  rect[3] = map_h;
  lv_color32_t color;
  vg_lite_blend_t blend = LV_BLEND_MODE_TO_VG(blend_mode);
  if (opa >= LV_OPA_MAX) {
    color.full = 0x0;
    src_vgbuf.image_mode = VG_LITE_NORMAL_IMAGE_MODE;
  } else {
    color.full = opa;
    color.full |= color.full << 8;
    color.full |= color.full << 16;
    src_vgbuf.image_mode = VG_LITE_MULTIPLY_IMAGE_MODE;
  }

  vg_lite_filter_t filter = transformed ? VG_LITE_FILTER_BI_LINEAR
                                        : VG_LITE_FILTER_POINT;
  if (indexed || alpha) {
    uint8_t px_size = VG_FMT_TO_BPP(src_vgbuf.format);
    uint16_t palette_size = 1 << px_size;
    src_vgbuf.format = BPP_TO_VG_FMT(px_size);
    if (alpha || recolor_opa != LV_OPA_TRANSP) {
      uint32_t* palette_r = lv_mem_buf_get(palette_size * sizeof(lv_color32_t));
      if (palette_r == NULL) {
        goto Error_handler;
      }
      recolor_palette(palette_r, alpha ? NULL : palette, palette_size,
          recolor_opa != LV_OPA_TRANSP ? lv_color_to32(recolor) : *palette,
          recolor_opa);
      vg_lite_set_CLUT(palette_size, palette_r);
      lv_mem_buf_release(palette_r);
    } else {
      vg_lite_set_CLUT(palette_size, (uint32_t*)palette);
    }
  } else if (recolor_opa != LV_OPA_TRANSP) {
    /* ARGB recolor, unsupported by GPU */
    GPU_ERROR("ARGB recolor!");
    goto Error_handler;
  }
  fill_rect_path(rect_path, &draw_area);
  vg_lite_path_t vpath;
  lv_memset_00(&vpath, sizeof(vg_lite_path_t));
  CHECK_ERROR(vg_lite_init_path(&vpath, VG_LITE_S16, VG_LITE_HIGH,
      sizeof(rect_path), rect_path, draw_area.x1, draw_area.y1,
      draw_area.x2 + 1, draw_area.y2 + 1));
  bool masked = lv_gpu_draw_mask_apply_path(&vpath, &draw_area);
  if (!vpath.path) {
    GPU_WARN("draw img multiple mask unsupported");
    return LV_RES_INV;
  }
  if (masked) {
    /* masked, have to use draw_pattern */
    vpath.format = VG_LITE_FP32;
    goto Draw_pattern;
  } else if (_lv_area_is_in(&map_tf, &draw_area, 0)) {
    /* No clipping, simply blit */
    CHECK_ERROR(vg_lite_blit_rect(&dst_vgbuf, &src_vgbuf, rect, &matrix, blend,
        color.full, filter));
  } else if (!transformed && map_tf.x1 == draw_area.x1
      && map_tf.y1 == draw_area.y1) {
    /* Clipped from left top, use good old blit_rect */
    rect[2] = lv_area_get_width(&draw_area);
    rect[3] = lv_area_get_height(&draw_area);
    CHECK_ERROR(vg_lite_blit_rect(&dst_vgbuf, &src_vgbuf, rect, &matrix, blend,
        color.full, filter));
  } else {
  Draw_pattern:
    /* arbitrarily clipped, have to use draw_pattern */
    CHECK_ERROR(vg_lite_set_multiply_color(color.full));
    CHECK_ERROR(vg_lite_draw_pattern(&dst_vgbuf, &vpath, VG_LITE_FILL_EVEN_ODD,
        (vg_lite_matrix_t*)&imat, &src_vgbuf, &matrix, blend, 0, 0, filter));
  }

  CHECK_ERROR(vg_lite_finish());
  if (masked) {
    lv_mem_buf_release(vpath.path);
  }
  if (IS_CACHED(dst_vgbuf.memory)) {
    up_invalidate_dcache((uintptr_t)dst_vgbuf.memory,
        (uintptr_t)dst_vgbuf.memory + dst_vgbuf.height * dst_vgbuf.stride);
  }

Error_handler:
  if (allocated_src) {
    GPU_WARN("freeing allocated vgbuf:(%ld,%ld)@%p", src_vgbuf.width,
        src_vgbuf.height, src_vgbuf.memory);
    free(src_vgbuf.memory);
  }
  if (vgerr != VG_LITE_SUCCESS) {
    goto Fallback;
  }
  return LV_RES_OK;
Fallback:
  if (IS_ERROR(vgerr)) {
    GPU_ERROR("[%s: %d] failed.error type is %s\n", __func__, __LINE__,
        error_type[vgerr]);
  }
  /*Fall back to SW render in case of error*/
  GPU_ERROR("GPU blit failed. Fallback to SW.\n");
  return LV_RES_INV;
}
