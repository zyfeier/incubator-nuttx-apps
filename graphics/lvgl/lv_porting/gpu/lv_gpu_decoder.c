/****************************************************************************
 * apps/graphics/lvgl/lv_porting/gpu/lv_gpu_decoder.c
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

/*********************
 *      INCLUDES
 *********************/

#include "lv_gpu_decoder.h"
#include "lv_assert.h"
#include "lv_color.h"
#include "lv_draw_img.h"
#include "lv_gpu_draw_utils.h"
#include "lv_gpu_evoreader.h"
#include "lv_img_decoder.h"
#include "lv_porting/lv_gpu_interface.h"
#include <stdio.h>
#include <stdlib.h>

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/

LV_ATTRIBUTE_FAST_MEM static lv_res_t decode_rgb(lv_img_decoder_t* decoder,
    lv_img_decoder_dsc_t* dsc);
LV_ATTRIBUTE_FAST_MEM static lv_res_t decode_indexed(lv_img_decoder_t* decoder,
    lv_img_decoder_dsc_t* dsc);
LV_ATTRIBUTE_FAST_MEM static lv_res_t decode_evo(lv_img_decoder_t* decoder,
    lv_img_decoder_dsc_t* dsc);

/**********************
 *   STATIC FUNCTIONS
 **********************/

static inline uint32_t bit_rev8(uint32_t x)
{
  uint32_t y = 0;
  asm("rbit %0, %1"
      : "=r"(y)
      : "r"(x));
  asm("rev %0, %1"
      : "=r"(y)
      : "r"(y));
  return y;
}

static void bit_rev(uint8_t px_size, uint8_t* buf, uint32_t stride)
{
  switch (px_size) {
  case 1:
    for (int_fast16_t i = 0; i < stride >> 2; i++) {
      ((uint32_t*)buf)[i] = bit_rev8(((uint32_t*)buf)[i]);
    }
    uint8_t tail = stride & 3;
    if (tail) {
      uint32_t r = bit_rev8(((uint32_t*)buf)[stride >> 2]);
      for (uint8_t i = 0; i < tail; i++) {
        buf[stride - tail + i] = r >> (i << 3) & 0xFF;
      }
    }
    break;
  case 2:
    for (int_fast16_t i = 0; i < stride; i++) {
      buf[i] = buf[i] << 6 | (buf[i] << 2 & 0x30)
          | (buf[i] >> 2 & 0x0C) | buf[i] >> 6;
    }
    break;
  case 4:
    for (int_fast16_t i = 0; i < stride; i++) {
      buf[i] = buf[i] << 4 | buf[i] >> 4;
    }
    break;
  case 8:
  default:
    break;
  }
}

LV_ATTRIBUTE_FAST_MEM static lv_res_t decode_rgb(lv_img_decoder_t* decoder,
    lv_img_decoder_dsc_t* dsc)
{
  const uint8_t* img_data = NULL; /* points to the input image */
  lv_fs_file_t f;
  uint8_t* fs_buf = NULL;
  uint32_t data_size;
  bool no_processing = dsc->header.cf == LV_IMG_CF_TRUE_COLOR
      && IS_ALIGNED(dsc->header.w, 16);
  /*Open the file if it's a file*/
  if (dsc->src_type == LV_IMG_SRC_FILE) {
    GPU_WARN("opening %s", (const char*)dsc->src);
    const char* ext = lv_fs_get_ext(dsc->src);
    /*Support only "*.bin" files*/
    if (strcmp(ext, "bin") != 0) {
      GPU_WARN("can't open %s", (const char*)dsc->src);
      return LV_RES_INV;
    }

    lv_fs_res_t res = lv_fs_open(&f, dsc->src, LV_FS_MODE_RD);
    if (res != LV_FS_RES_OK) {
      GPU_WARN("gpu_decoder can't open the file");
      return LV_RES_INV;
    }

    data_size = lv_img_cf_get_px_size(dsc->header.cf);
    data_size *= dsc->header.w * dsc->header.h;
    data_size >>= 3; /* bits to bytes */
    if (!no_processing) {
      fs_buf = lv_mem_alloc(data_size);
      if (fs_buf == NULL) {
        GPU_ERROR("out of memory");
        lv_fs_close(&f);
        return LV_RES_INV;
      }

      /*Skip the header*/
      lv_fs_seek(&f, 4, LV_FS_SEEK_SET);
      res = lv_fs_read(&f, fs_buf, data_size, NULL);
      lv_fs_close(&f);
      if (res != LV_FS_RES_OK) {
        lv_mem_free(fs_buf);
        GPU_ERROR("file read failed");
        return LV_RES_INV;
      }

      img_data = fs_buf;
    }
  } else if (dsc->src_type == LV_IMG_SRC_VARIABLE) {
    const lv_img_dsc_t* img_dsc = dsc->src;
    /*The variables should have valid data*/
    if (img_dsc->data == NULL) {
      GPU_WARN("no data");
      return LV_RES_INV;
    }
    img_data = img_dsc->data;
    data_size = img_dsc->data_size;
  } else {
    /* No way to get the image data, return invalid */
    return LV_RES_INV;
  }

  /* alloc new buffer that meets GPU requirements(width, alignment) */
  uint8_t* gpu_data;
  gpu_data = gpu_img_alloc(dsc->header.w, dsc->header.h, dsc->header.cf, NULL);
  if (gpu_data == NULL) {
    GPU_ERROR("out of memory");
    if (fs_buf) {
      /* release file cache */
      lv_mem_free(fs_buf);
    }
    return LV_RES_INV;
  }

  dsc->user_data = (void*)GPU_DATA_MAGIC;
  /* add gpu header right at beginning of gpu image buffer */
  gpu_data_header_t* header = (gpu_data_header_t*)gpu_data;
  header->magic = GPU_DATA_MAGIC;
  header->recolor = dsc->color.full;
  dsc->img_data = gpu_data;
  lv_res_t ret = LV_RES_OK;

  uint8_t* gpu_data_buf = gpu_data + sizeof(gpu_data_header_t);
  if (dsc->src_type == LV_IMG_SRC_FILE && no_processing) {
    lv_fs_seek(&f, 4, LV_FS_SEEK_SET);
    lv_fs_res_t res = lv_fs_read(&f, gpu_data_buf, data_size, NULL);
    lv_fs_close(&f);
    if (res != LV_FS_RES_OK) {
      lv_mem_free(gpu_data);
      GPU_ERROR("file read failed");
      return LV_RES_INV;
    }
    init_vg_buf(&header->vgbuf, dsc->header.w, dsc->header.h,
        dsc->header.w * sizeof(lv_color_t), gpu_data_buf, VGLITE_PX_FMT, true);
  } else {
    ret = lv_gpu_load_vgbuf(img_data, &dsc->header, &header->vgbuf,
        gpu_data_buf, dsc->color, false);
  }

  if (fs_buf) {
    /* file cache is no longger needed. */
    lv_mem_free(fs_buf);
  }

  return ret;
}

LV_ATTRIBUTE_FAST_MEM static lv_res_t decode_indexed(lv_img_decoder_t* decoder,
    lv_img_decoder_dsc_t* dsc)
{
  lv_fs_file_t f;
  lv_img_cf_t cf = dsc->header.cf;
  const lv_img_dsc_t* img_dsc = dsc->src;
  /*Open the file if it's a file*/
  if (dsc->src_type == LV_IMG_SRC_FILE) {
    /*Support only "*.bin" files*/
    GPU_WARN("opening %s", (const char*)dsc->src);
    if (strcmp(lv_fs_get_ext(dsc->src), "bin")) {
      GPU_WARN("can't open %s", (const char*)dsc->src);
      return LV_RES_INV;
    }

    lv_fs_res_t res = lv_fs_open(&f, dsc->src, LV_FS_MODE_RD);
    if (res != LV_FS_RES_OK) {
      GPU_WARN("gpu_decoder can't open the file");
      return LV_RES_INV;
    }
    /*Skip the header*/
    lv_fs_seek(&f, 4, LV_FS_SEEK_SET);
  } else if (dsc->src_type == LV_IMG_SRC_VARIABLE) {
    /*The variables should have valid data*/
    if (img_dsc->data == NULL) {
      GPU_WARN("no data");
      return LV_RES_INV;
    }
  }

  uint8_t* gpu_data;
  gpu_data = gpu_img_alloc(dsc->header.w, dsc->header.h, dsc->header.cf, NULL);
  if (gpu_data == NULL) {
    GPU_ERROR("out of memory");
    return LV_RES_INV;
  }

  dsc->user_data = (void*)GPU_DATA_MAGIC;
  gpu_data_header_t* header = (gpu_data_header_t*)gpu_data;
  header->magic = GPU_DATA_MAGIC;
  header->recolor = dsc->color.full;
  dsc->img_data = gpu_data;

  bool indexed = (cf >= LV_IMG_CF_INDEXED_1BIT && cf <= LV_IMG_CF_INDEXED_8BIT);
  uint8_t px_size = lv_img_cf_get_px_size(dsc->header.cf);
  int32_t img_w = dsc->header.w;
  int32_t img_h = dsc->header.h;
  int32_t vgbuf_w = (cf == LV_IMG_CF_INDEXED_1BIT) ? ALIGN_UP(img_w, 64)
      : (cf == LV_IMG_CF_INDEXED_2BIT)             ? ALIGN_UP(img_w, 32)
                                                   : ALIGN_UP(img_w, 16);
  uint8_t vgbuf_format = indexed ? BPP_TO_VG_FMT(px_size)
      : (px_size == 4)           ? VG_LITE_A4
                                 : VG_LITE_A8;
  int32_t vgbuf_stride = vgbuf_w * px_size >> 3;
  int32_t map_stride = (img_w * px_size + 7) >> 3;
  uint32_t vgbuf_data_size = vgbuf_stride * img_h;
  uint32_t palette_size = 1 << px_size;
  uint32_t* palette = (uint32_t*)(gpu_data + sizeof(gpu_data_header_t)
      + vgbuf_data_size);
  lv_color32_t* palette_p = NULL;
  if (indexed) {
    if (dsc->src_type == LV_IMG_SRC_FILE) {
      /*Read the palette from file*/
      lv_fs_res_t res = lv_fs_read(&f, palette,
          sizeof(lv_color32_t) * palette_size, NULL);
      if (res != LV_FS_RES_OK) {
        GPU_ERROR("file read failed");
        lv_fs_close(&f);
        lv_mem_free(gpu_data);
        return LV_RES_INV;
      }
      palette_p = (lv_color32_t*)palette;
    } else {
      /*The palette is in the beginning of the image data. Just point to it.*/
      palette_p = (lv_color32_t*)img_dsc->data;
    }
  }
  recolor_palette((lv_color32_t*)palette, palette_p, palette_size,
      dsc->color.full);

  vg_lite_buffer_t* vgbuf = &header->vgbuf;

  void* mem = gpu_data + sizeof(gpu_data_header_t);
  init_vg_buf(vgbuf, vgbuf_w, img_h, vgbuf_stride, mem, vgbuf_format,
      true);
  uint8_t* px_buf = vgbuf->memory;
  const uint8_t* px_map = img_dsc->data;
  if (indexed) {
    px_map += palette_size * sizeof(lv_color32_t);
  }

  uint32_t data_size = map_stride * img_h;
  if (map_stride == vgbuf_stride) {
    if (dsc->src_type == LV_IMG_SRC_FILE) {
      lv_fs_res_t res = lv_fs_read(&f, px_buf, data_size, NULL);
      lv_fs_close(&f);
      if (res != LV_FS_RES_OK) {
        lv_mem_free(gpu_data);
        GPU_ERROR("file read failed");
        return LV_RES_INV;
      }
    } else {
      lv_memcpy(px_buf, px_map, data_size);
    }
    bit_rev(px_size, px_buf, data_size);
  } else {
    uint8_t* fs_buf = NULL;
    if (dsc->src_type == LV_IMG_SRC_FILE) {
      fs_buf = lv_mem_alloc(data_size);
      if (fs_buf == NULL) {
        GPU_ERROR("out of memory");
        lv_fs_close(&f);
        lv_mem_free(gpu_data);
        return LV_RES_INV;
      }
      lv_fs_res_t res = lv_fs_read(&f, fs_buf, data_size, NULL);
      lv_fs_close(&f);
      if (res != LV_FS_RES_OK) {
        lv_mem_free(gpu_data);
        lv_mem_free(fs_buf);
        GPU_ERROR("file read failed");
        return LV_RES_INV;
      }
      px_map = fs_buf;
    }
    uint8_t zero_id = 0;
    while (palette[zero_id] && zero_id < palette_size) {
      zero_id++;
    }
    if (zero_id == palette_size) {
      zero_id = 0;
      if (map_stride < vgbuf_stride) {
        LV_LOG_ERROR("no transparent found in palette but padding required!");
      }
    }
    const uint8_t multiplier[4] = { 0xFF, 0x55, 0x11, 0x01 };
    uint8_t padding = zero_id * multiplier[__builtin_ctz(px_size)];
    for (int_fast16_t i = 0; i < img_h; i++) {
      lv_memcpy(px_buf, px_map, map_stride);
      lv_memset(px_buf + map_stride, padding, vgbuf_stride - map_stride);
      bit_rev(px_size, px_buf, map_stride);
      px_map += map_stride;
      px_buf += vgbuf_stride;
    }
    if (fs_buf) {
      lv_mem_free(fs_buf);
    }
  }
  return LV_RES_OK;
}

LV_ATTRIBUTE_FAST_MEM static lv_res_t decode_evo(lv_img_decoder_t* decoder,
    lv_img_decoder_dsc_t* dsc)
{
  lv_fs_file_t f;
  const uint8_t* img_data = NULL;
  const lv_img_dsc_t* img_dsc = dsc->src;
  /*Open the file if it's a file*/
  if (dsc->src_type == LV_IMG_SRC_FILE) {
    /*Support only "*.evo" files*/
    GPU_WARN("opening %s", (const char*)dsc->src);
    if (strcmp(lv_fs_get_ext(dsc->src), "evo")) {
      GPU_WARN("can't open %s", (const char*)dsc->src);
      return LV_RES_INV;
    }
    lv_fs_res_t res = lv_fs_open(&f, dsc->src, LV_FS_MODE_RD);
    if (res != LV_FS_RES_OK) {
      GPU_WARN("gpu_decoder can't open the file");
      return LV_RES_INV;
    }
    lv_fs_seek(&f, 4, LV_FS_SEEK_SET);

    gpu_data_header_t* gpu_data = lv_mem_alloc(sizeof(gpu_data_header_t));
    if (gpu_data == NULL) {
      GPU_ERROR("out of memory");
      lv_fs_close(&f);
      return LV_RES_INV;
    }
    gpu_data->magic = EVO_DATA_MAGIC;
    if (evo_read(&f, &gpu_data->evocontent) != LV_FS_RES_OK) {
      lv_mem_free(gpu_data);
      lv_fs_close(&f);
      GPU_ERROR("file read failed");
      return LV_RES_INV;
    }
    dsc->user_data = (void*)EVO_DATA_MAGIC;
    img_data = (const uint8_t*)gpu_data;
  } else if (dsc->src_type == LV_IMG_SRC_VARIABLE) {
    /*The variables should have valid data*/
    if (img_dsc->data == NULL) {
      GPU_WARN("no data");
      return LV_RES_INV;
    }
    img_data = img_dsc->data;
  }

  dsc->img_data = img_data;
  if (img_data) {
    if (dsc->src_type == LV_IMG_SRC_FILE) {
      lv_fs_close(&f);
    }
    return LV_RES_OK;
  } else {
    /*Unknown source. Can't decode it.*/
    GPU_WARN("Unknown source:%d w:%d h:%d @%p", dsc->src_type, dsc->header.w,
        dsc->header.h, dsc->img_data);
    return LV_RES_INV;
  }
}
/**********************
 *   GLOBAL FUNCTIONS
 **********************/

/****************************************************************************
 * Name: lv_gpu_decoder_init
 *
 * Description:
 *   Initialize the image decoder module
 *
 * @return None
 *
 ****************************************************************************/
void lv_gpu_decoder_init(void)
{
  lv_img_decoder_t* decoder = lv_img_decoder_create();
  if (decoder == NULL) {
    GPU_ERROR("lv_gpu_decoder_init: out of memory");
    return;
  }

  lv_img_decoder_set_info_cb(decoder, lv_gpu_decoder_info);
  lv_img_decoder_set_open_cb(decoder, lv_gpu_decoder_open);
  lv_img_decoder_set_close_cb(decoder, lv_gpu_decoder_close);
}

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
    lv_img_header_t* header)
{
  LV_UNUSED(decoder); /*Unused*/

  lv_img_src_t src_type = lv_img_src_get_type(src);
  if (src_type == LV_IMG_SRC_VARIABLE) {
    lv_img_cf_t cf = ((lv_img_dsc_t*)src)->header.cf;
    if (cf < CF_BUILT_IN_FIRST || cf > CF_BUILT_IN_LAST)
      return LV_RES_INV;

    header->w = ((lv_img_dsc_t*)src)->header.w;
    header->h = ((lv_img_dsc_t*)src)->header.h;
    header->cf = ((lv_img_dsc_t*)src)->header.cf;

    return LV_RES_OK;
  }

  if (src_type == LV_IMG_SRC_FILE) {
    /*Support only "*.bin", ".evo" and ".gpu" files*/
    if (strcmp(lv_fs_get_ext(src), "bin")
        & strcmp(lv_fs_get_ext(src), "evo")
        & strcmp(lv_fs_get_ext(src), "gpu"))
      return LV_RES_INV;

    lv_fs_file_t f;
    lv_fs_res_t res = lv_fs_open(&f, src, LV_FS_MODE_RD);
    if (res != LV_FS_RES_OK) {
      LV_LOG_INFO("GPU decoder open %s failed", (const char*)src);
      return LV_RES_INV;
    }

    uint32_t rn;
    res = lv_fs_read(&f, header, sizeof(lv_img_header_t), &rn);
    lv_fs_close(&f);
    if (res != LV_FS_RES_OK || rn != sizeof(lv_img_header_t)) {
      LV_LOG_WARN("Image get info read file header failed");
      return LV_RES_INV;
    }

    if (header->cf < CF_BUILT_IN_FIRST || header->cf > CF_BUILT_IN_LAST) {
      if (header->cf != LV_IMG_CF_EVO)
        return LV_RES_INV;
    }

    return LV_RES_OK;
  }

  if (src_type == LV_IMG_SRC_SYMBOL) {
    /* The size depend on the font but it is unknown here. It should be handled
     * outside of the function */
    header->w = 1;
    header->h = 1;
    /* Symbols always have transparent parts. Important because of cover check
     * in the draw function. The actual value doesn't matter because
     * lv_draw_label will draw it */
    header->cf = LV_IMG_CF_ALPHA_1BIT;
    return LV_RES_OK;
  }

  LV_LOG_WARN("Image get info found unknown src type");
  return LV_RES_INV;
}

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
    lv_img_decoder_dsc_t* dsc)
{
  lv_img_cf_t cf = dsc->header.cf;
  const lv_img_dsc_t* img_dsc = dsc->src;

  /* check if it's already decoded, if so, return directly */
  if (dsc->src_type == LV_IMG_SRC_VARIABLE) {
    gpu_data_header_t* header = (gpu_data_header_t*)img_dsc->data;
    if (header->magic == GPU_DATA_MAGIC || header->magic == EVO_DATA_MAGIC) {
      /* already decoded, just pass the pointer */
      GPU_INFO("%lx already decoded %p @ %p", header->magic,
          header->vgbuf.memory, header);
      dsc->img_data = img_dsc->data;
      dsc->user_data = NULL;
      return LV_RES_OK;
    }
  } else if (dsc->src_type == LV_IMG_SRC_FILE) {
    /* let's process "gpu" file firstly. */
    const char* ext = lv_fs_get_ext(dsc->src);
    if (strcmp(ext, "gpu") == 0) {
      /* No need to decode gpu file, simply load it to ram */
      lv_fs_file_t f;
      lv_fs_res_t res = lv_fs_open(&f, dsc->src, LV_FS_MODE_RD);
      if (res != LV_FS_RES_OK) {
        GPU_WARN("gpu_decoder can't open the file");
        return LV_RES_INV;
      }

      /* alloc new buffer that meets GPU requirements(width, alignment) */
      lv_img_dsc_t* gpu_dsc = gpu_img_buf_alloc(dsc->header.w, dsc->header.h, dsc->header.cf);
      if (gpu_dsc == NULL) {
        GPU_ERROR("out of memory");
        lv_fs_close(&f);
        return LV_RES_INV;
      }
      uint8_t* gpu_data = (uint8_t*)gpu_dsc->data;

      lv_fs_seek(&f, 4, LV_FS_SEEK_SET); /* skip file header. */
      res = lv_fs_read(&f, gpu_data, gpu_dsc->data_size, NULL);
      lv_fs_close(&f);
      if (res != LV_FS_RES_OK) {
        gpu_img_free(gpu_data);
        lv_mem_free(gpu_dsc);
        GPU_ERROR("file read failed");
        return LV_RES_INV;
      }
      dsc->img_data = gpu_data;
      dsc->user_data = (void*)GPU_DATA_MAGIC;

      gpu_data_update(gpu_dsc);
      lv_mem_free(gpu_dsc);

      return LV_RES_OK;
    }
  }

  /*GPU hasn't processed, decode now. */

  /*Process true color formats*/
  if (cf == LV_IMG_CF_TRUE_COLOR || cf == LV_IMG_CF_TRUE_COLOR_ALPHA
      || cf == LV_IMG_CF_TRUE_COLOR_CHROMA_KEYED) {
    lv_res_t ret = decode_rgb(decoder, dsc);
    return ret;
  }

  if ((cf >= LV_IMG_CF_INDEXED_1BIT && cf <= LV_IMG_CF_INDEXED_8BIT)
      || cf == LV_IMG_CF_ALPHA_8BIT || cf == LV_IMG_CF_ALPHA_4BIT) {
    lv_res_t ret = decode_indexed(decoder, dsc);
    return ret;
  }

  if (cf == LV_IMG_CF_EVO) {
    lv_res_t ret = decode_evo(decoder, dsc);
    return ret;
  }

  /*Unknown format. Can't decode it.*/
  GPU_WARN("GPU decoder open: unsupported color format %d", cf);
  return LV_RES_INV;
}

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
void lv_gpu_decoder_close(lv_img_decoder_t* decoder, lv_img_decoder_dsc_t* dsc)
{
  LV_UNUSED(decoder); /*Unused*/
  if (dsc->img_data == NULL)
    return;

  if ((uint32_t)dsc->user_data == GPU_DATA_MAGIC) {
    gpu_img_free((void*)dsc->img_data);
  } else if ((uint32_t)dsc->user_data == EVO_DATA_MAGIC) {
    evo_clear(&((gpu_data_header_t*)dsc->img_data)->evocontent);
    lv_mem_free((void*)dsc->img_data);
  }

  dsc->img_data = NULL;
}

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
 * @param preprocessed the source has been pre-multiplied and aligned to 16px.
 *   (used in draw_img only)
 *
 * @return LV_RES_OK: ok; LV_RES_INV: failed
 *
 ****************************************************************************/
LV_ATTRIBUTE_FAST_MEM lv_res_t lv_gpu_load_vgbuf(const uint8_t* img_data,
    lv_img_header_t* header, vg_lite_buffer_t* vgbuf_p, uint8_t* buf_p,
    lv_color32_t recolor, bool preprocessed)
{
  vg_lite_buffer_t vgbuf;

  void* mem = buf_p;
  uint32_t vgbuf_w = ALIGN_UP(header->w, 16);
  uint32_t vgbuf_stride = vgbuf_w * sizeof(lv_color_t);
  uint8_t vgbuf_format = VGLITE_PX_FMT;
  uint32_t map_stride = header->w * sizeof(lv_color_t);
#if LV_COLOR_DEPTH == 16
  if (header->cf == LV_IMG_CF_TRUE_COLOR_ALPHA
      || header->cf == LV_IMG_CF_TRUE_COLOR_CHROMA_KEYED) {
    vgbuf_stride = vgbuf_w * sizeof(lv_color32_t);
    vgbuf_format = VG_LITE_BGRA8888;
    map_stride = header->w * LV_IMG_PX_SIZE_ALPHA_BYTE;
  }
  if (preprocessed) {
    map_stride = vgbuf_stride;
  }
#endif
  uint32_t vgbuf_size = header->h * vgbuf_stride;
  if (mem == NULL) {
    mem = gpu_heap_aligned_alloc(8, vgbuf_size);
  }
  if (mem == NULL) {
    GPU_WARN("Insufficient memory for GPU 16px aligned image cache");
    return LV_RES_INV;
  }
  init_vg_buf(&vgbuf, vgbuf_w, header->h, vgbuf_stride, mem,
      vgbuf_format, true);
  uint8_t* px_buf = vgbuf.memory;
  const uint8_t* px_map = img_data;

  if (preprocessed) {
    /* must be recolored */
    if (header->cf == LV_IMG_CF_TRUE_COLOR) {
#if LV_COLOR_DEPTH == 16
      convert_rgb565_to_gpu(px_buf, vgbuf_stride, px_map, map_stride, header, recolor, 0);
#elif LV_COLOR_DEPTH == 32
      convert_rgb888_to_gpu(px_buf, vgbuf_stride, px_map, map_stride, header, recolor, 0);
#endif /* LV_COLOR_DEPTH */
    } else {
      /* has alpha, already converted to argb8888 */
      convert_argb8888_to_gpu(px_buf, vgbuf_stride, px_map, map_stride, header, recolor, true);
    }
  } else {
    /* regular decode */
    if (header->cf == LV_IMG_CF_TRUE_COLOR_ALPHA) {
#if LV_COLOR_DEPTH == 16
      convert_argb8565_to_8888(px_buf, vgbuf_stride, px_map, map_stride, header, recolor);
#elif LV_COLOR_DEPTH == 32
      convert_argb8888_to_gpu(px_buf, vgbuf_stride, px_map, map_stride, header, recolor, false);
#endif /* LV_COLOR_DEPTH */
    } else { /* LV_IMG_CF_TRUE_COLOR || LV_IMG_CF_TRUE_COLOR_CHROMA_KEYED */
      uint32_t ckey = (header->cf == LV_IMG_CF_TRUE_COLOR_CHROMA_KEYED)
          ? LV_COLOR_CHROMA_KEY.full
          : 0;
#if LV_COLOR_DEPTH == 16
      convert_rgb565_to_gpu(px_buf, vgbuf_stride, px_map, map_stride, header, recolor, ckey);
#elif LV_COLOR_DEPTH == 32
      convert_rgb888_to_gpu(px_buf, vgbuf_stride, px_map, map_stride, header, recolor, ckey);
#endif /* LV_COLOR_DEPTH */
    }
  }
  /*Save vglite buffer info*/
  lv_memcpy_small(vgbuf_p, &vgbuf, sizeof(vgbuf));
  return LV_RES_OK;
}

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
LV_ATTRIBUTE_FAST_MEM vg_lite_buffer_t* lv_gpu_get_vgbuf(void* data)
{
  gpu_data_header_t* header = data;
  return header->magic == GPU_DATA_MAGIC ? &header->vgbuf : NULL;
}
