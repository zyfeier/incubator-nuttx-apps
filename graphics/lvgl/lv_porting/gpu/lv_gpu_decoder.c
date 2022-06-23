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

LV_ATTRIBUTE_FAST_MEM static void pre_multiply(lv_color32_t* dp,
    const lv_color32_t* sp);
LV_ATTRIBUTE_FAST_MEM static void bgra5658_to_8888(const uint8_t* src,
    uint32_t* dst);
LV_ATTRIBUTE_FAST_MEM static lv_res_t decode_rgb(lv_img_decoder_t* decoder,
    lv_img_decoder_dsc_t* dsc);
LV_ATTRIBUTE_FAST_MEM static lv_res_t decode_indexed(lv_img_decoder_t* decoder,
    lv_img_decoder_dsc_t* dsc);
LV_ATTRIBUTE_FAST_MEM static lv_res_t decode_evo(lv_img_decoder_t* decoder,
    lv_img_decoder_dsc_t* dsc);

/**********************
 *   STATIC FUNCTIONS
 **********************/

LV_ATTRIBUTE_FAST_MEM static void pre_multiply(lv_color32_t* dp,
    const lv_color32_t* sp)
{
  if (sp->ch.alpha == 0) {
    dp->full = 0;
    return;
  }
  if (sp->ch.alpha == 0xFF) {
    dp->full = sp->full;
    return;
  }
  dp->ch.alpha = sp->ch.alpha;
  dp->ch.red = LV_UDIV255(sp->ch.red * sp->ch.alpha);
  dp->ch.green = LV_UDIV255(sp->ch.green * sp->ch.alpha);
  dp->ch.blue = LV_UDIV255(sp->ch.blue * sp->ch.alpha);
}

LV_ATTRIBUTE_FAST_MEM POSSIBLY_UNUSED static void bgra5658_to_8888(
    const uint8_t* src, uint32_t* dst)
{
  lv_color32_t* c32 = (lv_color32_t*)dst;
  const lv_color16_t* c16 = (const lv_color16_t*)src;
  c32->ch.red = c16->ch.red << 3 | c16->ch.red >> 2;
  c32->ch.green = c16->ch.green << 2 | c16->ch.green >> 4;
  c32->ch.blue = c16->ch.blue << 3 | c16->ch.blue >> 2;
  c32->ch.alpha = src[LV_IMG_PX_SIZE_ALPHA_BYTE - 1];
}

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
  const uint8_t* img_data; /* points to the input image */

  /*Open the file if it's a file*/
  if (dsc->src_type == LV_IMG_SRC_FILE) {
    lv_fs_file_t f;
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

    uint32_t data_size = lv_img_cf_get_px_size(dsc->header.cf);
    data_size *= dsc->header.w * dsc->header.h;
    data_size >>= 3; /* bits to bytes */
    uint8_t* fs_buf;
    fs_buf = lv_mem_alloc(data_size);
    if (fs_buf == NULL) {
      GPU_ERROR("out of memory");
      lv_fs_close(&f);
      return LV_RES_INV;
    }

    lv_fs_seek(&f, 4, LV_FS_SEEK_SET); /* skip file header. */
    res = lv_fs_read(&f, fs_buf, data_size, NULL);
    lv_fs_close(&f);
    if (res != LV_FS_RES_OK) {
      lv_mem_free(fs_buf);
      GPU_ERROR("file read failed");
      return LV_RES_INV;
    }

    img_data = fs_buf;
  } else if (dsc->src_type == LV_IMG_SRC_VARIABLE) {
    const lv_img_dsc_t* img_dsc = dsc->src;
    /*The variables should have valid data*/
    if (img_dsc->data == NULL) {
      GPU_WARN("no data");
      return LV_RES_INV;
    }
    img_data = img_dsc->data;
  } else {
    /* No way to get the image data, return invalid */
    return LV_RES_INV;
  }

  /* alloc new buffer that meets GPU requirements(width, alignment) */
  uint32_t gpu_data_size;
  uint8_t* gpu_data;
  gpu_data = gpu_img_alloc(dsc->header.w, dsc->header.h, dsc->header.cf, &gpu_data_size);
  if (gpu_data == NULL) {
    GPU_ERROR("out of memory");
    if (dsc->src_type == LV_IMG_SRC_FILE) {
      /* release file cache */
      lv_mem_free((void*)img_data);
    }
    return LV_RES_INV;
  }

  /* add gpu header right at beginning of gpu image buffer */
  gpu_data_header_t* header = (gpu_data_header_t*)gpu_data;
  header->magic = GPU_DATA_MAGIC;
  header->recolor = dsc->color.full;
  dsc->img_data = gpu_data;
  bool premult = !(dsc->header.cf == LV_IMG_CF_TRUE_COLOR_ALPHA);
  lv_res_t ret = lv_gpu_load_vgbuf(img_data, &dsc->header, &header->vgbuf,
      gpu_data + sizeof(gpu_data_header_t), dsc->color, premult);

  if (dsc->src_type == LV_IMG_SRC_FILE) {
    /* file cache is no longger needed. */
    lv_mem_free((void*)img_data);
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

  uint32_t gpu_data_size;
  uint8_t* gpu_data;
  gpu_data = gpu_img_alloc(dsc->header.w, dsc->header.h, dsc->header.cf, &gpu_data_size);
  if (gpu_data == NULL) {
    GPU_ERROR("out of memory");
    return LV_RES_INV;
  }

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
      LV_ASSERT(lv_fs_read(&f, palette,
                    sizeof(lv_color32_t) * palette_size, NULL)
          == LV_FS_RES_OK);
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
  lv_memset_00(mem, vgbuf_data_size);
  LV_ASSERT(init_vg_buf(vgbuf, vgbuf_w, img_h, vgbuf_stride, mem, vgbuf_format,
                true)
      == LV_RES_OK);
  uint8_t* px_buf = vgbuf->memory;
  const uint8_t* px_map = img_dsc->data;
  if (indexed) {
    px_map += palette_size * sizeof(lv_color32_t);
  }

  if (map_stride == vgbuf_stride) {
    if (dsc->src_type == LV_IMG_SRC_FILE) {
      lv_fs_read(&f, px_buf, map_stride * img_h, NULL);
    } else {
      lv_memcpy(px_buf, px_map, map_stride * img_h);
    }

    bit_rev(px_size, px_buf, map_stride * img_h);

    px_map += map_stride * img_h;
    px_buf += vgbuf_stride * img_h;
  } else {
    for (int_fast16_t i = 0; i < img_h; i++) {
      if (dsc->src_type == LV_IMG_SRC_FILE) {
        lv_fs_read(&f, px_buf, map_stride, NULL);
      } else {
        lv_memcpy(px_buf, px_map, map_stride);
      }
      bit_rev(px_size, px_buf, map_stride);
      px_map += map_stride;
      px_buf += vgbuf_stride;
    }
  }

  if (dsc->src_type == LV_IMG_SRC_FILE) {
    lv_fs_close(&f);
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

    gpu_data_header_t* gpu_data = lv_mem_buf_get(sizeof(gpu_data_header_t));
    if (gpu_data == NULL) {
      GPU_ERROR("out of memory");
      lv_fs_close(&f);
      return LV_RES_INV;
    }
    gpu_data->magic = EVO_DATA_MAGIC;
    if (evo_read(&f, &gpu_data->evocontent) != LV_FS_RES_OK) {
      lv_mem_buf_release(gpu_data);
      lv_fs_close(&f);
      GPU_ERROR("file read failed");
      return LV_RES_INV;
    }
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
      GPU_WARN("%lx already decoded %p @ %p", header->magic,
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
        return LV_RES_INV;
      }
      uint8_t* gpu_data = (uint8_t*)gpu_dsc->data;

      lv_fs_seek(&f, 4, LV_FS_SEEK_SET); /* skip file header. */
      res = lv_fs_read(&f, gpu_data, gpu_dsc->data_size, NULL);
      lv_fs_close(&f);
      if (res != LV_FS_RES_OK) {
        gpu_img_free(gpu_data);
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
    dsc->user_data = (void*)GPU_DATA_MAGIC;
    lv_res_t ret = decode_rgb(decoder, dsc);
    return ret;
  }

  if ((cf >= LV_IMG_CF_INDEXED_1BIT && cf <= LV_IMG_CF_INDEXED_8BIT)
      || cf == LV_IMG_CF_ALPHA_8BIT || cf == LV_IMG_CF_ALPHA_4BIT) {
    dsc->user_data = (void*)GPU_DATA_MAGIC;
    lv_res_t ret = decode_indexed(decoder, dsc);
    return ret;
  }

  if (cf == LV_IMG_CF_EVO) {
    dsc->user_data = (void*)EVO_DATA_MAGIC;
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
    lv_mem_buf_release((void*)dsc->img_data);
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
 * @param premult the source has been pre-multiplied. used in draw_img only
 *
 * @return LV_RES_OK: ok; LV_RES_INV: failed
 *
 ****************************************************************************/
LV_ATTRIBUTE_FAST_MEM lv_res_t lv_gpu_load_vgbuf(const uint8_t* img_data,
    lv_img_header_t* header, vg_lite_buffer_t* vgbuf_p, uint8_t* buf_p,
    lv_color32_t recolor, bool premult)
{
  vg_lite_buffer_t vgbuf;

  void* mem = buf_p;
  int32_t vgbuf_w = ALIGN_UP(header->w, 16);
  int32_t vgbuf_stride = vgbuf_w * sizeof(lv_color_t);
  uint8_t vgbuf_format = VGLITE_PX_FMT;
  int32_t map_stride = header->w * sizeof(lv_color_t);
#if LV_COLOR_DEPTH == 16
  if (header->cf == LV_IMG_CF_TRUE_COLOR_ALPHA
      || header->cf == LV_IMG_CF_TRUE_COLOR_CHROMA_KEYED) {
    vgbuf_stride = vgbuf_w * sizeof(lv_color32_t);
    vgbuf_format = VG_LITE_BGRA8888;
    map_stride = header->w * LV_IMG_PX_SIZE_ALPHA_BYTE;
  }
  if (premult) {
    map_stride = vgbuf_stride;
  }
#else
  bool noalpha = header->cf == LV_IMG_CF_TRUE_COLOR
      || header->cf == LV_IMG_CF_TRUE_COLOR_CHROMA_KEYED;
#endif
  uint32_t vgbuf_size = header->h * vgbuf_stride;
  if (mem == NULL) {
    mem = gpu_heap_aligned_alloc(8, vgbuf_size);
  }
  if (mem == NULL) {
    GPU_WARN("Insufficient memory for GPU 16px aligned image cache");
    return LV_RES_INV;
  }
  lv_memset_00(mem, vgbuf_size);
  LV_ASSERT(init_vg_buf(&vgbuf, vgbuf_w, header->h, vgbuf_stride, mem,
                vgbuf_format, true)
      == LV_RES_OK);
  uint8_t* px_buf = vgbuf.memory;
  const uint8_t* px_map = img_data;
  lv_opa_t opa = recolor.ch.alpha;
  lv_opa_t mix = 255 - opa;
  uint32_t recolor_premult[3] = { recolor.ch.red * opa,
    recolor.ch.green * opa, recolor.ch.blue * opa };
  TC_INIT
#if LV_COLOR_DEPTH == 16
  if (header->cf == LV_IMG_CF_TRUE_COLOR_ALPHA) {
    TC_START
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
        bgra5658_to_8888(phwSource, pwTarget);
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

        /* load a vector of 4 bgra5658 pixels
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
          /* load a vector of 4 bgra5658 pixels */
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
      px_buf += vgbuf_stride;
    }
#else
    for (int_fast16_t i = 0; i < header->h; i++) {
      for (int_fast16_t j = 0; j < header->w; j++) {
        lv_color32_t* c32 = (lv_color32_t*)px_buf + j;
        lv_color16_t* c16 = (const lv_color16_t*)px_map;
        c32->ch.alpha = px_map[LV_IMG_PX_SIZE_ALPHA_BYTE - 1];
        c32->ch.red = (c16->ch.red * 263 + 7) * c32->ch.alpha >> 13;
        c32->ch.green = (c16->ch.green * 259 + 3) * c32->ch.alpha >> 14;
        c32->ch.blue = (c16->ch.blue * 263 + 7) * c32->ch.alpha >> 13;
        px_map += LV_IMG_PX_SIZE_ALPHA_BYTE;
      }
      px_buf += vgbuf_stride;
    }
#endif
    TC_END
    TC_REP(bgra5658_convert_to_8888)
  } else if (header->cf == LV_IMG_CF_TRUE_COLOR_CHROMA_KEYED) {
    for (int_fast16_t i = 0; i < header->h; i++) {
      const lv_color_t* px_map16 = (const lv_color_t*)px_map;
      uint32_t* px_buf32 = (uint32_t*)px_buf;
      for (int_fast16_t j = 0; j < header->w; j++) {
          px_buf32[j] = lv_color_to32((px_map16[j]);
          if (px_buf32[j] == LV_COLOR_CHROMA_KEY.full) {
          px_buf32[j] = 0;
          }
          px_map16++;
      }
      px_buf += vgbuf_stride;
    }
  } else
#endif
  {
    TC_START
    for (int_fast16_t i = 0; i < header->h; i++) {
#if LV_COLOR_DEPTH == 16
      lv_memcpy(px_buf, px_map, map_stride);
#else
      int32_t blkCnt = header->w;
      const uint32_t* phwSource = (const uint32_t*)px_map;
      uint32_t* pwTarget = (uint32_t*)px_buf;
      uint32_t chroma32 = (header->cf == LV_IMG_CF_TRUE_COLOR_CHROMA_KEYED)
          ? LV_COLOR_CHROMA_KEY.full
          : 0;
#ifdef CONFIG_ARM_HAVE_MVE
      uint32_t ff_alpha = 0xFF000000;
      if (IS_ALIGNED(phwSource, 4)) {
        if (opa != LV_OPA_TRANSP) {
          if (noalpha) {
            __asm volatile(
                "   .p2align 2                                                  \n"
                "   vdup.32                 q0, %[pRecolor]                     \n"
                "   vdup.8                  q1, %[opa]                          \n"
                "   vrmulh.u8               q0, q0, q1                          \n"
                "   vdup.8                  q1, %[mix]                          \n"
                "   vdup.32                 q3, %[ff_alpha]                     \n"
                "   wlstp.32                lr, %[loopCnt], 1f                  \n"
                "   2:                                                          \n"
                "   "
                "   vldrw.32                q2, [%[pSource]], #16               \n"
                "   vorr                    q2, q2, q3                          \n"
                "   vcmp.i32                ne, q2, %[chroma]                   \n"
                "   vrmulh.u8               q2, q2, q1                          \n"
                "   vadd.i8                 q2, q2, q0                          \n"
                /* set alpha to 0xFF */
                "   vorr                    q2, q2, q3                          \n"
                "   vpst                                                        \n"
                "   vstrwt.32               q2, [%[pTarget]], #16               \n"
                "   letp                    lr, 2b                              \n"
                "   1:                                                          \n"
                : [pSource] "+r"(phwSource), [pTarget] "+r"(pwTarget)
                : [loopCnt] "r"(blkCnt), [chroma] "r"(chroma32), [opa] "r"(opa),
                [mix] "r"(mix), [ff_alpha] "r"(ff_alpha), [pRecolor] "r"(recolor)
                : "q0", "q1", "q2", "q3", "lr", "memory");
          } else if (!premult) {
            __asm volatile(
                "   .p2align 2                                                  \n"
                "   vdup.32                 q0, %[pRecolor]                     \n"
                "   vdup.8                  q1, %[opa]                          \n"
                "   vrmulh.u8               q0, q0, q1                          \n"
                "   vdup.8                  q1, %[mix]                          \n"
                "   wlstp.32                lr, %[loopCnt], 1f                  \n"
                "   2:                                                          \n"
                "   "
                "   vldrw.32                q2, [%[pSource]], #16               \n"
                "   vsri.32                 q3, q2, #8                          \n"
                "   vsri.32                 q3, q2, #16                         \n"
                "   vsri.32                 q3, q2, #24                         \n"
                "   vcmp.i32                ne, q2, %[chroma]                   \n"
                "   vrmulh.u8               q2, q2, q1                          \n"
                "   vadd.i8                 q2, q2, q0                          \n"
                /* pre-multiply alpha to all channels */
                "   vrmulh.u8               q2, q2, q3                          \n"
                "   vsli.32                 q2, q3, #24                         \n"
                "   vpst                                                        \n"
                "   vstrwt.32               q2, [%[pTarget]], #16               \n"
                "   letp                    lr, 2b                              \n"
                "   1:                                                          \n"
                : [pSource] "+r"(phwSource), [pTarget] "+r"(pwTarget)
                : [loopCnt] "r"(blkCnt), [chroma] "r"(chroma32), [opa] "r"(opa),
                [mix] "r"(mix), [pRecolor] "r"(recolor)
                : "q0", "q1", "q2", "q3", "lr", "memory");
          } else {
            __asm volatile(
                "   .p2align 2                                                  \n"
                "   vdup.32                 q0, %[pRecolor]                     \n"
                "   vdup.8                  q1, %[opa]                          \n"
                "   vrmulh.u8               q0, q0, q1                          \n"
                "   vdup.8                  q1, %[mix]                          \n"
                "   wlstp.32                lr, %[loopCnt], 1f                  \n"
                "   2:                                                          \n"
                "   "
                "   vldrw.32                q2, [%[pSource]], #16               \n"
                "   vsri.32                 q3, q2, #8                          \n"
                "   vsri.32                 q3, q2, #16                         \n"
                "   vsri.32                 q3, q2, #24                         \n"
                /* pre-multiply source alpha to recolor_premult */
                "   vrmulh.u8               q4, q0, q3                          \n"
                "   vcmp.i32                ne, q2, %[chroma]                   \n"
                "   vrmulh.u8               q2, q2, q1                          \n"
                "   vadd.i8                 q2, q2, q4                          \n"
                "   vsli.32                 q2, q3, #24                         \n"
                "   vpst                                                        \n"
                "   vstrwt.32               q2, [%[pTarget]], #16               \n"
                "   letp                    lr, 2b                              \n"
                "   1:                                                          \n"
                : [pSource] "+r"(phwSource), [pTarget] "+r"(pwTarget)
                : [loopCnt] "r"(blkCnt), [chroma] "r"(chroma32), [opa] "r"(opa),
                [mix] "r"(mix), [pRecolor] "r"(recolor)
                : "q0", "q1", "q2", "q3", "q4", "lr", "memory");
          }
        } else {
          if (noalpha) {
            __asm volatile(
                "   .p2align 2                                                  \n"
                "   vdup.32                 q1, %[ff_alpha]                     \n"
                "   wlstp.32                lr, %[loopCnt], 1f                  \n"
                "   2:                                                          \n"
                "   "
                "   vldrw.32                q0, [%[pSource]], #16               \n"
                "   vorr                    q0, q0, q1                          \n"
                "   vcmp.i32                ne, q0, %[chroma]                   \n"
                "   vpst                                                        \n"
                "   vstrwt.32               q0, [%[pTarget]], #16               \n"
                "   letp                    lr, 2b                              \n"
                "   1:                                                          \n"
                : [pSource] "+r"(phwSource), [pTarget] "+r"(pwTarget)
                : [loopCnt] "r"(blkCnt), [chroma] "r"(chroma32),
                [ff_alpha] "r"(ff_alpha)
                : "q0", "q1", "lr", "memory");
          } else if (!premult) {
            __asm volatile(
                "   .p2align 2                                                  \n"
                "   wlstp.32                lr, %[loopCnt], 1f                  \n"
                "   2:                                                          \n"
                "   "
                "   vldrw.32                q0, [%[pSource]], #16               \n"
                "   vsri.32                 q1, q0, #8                          \n"
                "   vsri.32                 q1, q0, #16                         \n"
                "   vsri.32                 q1, q0, #24                         \n"
                "   vcmp.i32                ne, q0, %[chroma]                   \n"
                /* pre-multiply alpha to all channels */
                "   vrmulh.u8               q0, q0, q1                          \n"
                "   vsli.32                 q0, q1, #24                         \n"
                "   vpst                                                        \n"
                "   vstrwt.32               q0, [%[pTarget]], #16               \n"
                "   letp                    lr, 2b                              \n"
                "   1:                                                          \n"
                : [pSource] "+r"(phwSource), [pTarget] "+r"(pwTarget)
                : [loopCnt] "r"(blkCnt), [chroma] "r"(chroma32)
                : "q0", "q1", "lr", "memory");
          } else {
            __asm volatile(
                "   .p2align 2                                                  \n"
                "   wlstp.32                lr, %[loopCnt], 1f                  \n"
                "   2:                                                          \n"
                "   "
                "   vldrw.32                q0, [%[pSource]], #16               \n"
                "   vcmp.i32                ne, q0, %[chroma]                   \n"
                "   vpst                                                        \n"
                "   vstrwt.32               q0, [%[pTarget]], #16               \n"
                "   letp                    lr, 2b                              \n"
                "   1:                                                          \n"
                : [pSource] "+r"(phwSource), [pTarget] "+r"(pwTarget)
                : [loopCnt] "r"(blkCnt), [chroma] "r"(chroma32)
                : "q0", "lr", "memory");
          }
        }
      } else {
#endif /* CONFIG_ARM_HAVE_MVE */
        const lv_color32_t* sp = (const lv_color32_t*)phwSource;
        lv_color32_t* dp = (lv_color32_t*)pwTarget;
        for (; blkCnt--; sp++, dp++) {
          if ((noalpha && (sp->full | 0xFF000000) != chroma32) || sp->ch.alpha == LV_OPA_TRANSP
              || sp->full == chroma32) {
            dp->full = 0;
            continue;
          }
          if (opa != LV_OPA_TRANSP) {
            /* Perform recolor */
            if (noalpha) {
              dp->ch.red = LV_UDIV255(recolor_premult[0] + sp->ch.red * mix);
              dp->ch.green = LV_UDIV255(recolor_premult[1] + sp->ch.green * mix);
              dp->ch.blue = LV_UDIV255(recolor_premult[2] + sp->ch.blue * mix);
              dp->ch.alpha = 0xFF;
            } else {
              if (premult) {
                dp->ch.red = LV_UDIV255(recolor_premult[0] * sp->ch.alpha / 255 + sp->ch.red * mix);
                dp->ch.green = LV_UDIV255(recolor_premult[1] * sp->ch.alpha / 255 + sp->ch.green * mix);
                dp->ch.blue = LV_UDIV255(recolor_premult[2] * sp->ch.alpha / 255 + sp->ch.blue * mix);
                dp->ch.alpha = sp->ch.alpha;
              } else {
                dp->ch.red = LV_UDIV255(recolor_premult[0] + sp->ch.red * mix);
                dp->ch.green = LV_UDIV255(recolor_premult[1] + sp->ch.green * mix);
                dp->ch.blue = LV_UDIV255(recolor_premult[2] + sp->ch.blue * mix);
                pre_multiply(dp, dp);
              }
            }
          } else {
            if (noalpha) {
              dp->full = sp->full;
              dp->ch.alpha = 0xFF;
            } else if (!premult) {
              pre_multiply(dp, sp);
            } else {
              dp->full = sp->full;
            }
          }
        }
#ifdef CONFIG_ARM_HAVE_MVE
      }
#endif /* CONFIG_ARM_HAVE_MVE */
#endif /* LV_COLOR_DEPTH == 16 */
      px_map += map_stride;
      px_buf += vgbuf_stride;
    }
    TC_END
    TC_REP(vgbuf_memcpy_align)
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
