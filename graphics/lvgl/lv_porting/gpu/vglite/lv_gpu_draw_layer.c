/**
 * @file lv_gpu_draw_layer.c
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include "../../core/lv_refr.h"
#include "../../hal/lv_hal_disp.h"
#include "../../misc/lv_area.h"
#include "lv_draw_sw.h"
#include "../lv_gpu_draw_utils.h"
#include <stdlib.h>

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

typedef struct {
  lv_draw_layer_ctx_t base_draw;

  uint32_t buf_size_bytes : 31;
  uint32_t has_alpha : 1;
  lv_area_t area_aligned;
} lv_gpu_draw_layer_ctx_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *  GLOBAL VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

struct _lv_draw_layer_ctx_t* lv_gpu_draw_layer_create(struct _lv_draw_ctx_t* draw_ctx, lv_draw_layer_ctx_t* layer_ctx,
    lv_draw_layer_flags_t flags)
{
  lv_gpu_draw_layer_ctx_t* gpu_layer_ctx = (lv_gpu_draw_layer_ctx_t*)layer_ctx;
  uint32_t px_size = flags & LV_DRAW_LAYER_FLAG_HAS_ALPHA ? LV_IMG_PX_SIZE_ALPHA_BYTE : sizeof(lv_color_t);
  lv_coord_t w = lv_area_get_width(&gpu_layer_ctx->base_draw.area_full);
  if (!IS_ALIGNED(w, 16)) {
    flags |= LV_DRAW_LAYER_FLAG_HAS_ALPHA;
    w = ALIGN_UP(w, 16);
  }
  gpu_layer_ctx->area_aligned = gpu_layer_ctx->base_draw.area_full;
  gpu_layer_ctx->area_aligned.x2 = gpu_layer_ctx->area_aligned.x1 + w - 1;
  if (flags & LV_DRAW_LAYER_FLAG_CAN_SUBDIVIDE) {
    gpu_layer_ctx->buf_size_bytes = LV_LAYER_SIMPLE_BUF_SIZE;
    uint32_t full_size = lv_area_get_size(&gpu_layer_ctx->area_aligned) * px_size;
    if (gpu_layer_ctx->buf_size_bytes > full_size)
      gpu_layer_ctx->buf_size_bytes = full_size;
    gpu_layer_ctx->base_draw.buf = aligned_alloc(64, gpu_layer_ctx->buf_size_bytes);
    if (gpu_layer_ctx->base_draw.buf == NULL) {
      LV_LOG_WARN("Cannot allocate %" LV_PRIu32 " bytes for layer buffer. Allocating %" LV_PRIu32 " bytes instead. (Reduced performance)",
          (uint32_t)gpu_layer_ctx->buf_size_bytes, (uint32_t)LV_LAYER_SIMPLE_FALLBACK_BUF_SIZE * px_size);
      gpu_layer_ctx->buf_size_bytes = LV_LAYER_SIMPLE_FALLBACK_BUF_SIZE;
      gpu_layer_ctx->base_draw.buf = aligned_alloc(64, gpu_layer_ctx->buf_size_bytes);
      if (gpu_layer_ctx->base_draw.buf == NULL) {
        return NULL;
      }
    }
    gpu_layer_ctx->base_draw.area_act = gpu_layer_ctx->base_draw.area_full;
    gpu_layer_ctx->base_draw.area_act.y2 = gpu_layer_ctx->base_draw.area_full.y1;
    gpu_layer_ctx->base_draw.max_row_with_alpha = gpu_layer_ctx->buf_size_bytes / w / LV_IMG_PX_SIZE_ALPHA_BYTE;
    gpu_layer_ctx->base_draw.max_row_with_no_alpha = gpu_layer_ctx->buf_size_bytes / w / sizeof(lv_color_t);
  } else {
    gpu_layer_ctx->base_draw.area_act = gpu_layer_ctx->base_draw.area_full;
    gpu_layer_ctx->buf_size_bytes = lv_area_get_size(&gpu_layer_ctx->area_aligned) * px_size;
    gpu_layer_ctx->base_draw.buf = aligned_alloc(64, gpu_layer_ctx->buf_size_bytes);
    lv_memset_00(gpu_layer_ctx->base_draw.buf, gpu_layer_ctx->buf_size_bytes);
    gpu_layer_ctx->has_alpha = flags & LV_DRAW_LAYER_FLAG_HAS_ALPHA ? 1 : 0;
    if (gpu_layer_ctx->base_draw.buf == NULL) {
      return NULL;
    }

    draw_ctx->buf = gpu_layer_ctx->base_draw.buf;
    draw_ctx->buf_area = &gpu_layer_ctx->area_aligned;
    draw_ctx->clip_area = &gpu_layer_ctx->base_draw.area_act;
    lv_disp_t* disp_refr = _lv_refr_get_disp_refreshing();
    disp_refr->driver->screen_transp = flags & LV_DRAW_LAYER_FLAG_HAS_ALPHA ? 1 : 0;
  }

  return layer_ctx;
}

void lv_gpu_draw_layer_adjust(struct _lv_draw_ctx_t* draw_ctx, struct _lv_draw_layer_ctx_t* layer_ctx,
    lv_draw_layer_flags_t flags)
{
  lv_gpu_draw_layer_ctx_t* gpu_layer_ctx = (lv_gpu_draw_layer_ctx_t*)layer_ctx;
  lv_disp_t* disp_refr = _lv_refr_get_disp_refreshing();
  if (flags & LV_DRAW_LAYER_FLAG_HAS_ALPHA) {
    lv_memset_00(layer_ctx->buf, gpu_layer_ctx->buf_size_bytes);
    gpu_layer_ctx->has_alpha = 1;
    disp_refr->driver->screen_transp = 1;
  } else {
    gpu_layer_ctx->has_alpha = 0;
    disp_refr->driver->screen_transp = 0;
  }

  draw_ctx->buf = layer_ctx->buf;
  draw_ctx->buf_area = &gpu_layer_ctx->area_aligned;
  draw_ctx->clip_area = &layer_ctx->area_act;
  draw_ctx->buf_area->y1 = draw_ctx->clip_area->y1;
  draw_ctx->buf_area->y2 = draw_ctx->clip_area->y2;
}

void lv_gpu_draw_layer_blend(struct _lv_draw_ctx_t* draw_ctx, struct _lv_draw_layer_ctx_t* layer_ctx,
    const lv_draw_img_dsc_t* draw_dsc)
{
  lv_gpu_draw_layer_ctx_t* gpu_layer_ctx = (lv_gpu_draw_layer_ctx_t*)layer_ctx;

  lv_img_dsc_t img;
  img.data = draw_ctx->buf;
  img.header.always_zero = 0;
  img.header.w = lv_area_get_width(draw_ctx->buf_area);
  img.header.h = lv_area_get_height(draw_ctx->buf_area);
  img.header.cf = gpu_layer_ctx->has_alpha ? LV_IMG_CF_TRUE_COLOR_ALPHA : LV_IMG_CF_TRUE_COLOR;

  /*Restore the original draw_ctx*/
  draw_ctx->buf = layer_ctx->original.buf;
  draw_ctx->buf_area = layer_ctx->original.buf_area;
  draw_ctx->clip_area = layer_ctx->original.clip_area;
  lv_disp_t* disp_refr = _lv_refr_get_disp_refreshing();
  disp_refr->driver->screen_transp = layer_ctx->original.screen_transp;

  /*Blend the layer*/
  lv_draw_img(draw_ctx, draw_dsc, &gpu_layer_ctx->area_aligned, &img);
  lv_draw_wait_for_finish(draw_ctx);
  lv_img_cache_invalidate_src(&img);
}

void lv_gpu_draw_layer_destroy(lv_draw_ctx_t* draw_ctx, lv_draw_layer_ctx_t* layer_ctx)
{
  LV_UNUSED(draw_ctx);

  lv_mem_free(layer_ctx->buf);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/
