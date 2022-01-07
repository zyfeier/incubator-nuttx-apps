/**
 * @file lv_gpu_decoder.c
 *
 */

/*********************
 *      INCLUDES
 *********************/

#include "lv_gpu_decoder.h"
#include "../lvgl/src/draw/lv_draw_img.h"
#include "../lvgl/src/draw/lv_img_decoder.h"
#include "../lvgl/src/misc/lv_assert.h"
#include "../lvgl/src/misc/lv_color.h"
#include "../lvgl/src/misc/lv_gc.h"
#include "gpu_port.h"
#include "lv_gpu_interface.h"
#include <stdlib.h>

/*********************
 *      DEFINES
 *********************/

#define GPU_DATA_MAGIC 0x7615600D /* VGISGOOD */

/**********************
 *      TYPEDEFS
 **********************/

typedef struct {
  uint32_t magic;
  lv_fs_file_t f;
  lv_color32_t* palette;
  lv_opa_t* opa;
  vg_lite_buffer_t vgbuf;
} lv_gpu_decoder_data_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/

LV_ATTRIBUTE_FAST_MEM static void pre_multiply(lv_color32_t* dp, const lv_color32_t* sp);
LV_ATTRIBUTE_FAST_MEM static void bgra5658_to_8888(const uint8_t* src, uint32_t* dst);
LV_ATTRIBUTE_FAST_MEM static lv_res_t decode_indexed(lv_img_decoder_t* decoder, lv_img_decoder_dsc_t* dsc);

/**********************
 *   STATIC FUNCTIONS
 **********************/

LV_ATTRIBUTE_FAST_MEM static void pre_multiply(lv_color32_t* dp, const lv_color32_t* sp)
{
  dp->ch.alpha = sp->ch.alpha;
  dp->ch.red = LV_UDIV255(sp->ch.red * dp->ch.alpha);
  dp->ch.green = LV_UDIV255(sp->ch.green * dp->ch.alpha);
  dp->ch.blue = LV_UDIV255(sp->ch.blue * dp->ch.alpha);
}

LV_ATTRIBUTE_FAST_MEM POSSIBLY_UNUSED static void bgra5658_to_8888(const uint8_t* src, uint32_t* dst)
{
  lv_color32_t* c32 = (lv_color32_t*)dst;
  const lv_color16_t* c16 = (const lv_color16_t*)src;
  c32->ch.red = c16->ch.red << 3 | c16->ch.red >> 2;
  c32->ch.green = c16->ch.green << 2 | c16->ch.green >> 4;
  c32->ch.blue = c16->ch.blue << 3 | c16->ch.blue >> 2;
  c32->ch.alpha = src[LV_IMG_PX_SIZE_ALPHA_BYTE - 1];
}

LV_ATTRIBUTE_FAST_MEM static lv_res_t decode_indexed(lv_img_decoder_t* decoder, lv_img_decoder_dsc_t* dsc)
{
  uint8_t px_size = lv_img_cf_get_px_size(dsc->header.cf);
  uint32_t palette_size = 1 << px_size;
  /*Allocate the palette*/
  lv_gpu_decoder_data_t* user_data = dsc->user_data;
  user_data->palette = lv_mem_alloc(palette_size * sizeof(lv_color32_t));
  LV_ASSERT_MALLOC(user_data->palette);
  user_data->opa = lv_mem_alloc(palette_size * sizeof(lv_opa_t));
  LV_ASSERT_MALLOC(user_data->opa);
  if (user_data->palette == NULL || user_data->opa == NULL) {
    GPU_ERROR("gpu_decoder_open: out of memory");
    lv_gpu_decoder_close(decoder, dsc);
    return LV_RES_INV;
  }

  if (dsc->src_type == LV_IMG_SRC_FILE) {
    /*Read the palette from file*/
    lv_fs_seek(&user_data->f, 4, LV_FS_SEEK_SET); /*Skip the header*/
    lv_color32_t cur_color;
    for (int_fast16_t i = 0; i < palette_size; i++) {
      lv_fs_read(&user_data->f, &cur_color, sizeof(lv_color32_t), NULL);
      pre_multiply(&user_data->palette[i], &cur_color);
      user_data->opa[i] = cur_color.ch.alpha;
    }
  } else {
    /*The palette begins in the beginning of the image data. Just point to it.*/
    lv_color32_t* palette_p = (lv_color32_t*)((lv_img_dsc_t*)dsc->src)->data;

    for (int_fast16_t i = 0; i < palette_size; i++) {
      pre_multiply(&user_data->palette[i], &palette_p[i]);
      user_data->opa[i] = palette_p[i].ch.alpha;
    }
  }

  vg_lite_buffer_t vgbuf;

  int32_t img_w = dsc->header.w;
  int32_t img_h = dsc->header.h;
  int32_t vgbuf_w = ALIGN_UP(img_w, 16);
  uint8_t vgbuf_format = BPP_TO_VG_FMT(px_size);
  int32_t vgbuf_stride = vgbuf_w * px_size >> 3;
  int32_t map_stride_bits = img_w * px_size;
  int32_t map_stride = img_w * px_size >> 3;

  void* mem = memalign(8, img_h * vgbuf_stride);
  if (mem != NULL) {
    LV_ASSERT(init_vg_buf(&vgbuf, vgbuf_w, img_h, vgbuf_stride, mem, vgbuf_format, true) == LV_RES_OK);
    uint8_t* px_buf = vgbuf.memory;
    dsc->img_data = ((lv_img_dsc_t*)dsc->src)->data;
    const uint8_t* px_map = dsc->img_data + palette_size * sizeof(lv_color32_t);
    uint8_t res_bits = map_stride_bits & 7;
    uint8_t top_mask = ~((1 << (8 - res_bits)) - 1);
    if (!res_bits) {
      for (int_fast16_t i = 0; i < img_h; i++) {
        lv_memcpy(px_buf, px_map, map_stride);
        px_map += map_stride;
        px_buf += vgbuf_stride;
      }
    } else {
      /* Fo un-byte-aligned strides, untested, can be accelerated by MVE */
      uint8_t ls_bits = 0;
      for (int_fast16_t i = 0; i < img_h; i++) {
        if (ls_bits == 0) {
          lv_memcpy(px_buf, px_map, map_stride);
          px_buf[map_stride] = px_map[map_stride] & top_mask;
          px_map += map_stride;
          px_buf += vgbuf_stride;
          /* the next line will be [ls_bits] left-shifted */
        } else {
          for (int_fast16_t j = 0; j < map_stride + 1; j++) {
            *(px_buf + j) = *px_map++ << ls_bits;
            *(px_buf + j) |= *px_map >> (8 - ls_bits);
          }
        }
        ls_bits += res_bits;
        if (ls_bits > 7) {
          ls_bits -= 8;
          px_map++;
        }
      }
      px_buf += vgbuf_stride;
    }
    /*Save vglite buffer info*/
    lv_memcpy_small(&user_data->vgbuf, &vgbuf, sizeof(vgbuf));
    if (IS_CACHED(vgbuf.memory)) {
      cpu_cache_flush((uint32_t)vgbuf.memory, vgbuf.height * vgbuf.stride);
    }
  } else {
    GPU_WARN("Insufficient memory for GPU 16px aligned image cache");
    return LV_RES_INV;
  }
  return LV_RES_OK;
}

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

/**
 * Initialize the image decoder module
 */
void lv_gpu_decoder_init(void)
{
  lv_img_decoder_t* decoder = lv_img_decoder_create();
  if (decoder == NULL) {
    GPU_ERROR("lv_gpu_decoder_init: out of memory");
    return;
  }

  lv_img_decoder_set_info_cb(decoder, lv_img_decoder_built_in_info);
  lv_img_decoder_set_open_cb(decoder, lv_gpu_decoder_open);
  lv_img_decoder_set_close_cb(decoder, lv_gpu_decoder_close);
}

/**
 * Open an image for GPU rendering (aligning to 16px and pre-multiplying alpha channel)
 * @param decoder the decoder where this function belongs
 * @param dsc pointer to decoder descriptor. `src`, `style` are already initialized in it.
 * @return LV_RES_OK: the info is successfully stored in `header`; LV_RES_INV: unknown format or other error.
 */
lv_res_t lv_gpu_decoder_open(lv_img_decoder_t* decoder, lv_img_decoder_dsc_t* dsc)
{
  /*Open the file if it's a file*/

  if (dsc->src_type == LV_IMG_SRC_FILE) {
    /*Support only "*.bin" files*/
    if (strcmp(lv_fs_get_ext(dsc->src), "bin"))
      return LV_RES_INV;

    lv_fs_file_t f;
    lv_fs_res_t res = lv_fs_open(&f, dsc->src, LV_FS_MODE_RD);
    if (res != LV_FS_RES_OK) {
      GPU_WARN("gpu_decoder can't open the file");
      return LV_RES_INV;
    }

    /*If the file was open successfully save the file descriptor*/
    if (dsc->user_data == NULL) {
      dsc->user_data = lv_mem_alloc(sizeof(lv_gpu_decoder_data_t));
      LV_ASSERT_MALLOC(dsc->user_data);
      if (dsc->user_data == NULL) {
        GPU_ERROR("gpu_decoder_open: out of memory");
        lv_fs_close(&f);
        return LV_RES_INV;
      }
      lv_memset_00(dsc->user_data, sizeof(lv_gpu_decoder_data_t));
    }
    lv_gpu_decoder_data_t* user_data = dsc->user_data;
    user_data->magic = GPU_DATA_MAGIC;
    lv_memcpy_small(&user_data->f, &f, sizeof(f));
  } else if (dsc->src_type == LV_IMG_SRC_VARIABLE) {
    /*The variables should have valid data*/
    if (((lv_img_dsc_t*)dsc->src)->data == NULL) {
      return LV_RES_INV;
    }

    /*If the image was open successfully allocate the vglite buffer*/
    if (dsc->user_data == NULL) {
      dsc->user_data = lv_mem_alloc(sizeof(lv_gpu_decoder_data_t));
      LV_ASSERT_MALLOC(dsc->user_data);
      if (dsc->user_data == NULL) {
        GPU_ERROR("gpu_decoder_open: out of memory");
        return LV_RES_INV;
      }
      lv_memset_00(dsc->user_data, sizeof(lv_gpu_decoder_data_t));
    }
    lv_gpu_decoder_data_t* user_data = dsc->user_data;
    user_data->magic = GPU_DATA_MAGIC;
  }

  lv_img_cf_t cf = dsc->header.cf;
  /*Process true color formats*/
  if (cf == LV_IMG_CF_TRUE_COLOR || cf == LV_IMG_CF_TRUE_COLOR_ALPHA || cf == LV_IMG_CF_TRUE_COLOR_CHROMA_KEYED) {
    if (dsc->src_type == LV_IMG_SRC_VARIABLE) {
      /*In case of uncompressed formats the image stored in the ROM/RAM.
             *So simply give its pointer*/
      dsc->img_data = ((lv_img_dsc_t*)dsc->src)->data;
      GPU_INFO("gpu_decoder open w:%d h:%d @%p", dsc->header.w, dsc->header.h, dsc->img_data);
      lv_gpu_decoder_data_t* user_data = dsc->user_data;
      return lv_gpu_load_vgbuf(dsc->img_data, &dsc->header, &user_data->vgbuf);
    } else {
      /*If it's a file it need to be read line by line later*/
      return LV_RES_INV;
    }
  }
  /*Process indexed images. Build a palette*/
  else if (cf == LV_IMG_CF_INDEXED_1BIT || cf == LV_IMG_CF_INDEXED_2BIT || cf == LV_IMG_CF_INDEXED_4BIT || cf == LV_IMG_CF_INDEXED_8BIT) {
    return LV_RES_INV; /* decode_indexed(decoder, dsc); */
  }
  /*Unknown format. Can't decode it.*/
  else {
    /*Free the potentially allocated memories*/
    lv_gpu_decoder_close(decoder, dsc);

    GPU_WARN("GPU decoder open: unsupported color format");
    return LV_RES_INV;
  }
}

/**
 * Close the pending decoding. Free resources etc.
 * @param decoder pointer to the decoder the function associated with
 * @param dsc pointer to decoder descriptor
 */
void lv_gpu_decoder_close(lv_img_decoder_t* decoder, lv_img_decoder_dsc_t* dsc)
{
  LV_UNUSED(decoder); /*Unused*/

  lv_gpu_decoder_data_t* user_data = dsc->user_data;
  if (user_data) {
    if (dsc->src_type == LV_IMG_SRC_FILE) {
      lv_fs_close(&user_data->f);
    }
    if (dsc->src_type == LV_IMG_SRC_VARIABLE) {
      GPU_INFO("gpu_decoder close w:%d h:%d @%p", dsc->header.w, dsc->header.h, dsc->img_data);
      lv_mem_free(user_data->vgbuf.memory);
      vg_lite_free(&user_data->vgbuf);
    }
    if (user_data->palette) {
      lv_mem_free(user_data->palette);
    }
    if (user_data->opa) {
      lv_mem_free(user_data->opa);
    }
    lv_mem_free(user_data);
    dsc->user_data = NULL;
  }
}

/**
 * Load an image into vg_lite buffer with automatic alignment. Appropriate room will be
 * allocated in vgbuf_p->memory, leaving the user responsible for cleaning it up.
 * @param img_data pointer to the pixel buffer
 * @param img_header header of the image containing width, height and color format
 * @param vgbuf_p address of the vg_lite_buffer_t structure to be initialized
 * @return LV_RES_OK: ok; LV_RES_INV: failed
 */
LV_ATTRIBUTE_FAST_MEM lv_res_t lv_gpu_load_vgbuf(const uint8_t* img_data, lv_img_header_t* header, vg_lite_buffer_t* vgbuf_p)
{
  vg_lite_buffer_t vgbuf;

  if (header->w * header->h < GPU_SIZE_LIMIT) {
    GPU_WARN("Image (w:%d h:%d) too small for GPU", header->w, header->h);
    return LV_RES_INV;
  }

  void* mem;
  int32_t vgbuf_w = ALIGN_UP(header->w, 16);
  int32_t vgbuf_stride = vgbuf_w * sizeof(lv_color_t);
  uint8_t vgbuf_format = VGLITE_PX_FMT;
  int32_t map_stride = header->w * sizeof(lv_color_t);
#if LV_COLOR_DEPTH == 16
  if (header->cf == LV_IMG_CF_TRUE_COLOR_ALPHA || header->cf == LV_IMG_CF_TRUE_COLOR_CHROMA_KEYED) {
    vgbuf_stride = vgbuf_w * sizeof(lv_color32_t);
    vgbuf_format = VG_LITE_BGRA8888;
    map_stride = header->w * LV_IMG_PX_SIZE_ALPHA_BYTE;
  }
#endif
  uint32_t vgbuf_size = header->h * vgbuf_stride;
  mem = memalign(8, vgbuf_size);
  if (mem != NULL) {
    lv_memset_00(mem, vgbuf_size);
    LV_ASSERT(init_vg_buf(&vgbuf, vgbuf_w, header->h, vgbuf_stride, mem, vgbuf_format, true) == LV_RES_OK);
    uint8_t* px_buf = vgbuf.memory;
    const uint8_t* px_map = img_data;
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
// #define USE_MVE_INTRINSICS (disabled due to intrinsics being much slower than hand-written asm)
#ifdef USE_MVE_INTRINSICS
        uint32x4_t maskA = vldrwq_u32(_maskA);
        uint32x4_t maskRB = vldrwq_u32(_maskRB);
        uint32x4_t maskG = vldrwq_u32(_maskG);
        uint32x4_t shiftC = vldrwq_u32(_shiftC);
        do {
          mve_pred16_t tailPred = vctp32q(blkCnt);

          /* load a vector of 4 bgra5658 pixels (residuals are processed in the next loop) */
          uint32x4_t vecIn = vld1q_z_u32((const uint32_t*)phwSource, tailPred);
          /* extract individual channels and place them in high 8bits (P=GlB, Q=RGh) */

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
        uint32_t carry = 0;
        __asm volatile(
            "   .p2align 2                                                  \n"
            "   wlstp.32                lr, %[loopCnt], 1f                  \n"
            "   2:                                                          \n"
            /* load a vector of 4 bgra5658 pixels */
            "   vldrw.32                q0, [%[pSource]], #12               \n"
            /* q0 => |****|AQPA|QPAQ|PAQP| */
            "   vshlc                   q0, %[pCarry], #8                   \n"
            /* q0 => |***A|QPAQ|PAQP|AQP0| */
            "   vldrw.32                q1, [%[maskA]]                      \n"
            "   vand                    q2, q0, q1                          \n"
            /* q2 => |000A|00A0|0A00|A000| */
            "   vldrw.32                q4, [%[shiftC]]                     \n"
            "   vmul.i32                q1, q2, q4                          \n"
            /* q1 => |A000|A000|A000|A000|, use q1 as final output */
            "   vshlc                   q0, %[pCarry], #8                   \n"
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
            "   vshlc                   q0, %[pCarry], #5                   \n"
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
            "   vshlc                   q0, %[pCarry], #6                   \n"
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
            "   vmulh.u8                q2, q1, q3                          \n"
            "   vsli.32                 q2, q3, #24                         \n"
            /* store a vector of 4 bgra8888 pixels */
            "   vstrw.32                q2, [%[pTarget]], #16               \n"
            "   letp                    lr, 2b                              \n"
            "   1:                                                          \n"

            : [pSource] "+r"(phwSource), [pTarget] "+r"(pwTarget)
            : [loopCnt] "r"(blkCnt), [maskA] "r"(_maskA), [shiftC] "r"(_shiftC),
            [maskRB] "r"(_maskRB), [maskG] "r"(_maskG), [pCarry] "r"(carry)
            : "q0", "q1", "q2", "q3", "q4", "lr", "memory");
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
        const lv_color32_t* phwSource = (const lv_color32_t*)px_map;
        uint32_t* pwTarget = (uint32_t*)px_buf;
        uint32_t chroma32 = (header->cf == LV_IMG_CF_TRUE_COLOR_CHROMA_KEYED) ? LV_COLOR_CHROMA_KEY.full : 0;
#ifdef CONFIG_ARM_HAVE_MVE
        if (!IS_ALIGNED(phwSource, 4)) {
          if (phwSource->full != chroma32) {
            pre_multiply((lv_color32_t*)pwTarget, phwSource);
          }
          phwSource++;
          pwTarget++;
          blkCnt--;
        }
        __asm volatile(
            "   .p2align 2                                                  \n"
            "   wlstp.32                lr, %[loopCnt], 1f                  \n"
            "   2:                                                          \n"
            "   vldrw.32                q0, [%[pSource]], #16               \n"
            "   vsri.32                 q1, q0, #8                          \n"
            "   vsri.32                 q1, q0, #16                         \n"
            "   vsri.32                 q1, q0, #24                         \n"
            "   vmulh.u8                q2, q0, q1                          \n"
            "   vsli.32                 q2, q1, #24                         \n"
            "   vcmp.i32                ne, q0, %[chroma]                   \n"
            "   vpst                                                        \n"
            "   vstrwt.32               q2, [%[pTarget]], #16               \n"
            "   letp                    lr, 2b                              \n"
            "   1:                                                          \n"
            : [pSource] "+r"(phwSource), [pTarget] "+r"(pwTarget)
            : [loopCnt] "r"(blkCnt), [chroma] "r"(chroma32)
            : "q0", "q1", "q2", "lr", "memory");
#else
        const lv_color32_t* sp = (const lv_color32_t*)phwSource;
        lv_color32_t* dp = (lv_color32_t*)pwTarget;
        for (; blkCnt--; sp++, dp++) {
          if (sp->full != chroma32) {
            pre_multiply(dp, sp);
          }
        }
#endif /* CONFIG_ARM_HAVE_MVE */
#endif /* LV_COLOR_DEPTH == 16 */
        px_map += map_stride;
        px_buf += vgbuf_stride;
      }
      TC_END
      TC_REP(vgbuf_memcpy_align)
    }
  } else {
    GPU_WARN("Insufficient memory for GPU 16px aligned image cache");
    return LV_RES_INV;
  }
  /*Save vglite buffer info*/
  lv_memcpy_small(vgbuf_p, &vgbuf, sizeof(vgbuf));
  if (IS_CACHED(vgbuf.memory)) {
    cpu_cache_flush((uint32_t)vgbuf.memory, vgbuf.height * vgbuf.stride);
  }
  return LV_RES_OK;
}

/**
 * Get the vgbuf cache corresponding to the image pointer (if available).
 *
 * @param ptr pointer to the pixel buffer
 * @return pointer to the vg_lite_buffer_t structure in cache items, NULL if cache miss
 */
LV_ATTRIBUTE_FAST_MEM vg_lite_buffer_t* lv_gpu_get_vgbuf(const void* ptr)
{
  _lv_img_cache_entry_t* cache = LV_GC_ROOT(_lv_img_cache_array);
  int16_t entry_cnt = LV_IMG_CACHE_DEF_SIZE;
  for (int_fast16_t i = 0; i < entry_cnt; i++) {
    if (ptr == cache[i].dec_dsc.img_data) {
      lv_gpu_decoder_data_t* user_data = cache[i].dec_dsc.user_data;
      if (user_data && (user_data->magic == GPU_DATA_MAGIC)) {
        return &user_data->vgbuf;
      } else {
        GPU_WARN("no vgbuf data");
        return NULL;
      }
    }
  }
  return NULL;
}
