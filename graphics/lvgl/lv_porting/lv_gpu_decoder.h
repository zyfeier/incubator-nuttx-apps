/**
 * @file lv_gpu_decoder.h
 *
 */

#ifndef LV_GPU_DECODER_H
#define LV_GPU_DECODER_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/
#include "../lv_conf_internal.h"
#include "../lvgl/src/draw/lv_img_decoder.h"
#include "vg_lite.h"
#include <lvgl/lvgl.h>

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/
typedef struct {
  uint32_t magic;
  vg_lite_buffer_t vgbuf;
} gpu_data_header_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/
/**
 * Initialize the image decoder module
 */
void lv_gpu_decoder_init(void);

/**
 * Open an image for GPU rendering (aligning to 16px and pre-multiplying alpha channel)
 * @param decoder the decoder where this function belongs
 * @param dsc pointer to decoder descriptor. `src`, `style` are already initialized in it.
 * @return LV_RES_OK: the info is successfully stored in `header`; LV_RES_INV: unknown format or other error.
 */
lv_res_t lv_gpu_decoder_open(lv_img_decoder_t* decoder, lv_img_decoder_dsc_t* dsc);

/**
 * Close the pending decoding. Free resources etc.
 * @param decoder pointer to the decoder the function associated with
 * @param dsc pointer to decoder descriptor
 */
void lv_gpu_decoder_close(lv_img_decoder_t* decoder, lv_img_decoder_dsc_t* dsc);

/**
 * Load an image into vg_lite buffer with automatic alignment. Appropriate room will be
 * allocated in vgbuf_p->memory, leaving the user responsible for cleaning it up.
 * @param img_data pointer to the pixel buffer
 * @param img_header header of the image containing width, height and color format
 * @param vgbuf_p address of the vg_lite_buffer_t structure to be initialized
 * @return LV_RES_OK: ok; LV_RES_INV: failed
 */
lv_res_t lv_gpu_load_vgbuf(const uint8_t* img_data, lv_img_header_t* header, vg_lite_buffer_t* vgbuf_p, uint8_t* buf_p);

/**
 * Get the vgbuf cache corresponding to the image pointer (if available).
 *
 * @param ptr pointer to the pixel buffer
 * @return pointer to the vg_lite_buffer_t structure in cache items, NULL if cache miss
 */
vg_lite_buffer_t* lv_gpu_get_vgbuf(void* data);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*LV_GPU_DECODER_H*/
