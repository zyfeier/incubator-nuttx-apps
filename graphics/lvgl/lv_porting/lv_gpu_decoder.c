/**
 * @file lv_gpu_decoder.c
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include "../lvgl/src/draw/lv_draw_img.h"
#include "../lvgl/src/misc/lv_assert.h"
#include "../lvgl/src/misc/lv_gc.h"
#include "../lvgl/src/misc/lv_ll.h"
#include "lv_gpu_interface.h"
#include "lv_gpu_decoder.h"

/*********************
 *      DEFINES
 *********************/
#define CF_BUILT_IN_FIRST LV_IMG_CF_TRUE_COLOR
#define CF_BUILT_IN_LAST LV_IMG_CF_ALPHA_8BIT

/**********************
 *      TYPEDEFS
 **********************/

typedef struct {
  lv_fs_file_t f;
  lv_color_t* palette;
  lv_opa_t* opa;
  vg_lite_buffer_t vgbuf;
} lv_gpu_decoder_data_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/
static lv_res_t lv_gpu_decoder_line_true_color(lv_img_decoder_dsc_t* dsc, lv_coord_t x, lv_coord_t y,
    lv_coord_t len, uint8_t* buf);
static lv_res_t lv_gpu_decoder_line_alpha(lv_img_decoder_dsc_t* dsc, lv_coord_t x, lv_coord_t y,
    lv_coord_t len, uint8_t* buf);
static lv_res_t lv_gpu_decoder_line_indexed(lv_img_decoder_dsc_t* dsc, lv_coord_t x, lv_coord_t y,
    lv_coord_t len, uint8_t* buf);

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

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
    LV_LOG_WARN("lv_gpu_decoder_init: out of memory");
    return;
  }

  lv_img_decoder_set_info_cb(decoder, lv_gpu_decoder_info);
  lv_img_decoder_set_open_cb(decoder, lv_gpu_decoder_open);
  lv_img_decoder_set_read_line_cb(decoder, lv_gpu_decoder_read_line);
  lv_img_decoder_set_close_cb(decoder, lv_gpu_decoder_close);
}

/**
 * Get info about a built-in image
 * @param decoder the decoder where this function belongs
 * @param src the image source: pointer to an `lv_img_dsc_t` variable, a file path or a symbol
 * @param header store the image data here
 * @return LV_RES_OK: the info is successfully stored in `header`; LV_RES_INV: unknown format or other error.
 */
lv_res_t lv_gpu_decoder_info(lv_img_decoder_t* decoder, const void* src, lv_img_header_t* header)
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
  } else if (src_type == LV_IMG_SRC_FILE) {
    /*Support only "*.bin" files*/
    if (strcmp(lv_fs_get_ext(src), "bin"))
      return LV_RES_INV;

    lv_fs_file_t f;
    lv_fs_res_t res = lv_fs_open(&f, src, LV_FS_MODE_RD);
    if (res == LV_FS_RES_OK) {
      uint32_t rn;
      res = lv_fs_read(&f, header, sizeof(lv_img_header_t), &rn);
      lv_fs_close(&f);
      if (res != LV_FS_RES_OK || rn != sizeof(lv_img_header_t)) {
        LV_LOG_WARN("GPU decoder read file header error");
        return LV_RES_INV;
      }
    }

    if (header->cf < CF_BUILT_IN_FIRST || header->cf > CF_BUILT_IN_LAST)
      return LV_RES_INV;
  } else if (src_type == LV_IMG_SRC_SYMBOL) {
    /*The size depend on the font but it is unknown here. It should be handled outside of the
         *function*/
    header->w = 1;
    header->h = 1;
    /*Symbols always have transparent parts. Important because of cover check in the draw
         *function. The actual value doesn't matter because lv_draw_label will draw it*/
    header->cf = LV_IMG_CF_ALPHA_1BIT;
  } else {
    LV_LOG_WARN("Image get info found unknown src type");
    return LV_RES_INV;
  }
  return LV_RES_OK;
}

/**
 * Open a built in image
 * @param decoder the decoder where this function belongs
 * @param dsc pointer to decoder descriptor. `src`, `color` are already initialized in it.
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
      LV_LOG_WARN("Built-in image decoder can't open the file");
      return LV_RES_INV;
    }

    /*If the file was open successfully save the file descriptor*/
    if (dsc->user_data == NULL) {
      dsc->user_data = lv_mem_alloc(sizeof(lv_gpu_decoder_data_t));
      LV_ASSERT_MALLOC(dsc->user_data);
      if (dsc->user_data == NULL) {
        LV_LOG_ERROR("gpu_decoder_open: out of memory");
        lv_fs_close(&f);
        return LV_RES_INV;
      }
      lv_memset_00(dsc->user_data, sizeof(lv_gpu_decoder_data_t));
    }

    lv_gpu_decoder_data_t* user_data = dsc->user_data;
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
        LV_LOG_ERROR("gpu_decoder_open: out of memory");
        return LV_RES_INV;
      }
      lv_memset_00(dsc->user_data, sizeof(lv_gpu_decoder_data_t));
    }
  }

  lv_img_cf_t cf = dsc->header.cf;
  /*Process true color formats*/
  if (cf == LV_IMG_CF_TRUE_COLOR || cf == LV_IMG_CF_TRUE_COLOR_ALPHA || cf == LV_IMG_CF_TRUE_COLOR_CHROMA_KEYED) {
    if (dsc->src_type == LV_IMG_SRC_VARIABLE) {
      /*In case of uncompressed formats the image stored in the ROM/RAM.
             *So simply give its pointer*/
      dsc->img_data = ((lv_img_dsc_t*)dsc->src)->data;
      lv_gpu_decoder_data_t* user_data = dsc->user_data;
      vg_lite_buffer_t vgbuf;

      int32_t img_w = dsc->header.w;
      int32_t img_h = dsc->header.h;
      if (img_w * img_h < GPU_SIZE_LIMIT) {
        LV_LOG_WARN("Image too small for GPU");
        return LV_RES_OK;
      }

      void* mem;

      int32_t vgbuf_w = ALIGN_UP(img_w, 16);
      int32_t vgbuf_stride = vgbuf_w * sizeof(lv_color_t);
      uint8_t vgbuf_format = VGLITE_PX_FMT;
      int32_t map_stride = img_w * sizeof(lv_color_t);
#if LV_COLOR_DEPTH == 16
      if (cf == LV_IMG_CF_TRUE_COLOR_ALPHA) {
        vgbuf_stride = vgbuf_w * sizeof(lv_color32_t);
        vgbuf_format = VG_LITE_BGRA8888;
        map_stride = img_w * LV_IMG_PX_SIZE_ALPHA_BYTE;
      }
#endif
      mem = lv_mem_alloc(img_h * vgbuf_stride);
      if (mem != NULL) {
        init_vg_buf(&vgbuf, vgbuf_w, img_h, vgbuf_stride, mem, vgbuf_format, true);
        uint8_t* px_buf = vgbuf.memory;
        uint8_t* px_map = dsc->img_data;
#if LV_COLOR_DEPTH == 16
        if (cf == LV_IMG_CF_TRUE_COLOR_ALPHA) {
#ifdef CONFIG_ARM_HAVE_MVE

          int32_t blkCnt;
          const uint32_t _maskA[4] = { 0xff000000, 0xff0000, 0xff00, 0xff };
          const uint32_t _maskRB[4] = { 0xf8000000, 0xf80000, 0xf800, 0xf8 };
          const uint32_t _maskG[4] = { 0xfc000000, 0xfc0000, 0xfc00, 0xfc };
          const uint32_t _shiftC[4] = { 0x1, 0x100, 0x10000, 0x1000000 };

          for (int_fast16_t y = 0; y < img_h; y++) {
            const uint16_t* phwSource = px_map;
            uint32_t* pwTarget = px_buf;

            blkCnt = img_w;
            while (!IS_ALIGNED(phwSource, 4)) {
              bgra5658_to_8888(phwSource, pwTarget);
              phwSource = (uint8_t*)phwSource + 3;
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
            uint32_t carry;
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
          for (int_fast16_t i = 0; i < map_h; i++) {
            for (int_fast16_t j = 0; j < map_w; j++) {
              lv_color32_t* c32 = (uint32_t*)px_buf + j;
              lv_color16_t* c16 = (const lv_color16_t*)px_map;
              c32->ch.alpha = px_map[LV_IMG_PX_SIZE_ALPHA_BYTE - 1];
              c32->ch.red = (c16->ch.red * 263 + 7) * c32->ch.alpha >> 13;
              c32->ch.green = (c16->ch.green * 259 + 3) * c32->ch.alpha >> 14;
              c32->ch.blue = (c16->ch.blue * 263 + 7) * c32->ch.alpha >> 13;
              // *((uint32_t*)px_buf + j) = c32.full;
              px_map += LV_IMG_PX_SIZE_ALPHA_BYTE;
            }
            // lv_memset_00(px_buf + map_w * 4, vgbuf_stride - map_w * 4);
            px_buf += vgbuf_stride;
          }
#endif
        } else
#endif
          for (int_fast16_t i = 0; i < img_h; i++) {
#if LV_COLOR_DEPTH == 16
            lv_memcpy(px_buf, px_map, map_stride);
#else
            int32_t blkCnt = img_w;
            const uint16_t* phwSource = px_map;
            uint32_t* pwTarget = px_buf;
#ifdef CONFIG_ARM_HAVE_MVE
            if (!IS_ALIGNED(phwSource, 4)) {
              lv_color32_t* sp = (uint32_t*)phwSource;
              lv_color32_t* dp = pwTarget;
              dp->ch.alpha = px_map[3];
              dp->ch.red = LV_UDIV255(sp->ch.red * dp->ch.alpha);
              dp->ch.green = LV_UDIV255(sp->ch.green * dp->ch.alpha);
              dp->ch.blue = LV_UDIV255(sp->ch.blue * dp->ch.alpha);
              phwSource += 2;
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
                "   vstrw.32                q2, [%[pTarget]], #16               \n"
                "   letp                    lr, 2b                              \n"
                "   1:                                                          \n"
                : [pSource] "+r"(phwSource), [pTarget] "+r"(pwTarget)
                : [loopCnt] "r"(blkCnt)
                : "q0", "q1", "q2", "lr", "memory");
#else
              lv_color32_t* sp = (uint32_t*)phwSource;
              lv_color32_t* dp = pwTarget;
              for (; blkCnt--; sp++, dp++) {
                dp->ch.alpha = px_map[3];
                dp->ch.red = LV_UDIV255(sp->ch.red * dp->ch.alpha);
                dp->ch.green = LV_UDIV255(sp->ch.green * dp->ch.alpha);
                dp->ch.blue = LV_UDIV255(sp->ch.blue * dp->ch.alpha);
              }
#endif /* CONFIG_ARM_HAVE_MVE */
#endif /* LV_COLOR_DEPTH == 16 */
            lv_memset_00(px_buf + map_stride, vgbuf_stride - map_stride);
            px_map += map_stride;
            px_buf += vgbuf_stride;
          }
      } else {
        LV_LOG_WARN("Insufficient memory for GPU 16px aligned image cache");
        return LV_RES_OK;
      }
      /*Save vglite buffer info*/
      lv_memcpy_small(&user_data->vgbuf, &vgbuf, sizeof(vgbuf));
      if (IS_CACHED(vgbuf.memory)) {
        cpu_cache_flush(vgbuf.memory, vgbuf.height * vgbuf.stride);
      }
      return LV_RES_OK;
    } else {
      /*If it's a file it need to be read line by line later*/
      return LV_RES_OK;
    }
  }
  // /*Process indexed images. Build a palette*/
  // else if (cf == LV_IMG_CF_INDEXED_1BIT || cf == LV_IMG_CF_INDEXED_2BIT || cf == LV_IMG_CF_INDEXED_4BIT || cf == LV_IMG_CF_INDEXED_8BIT) {
  //   uint8_t px_size = lv_img_cf_get_px_size(cf);
  //   uint32_t palette_size = 1 << px_size;

  //   /*Allocate the palette*/

  //   lv_gpu_decoder_data_t* user_data = dsc->user_data;
  //   user_data->palette = lv_mem_alloc(palette_size * sizeof(lv_color_t));
  //   LV_ASSERT_MALLOC(user_data->palette);
  //   user_data->opa = lv_mem_alloc(palette_size * sizeof(lv_opa_t));
  //   LV_ASSERT_MALLOC(user_data->opa);
  //   if (user_data->palette == NULL || user_data->opa == NULL) {
  //     LV_LOG_ERROR("gpu_decoder_open: out of memory");
  //     lv_gpu_decoder_close(decoder, dsc);
  //     return LV_RES_INV;
  //   }

  //   if (dsc->src_type == LV_IMG_SRC_FILE) {
  //     /*Read the palette from file*/
  //     lv_fs_seek(&user_data->f, 4, LV_FS_SEEK_SET); /*Skip the header*/
  //     lv_color32_t cur_color;
  //     for (int_fast16_t i = 0; i < palette_size; i++) {
  //       lv_fs_read(&user_data->f, &cur_color, sizeof(lv_color32_t), NULL);
  //       user_data->palette[i] = lv_color_make(cur_color.ch.red, cur_color.ch.green, cur_color.ch.blue);
  //       user_data->opa[i] = cur_color.ch.alpha;
  //     }
  //   } else {
  //     /*The palette begins in the beginning of the image data. Just point to it.*/
  //     lv_color32_t* palette_p = (lv_color32_t*)((lv_img_dsc_t*)dsc->src)->data;

  //     for (int_fast16_t i = 0; i < palette_size; i++) {
  //       user_data->palette[i] = lv_color_make(palette_p[i].ch.red, palette_p[i].ch.green, palette_p[i].ch.blue);
  //       user_data->opa[i] = palette_p[i].ch.alpha;
  //     }
  //   }
  //   vg_lite_buffer_t vgbuf;

  //   int32_t img_w = dsc->header.w;
  //   int32_t img_h = dsc->header.h;
  //   int32_t vgbuf_w = ALIGN_UP(img_w, 16);
  //   uint8_t vgbuf_format = BPP_TO_VG_FMT(px_size);
  //   int32_t vgbuf_stride = vgbuf_w * px_size >> 3;
  //   int32_t map_stride = img_w * px_size >> 3;
  //   LV_LOG_ERROR("img_w:%d img_h:%d vgbuf_w:%d fmt:%d vg_stride:%d img_stride:%d", img_w, img_h, vgbuf_w, vgbuf_format, vgbuf_stride, map_stride);
  //   sleep(1);
  //   void * mem = lv_mem_alloc(img_h * vgbuf_stride);
  //   if (mem != NULL) {
  //     init_vg_buf(&vgbuf, vgbuf_w, img_h, vgbuf_stride, mem, vgbuf_format, true);
  //     uint8_t* px_buf = vgbuf.memory;
  //     uint8_t* px_map = dsc->img_data;
  //     for (int_fast16_t i = 0; i < img_h; i++) {
  //       lv_memcpy(px_buf, px_map, map_stride);
  //       lv_memset_00(px_buf + map_stride, vgbuf_stride - map_stride);
  //       px_map += map_stride;
  //       px_buf += vgbuf_stride;
  //     }
  //     /*Save vglite buffer info*/
  //     lv_memcpy_small(&user_data->vgbuf, &vgbuf, sizeof(vgbuf));
  //     if (IS_CACHED(vgbuf.memory)) {
  //       cpu_cache_flush(vgbuf.memory, vgbuf.height * vgbuf.stride);
  //     }
  //     return LV_RES_OK;
  //   }
  //   else {
  //     LV_LOG_WARN("Insufficient memory for GPU 16px aligned image cache");
  //     return LV_RES_OK;
  //   }
  // }
  // /*Alpha indexed images.*/
  // else if (cf == LV_IMG_CF_ALPHA_1BIT || cf == LV_IMG_CF_ALPHA_2BIT || cf == LV_IMG_CF_ALPHA_4BIT || cf == LV_IMG_CF_ALPHA_8BIT) {
  //   return LV_RES_OK; /*Nothing to process*/
  // }
  /*Unknown format. Can't decode it.*/
  else {
    /*Free the potentially allocated memories*/
    lv_gpu_decoder_close(decoder, dsc);

    LV_LOG_WARN("Image decoder open: unknown color format");
    return LV_RES_INV;
  }
}

/**
 * Decode `len` pixels starting from the given `x`, `y` coordinates and store them in `buf`.
 * Required only if the "open" function can't return with the whole decoded pixel array.
 * @param decoder pointer to the decoder the function associated with
 * @param dsc pointer to decoder descriptor
 * @param x start x coordinate
 * @param y start y coordinate
 * @param len number of pixels to decode
 * @param buf a buffer to store the decoded pixels
 * @return LV_RES_OK: ok; LV_RES_INV: failed
 */
lv_res_t lv_gpu_decoder_read_line(lv_img_decoder_t* decoder, lv_img_decoder_dsc_t* dsc, lv_coord_t x,
    lv_coord_t y, lv_coord_t len, uint8_t* buf)
{
  LV_UNUSED(decoder); /*Unused*/

  lv_res_t res = LV_RES_INV;

  if (dsc->header.cf == LV_IMG_CF_TRUE_COLOR || dsc->header.cf == LV_IMG_CF_TRUE_COLOR_ALPHA || dsc->header.cf == LV_IMG_CF_TRUE_COLOR_CHROMA_KEYED) {
    /*For TRUE_COLOR images read line required only for files.
         *For variables the image data was returned in `open`*/
    if (dsc->src_type == LV_IMG_SRC_FILE) {
      res = lv_gpu_decoder_line_true_color(dsc, x, y, len, buf);
    }
  } else if (dsc->header.cf == LV_IMG_CF_ALPHA_1BIT || dsc->header.cf == LV_IMG_CF_ALPHA_2BIT || dsc->header.cf == LV_IMG_CF_ALPHA_4BIT || dsc->header.cf == LV_IMG_CF_ALPHA_8BIT) {
    res = lv_gpu_decoder_line_alpha(dsc, x, y, len, buf);
  } else if (dsc->header.cf == LV_IMG_CF_INDEXED_1BIT || dsc->header.cf == LV_IMG_CF_INDEXED_2BIT || dsc->header.cf == LV_IMG_CF_INDEXED_4BIT || dsc->header.cf == LV_IMG_CF_INDEXED_8BIT) {
    res = lv_gpu_decoder_line_indexed(dsc, x, y, len, buf);
  } else {
    LV_LOG_WARN("Built-in image decoder read not supports the color format");
    return LV_RES_INV;
  }

  return res;
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
      lv_mem_free(user_data->vgbuf.memory);
      vg_lite_free(&user_data->vgbuf);
    }
    if (user_data->palette)
      lv_mem_free(user_data->palette);
    if (user_data->opa)
      lv_mem_free(user_data->opa);

    lv_mem_free(user_data);
    dsc->user_data = NULL;
  }
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static lv_res_t lv_gpu_decoder_line_true_color(lv_img_decoder_dsc_t* dsc, lv_coord_t x, lv_coord_t y,
    lv_coord_t len, uint8_t* buf)
{
  lv_gpu_decoder_data_t* user_data = dsc->user_data;
  lv_fs_res_t res;
  uint8_t px_size = lv_img_cf_get_px_size(dsc->header.cf);

  uint32_t pos = ((y * dsc->header.w + x) * px_size) >> 3;
  pos += 4; /*Skip the header*/
  res = lv_fs_seek(&user_data->f, pos, LV_FS_SEEK_SET);
  if (res != LV_FS_RES_OK) {
    LV_LOG_WARN("Built-in image decoder seek failed");
    return LV_RES_INV;
  }
  uint32_t btr = len * (px_size >> 3);
  uint32_t br = 0;
  res = lv_fs_read(&user_data->f, buf, btr, &br);
  if (res != LV_FS_RES_OK || btr != br) {
    LV_LOG_WARN("Built-in image decoder read failed");
    return LV_RES_INV;
  }

  return LV_RES_OK;
}

static lv_res_t lv_gpu_decoder_line_alpha(lv_img_decoder_dsc_t* dsc, lv_coord_t x, lv_coord_t y,
    lv_coord_t len, uint8_t* buf)
{
  const lv_opa_t alpha1_opa_table[2] = { 0, 255 }; /*Opacity mapping with bpp = 1 (Just for compatibility)*/
  const lv_opa_t alpha2_opa_table[4] = { 0, 85, 170, 255 }; /*Opacity mapping with bpp = 2*/
  const lv_opa_t alpha4_opa_table[16] = { 0, 17, 34, 51, /*Opacity mapping with bpp = 4*/
    68, 85, 102, 119, 136, 153, 170, 187, 204, 221, 238, 255 };

  /*Simply fill the buffer with the color. Later only the alpha value will be modified.*/
  lv_color_t bg_color = dsc->color;
  lv_coord_t i;
  for (i = 0; i < len; i++) {
#if LV_COLOR_DEPTH == 8 || LV_COLOR_DEPTH == 1
    buf[i * LV_IMG_PX_SIZE_ALPHA_BYTE] = bg_color.full;
#elif LV_COLOR_DEPTH == 16
    /*Because of Alpha byte 16 bit color can start on odd address which can cause crash*/
    buf[i * LV_IMG_PX_SIZE_ALPHA_BYTE] = bg_color.full & 0xFF;
    buf[i * LV_IMG_PX_SIZE_ALPHA_BYTE + 1] = (bg_color.full >> 8) & 0xFF;
#elif LV_COLOR_DEPTH == 32
    *((uint32_t*)&buf[i * LV_IMG_PX_SIZE_ALPHA_BYTE]) = bg_color.full;
#else
#error "Invalid LV_COLOR_DEPTH. Check it in lv_conf.h"
#endif
  }

  const lv_opa_t* opa_table = NULL;
  uint8_t px_size = lv_img_cf_get_px_size(dsc->header.cf);
  uint16_t mask = (1 << px_size) - 1; /*E.g. px_size = 2; mask = 0x03*/

  lv_coord_t w = 0;
  uint32_t ofs = 0;
  int8_t pos = 0;
  switch (dsc->header.cf) {
  case LV_IMG_CF_ALPHA_1BIT:
    w = (dsc->header.w + 7) >> 3; /*E.g. w = 20 -> w = 2 + 1*/
    ofs += w * y + (x >> 3); /*First pixel*/
    pos = 7 - (x & 0x7);
    opa_table = alpha1_opa_table;
    break;
  case LV_IMG_CF_ALPHA_2BIT:
    w = (dsc->header.w + 3) >> 2; /*E.g. w = 13 -> w = 3 + 1 (bytes)*/
    ofs += w * y + (x >> 2); /*First pixel*/
    pos = 6 - (x & 0x3) * 2;
    opa_table = alpha2_opa_table;
    break;
  case LV_IMG_CF_ALPHA_4BIT:
    w = (dsc->header.w + 1) >> 1; /*E.g. w = 13 -> w = 6 + 1 (bytes)*/
    ofs += w * y + (x >> 1); /*First pixel*/
    pos = 4 - (x & 0x1) * 4;
    opa_table = alpha4_opa_table;
    break;
  case LV_IMG_CF_ALPHA_8BIT:
    w = dsc->header.w; /*E.g. x = 7 -> w = 7 (bytes)*/
    ofs += w * y + x; /*First pixel*/
    pos = 0;
    break;
  }

  lv_gpu_decoder_data_t* user_data = dsc->user_data;
  uint8_t* fs_buf = lv_mem_buf_get(w);
  if (fs_buf == NULL)
    return LV_RES_INV;

  const uint8_t* data_tmp = NULL;
  if (dsc->src_type == LV_IMG_SRC_VARIABLE) {
    const lv_img_dsc_t* img_dsc = dsc->src;

    data_tmp = img_dsc->data + ofs;
  } else {
    lv_fs_seek(&user_data->f, ofs + 4, LV_FS_SEEK_SET); /*+4 to skip the header*/
    lv_fs_read(&user_data->f, fs_buf, w, NULL);
    data_tmp = fs_buf;
  }

  for (i = 0; i < len; i++) {
    uint8_t val_act = (*data_tmp >> pos) & mask;

    buf[i * LV_IMG_PX_SIZE_ALPHA_BYTE + LV_IMG_PX_SIZE_ALPHA_BYTE - 1] = dsc->header.cf == LV_IMG_CF_ALPHA_8BIT ? val_act : opa_table[val_act];

    pos -= px_size;
    if (pos < 0) {
      pos = 8 - px_size;
      data_tmp++;
    }
  }
  lv_mem_buf_release(fs_buf);
  return LV_RES_OK;
}

static lv_res_t lv_gpu_decoder_line_indexed(lv_img_decoder_dsc_t* dsc, lv_coord_t x, lv_coord_t y,
    lv_coord_t len, uint8_t* buf)
{
  uint8_t px_size = lv_img_cf_get_px_size(dsc->header.cf);
  uint16_t mask = (1 << px_size) - 1; /*E.g. px_size = 2; mask = 0x03*/

  lv_coord_t w = 0;
  int8_t pos = 0;
  uint32_t ofs = 0;
  switch (dsc->header.cf) {
  case LV_IMG_CF_INDEXED_1BIT:
    w = (dsc->header.w + 7) >> 3; /*E.g. w = 20 -> w = 2 + 1*/
    ofs += w * y + (x >> 3); /*First pixel*/
    ofs += 8; /*Skip the palette*/
    pos = 7 - (x & 0x7);
    break;
  case LV_IMG_CF_INDEXED_2BIT:
    w = (dsc->header.w + 3) >> 2; /*E.g. w = 13 -> w = 3 + 1 (bytes)*/
    ofs += w * y + (x >> 2); /*First pixel*/
    ofs += 16; /*Skip the palette*/
    pos = 6 - (x & 0x3) * 2;
    break;
  case LV_IMG_CF_INDEXED_4BIT:
    w = (dsc->header.w + 1) >> 1; /*E.g. w = 13 -> w = 6 + 1 (bytes)*/
    ofs += w * y + (x >> 1); /*First pixel*/
    ofs += 64; /*Skip the palette*/
    pos = 4 - (x & 0x1) * 4;
    break;
  case LV_IMG_CF_INDEXED_8BIT:
    w = dsc->header.w; /*E.g. x = 7 -> w = 7 (bytes)*/
    ofs += w * y + x; /*First pixel*/
    ofs += 1024; /*Skip the palette*/
    pos = 0;
    break;
  }

  lv_gpu_decoder_data_t* user_data = dsc->user_data;

  uint8_t* fs_buf = lv_mem_buf_get(w);
  if (fs_buf == NULL)
    return LV_RES_INV;
  const uint8_t* data_tmp = NULL;
  if (dsc->src_type == LV_IMG_SRC_VARIABLE) {
    const lv_img_dsc_t* img_dsc = dsc->src;
    data_tmp = img_dsc->data + ofs;
  } else {
    lv_fs_seek(&user_data->f, ofs + 4, LV_FS_SEEK_SET); /*+4 to skip the header*/
    lv_fs_read(&user_data->f, fs_buf, w, NULL);
    data_tmp = fs_buf;
  }

  lv_coord_t i;
  for (i = 0; i < len; i++) {
    uint8_t val_act = (*data_tmp >> pos) & mask;

    lv_color_t color = user_data->palette[val_act];
#if LV_COLOR_DEPTH == 8 || LV_COLOR_DEPTH == 1
    buf[i * LV_IMG_PX_SIZE_ALPHA_BYTE] = color.full;
#elif LV_COLOR_DEPTH == 16
    /*Because of Alpha byte 16 bit color can start on odd address which can cause crash*/
    buf[i * LV_IMG_PX_SIZE_ALPHA_BYTE] = color.full & 0xFF;
    buf[i * LV_IMG_PX_SIZE_ALPHA_BYTE + 1] = (color.full >> 8) & 0xFF;
#elif LV_COLOR_DEPTH == 32
    *((uint32_t*)&buf[i * LV_IMG_PX_SIZE_ALPHA_BYTE]) = color.full;
#else
#error "Invalid LV_COLOR_DEPTH. Check it in lv_conf.h"
#endif
    buf[i * LV_IMG_PX_SIZE_ALPHA_BYTE + LV_IMG_PX_SIZE_ALPHA_BYTE - 1] = user_data->opa[val_act];

    pos -= px_size;
    if (pos < 0) {
      pos = 8 - px_size;
      data_tmp++;
    }
  }
  lv_mem_buf_release(fs_buf);
  return LV_RES_OK;
}

vg_lite_buffer_t * lv_gpu_get_vgbuf(void *ptr)
{
  _lv_img_cache_entry_t * cache = LV_GC_ROOT(_lv_img_cache_array);
  int16_t entry_cnt = LV_IMG_CACHE_DEF_SIZE;
  for(int_fast16_t i = 0; i < entry_cnt; i++) {
    if(ptr == cache[i].dec_dsc.img_data) {
      lv_gpu_decoder_data_t * user_data = cache[i].dec_dsc.user_data;
      return &user_data->vgbuf;
    }
  }
  return NULL;
}
