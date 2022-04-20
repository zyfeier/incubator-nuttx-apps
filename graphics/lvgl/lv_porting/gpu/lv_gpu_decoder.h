/****************************************************************************
 * apps/graphics/lvgl/lv_porting/gpu/lv_gpu_decoder.h
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

#ifndef __LV_GPU_DECODER_H__
#define __LV_GPU_DECODER_H__

/*********************
 *      INCLUDES
 *********************/
#include "lv_gpu_evoreader.h"
#include "lv_img_decoder.h"
#include "src/lv_conf_internal.h"
#include "vg_lite.h"
#include <lvgl/lvgl.h>

/*********************
 *      DEFINES
 *********************/
#define GPU_DATA_MAGIC 0x7615600D /* VGISGOOD:1981112333 */
#define EVO_DATA_MAGIC 0x23333333
#define CF_BUILT_IN_FIRST LV_IMG_CF_TRUE_COLOR
#define CF_BUILT_IN_LAST LV_IMG_CF_ALPHA_8BIT
#define LV_IMG_CF_EVO LV_IMG_CF_USER_ENCODED_0

/**********************
 *      TYPEDEFS
 **********************/
typedef struct {
  uint32_t magic;
  union {
    vg_lite_buffer_t vgbuf;
    evo_fcontent_t evocontent;
  };
} __attribute__((aligned(8))) gpu_data_header_t;

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
 * Name: lv_gpu_decoder_init
 *
 * Description:
 *   Initialize the image decoder module
 *
 * @return None
 *
 ****************************************************************************/

void lv_gpu_decoder_init(void);

/****************************************************************************
 * Name: lv_gpu_decoder_info
 *
 * Description:
 *   Get info about a gpu-supported image
 *
 * @param decoder the decoder where this function belongs
 * @param src the image source: pointer to an `lv_img_dsc_t` variable, a file
 *   path or a symbol
 * @param header store the image data here
 *
 * @return LV_RES_OK: the info is successfully stored in `header`;
 *         LV_RES_INV: unknown format or other error.
 *
 ****************************************************************************/

lv_res_t lv_gpu_decoder_info(lv_img_decoder_t* decoder, const void* src,
    lv_img_header_t* header);

/****************************************************************************
 * Name: lv_gpu_decoder_open
 *
 * Description:
 *   Open an image for GPU rendering (aligning to 16px and pre-multiplying
 *   alpha channel)
 *
 * @param decoder the decoder where this function belongs
 * @param dsc pointer to decoder descriptor. `src`, `style` are already
 *   initialized in it.
 *
 * @return LV_RES_OK: the info is successfully stored in `header`;
 *         LV_RES_INV: unknown format or other error.
 *
 ****************************************************************************/

lv_res_t lv_gpu_decoder_open(lv_img_decoder_t* decoder,
    lv_img_decoder_dsc_t* dsc);

/****************************************************************************
 * Name: lv_gpu_decoder_close
 *
 * Description:
 *   Close the pending decoding. Free resources etc.
 *
 * @param decoder pointer to the decoder the function associated with
 * @param dsc pointer to decoder descriptor
 *
 * @return None
 *
 ****************************************************************************/

void lv_gpu_decoder_close(lv_img_decoder_t* decoder,
    lv_img_decoder_dsc_t* dsc);

/****************************************************************************
 * Name: lv_gpu_load_vgbuf
 *
 * Description:
 *   Load an image into vg_lite buffer with automatic alignment. Appropriate
 *   room will be allocated in vgbuf_p->memory, leaving the user responsible
 *   for cleaning it up.
 *
 * @param img_data pointer to the pixel buffer
 * @param img_header header of the image containing width, height and color
 *   format
 * @param vgbuf_p address of the vg_lite_buffer_t structure to be initialized
 * @param buf_p buffer address to be used as vgbuf.memory, will allocate a
 *   new buffer if buf_p == NULL
 * @param recolor recolor (ARGB) to apply. recolor_opa is in the A channel
 *
 * @return LV_RES_OK: ok; LV_RES_INV: failed
 *
 ****************************************************************************/
LV_ATTRIBUTE_FAST_MEM lv_res_t lv_gpu_load_vgbuf(const uint8_t* img_data,
    lv_img_header_t* header, vg_lite_buffer_t* vgbuf_p, uint8_t* buf_p,
    lv_color32_t recolor);

/****************************************************************************
 * Name: lv_gpu_get_vgbuf
 *
 * Description:
 * Get the vgbuf cache corresponding to the image pointer (if available).
 *
 * @param ptr pointer to the pixel buffer
 *
 * @return pointer to the vg_lite_buffer_t structure in cache items, NULL if
 *   cache miss
 *
 ****************************************************************************/

vg_lite_buffer_t* lv_gpu_get_vgbuf(void* data);

#undef EXTERN
#ifdef __cplusplus
}
#endif

#endif /* __LV_GPU_DECODER_H__ */
