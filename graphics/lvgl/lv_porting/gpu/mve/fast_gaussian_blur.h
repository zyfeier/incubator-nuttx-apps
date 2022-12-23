#include <lvgl/lvgl.h>

/****************************************************************************
 * Name: fast_gaussian_blur
 *
 * Description:
 *   Do gaussian blur in an area.
 *
 * Input Parameters:
 * @param src source image
 * @param dst target buffer for the result
 * @param src_stride source image width
 * @param dst_stride target image width
 * @param blur_area blur area on source image, target area starts from dst(0,0)
 * @param r filter width, r=1 is accelerated by MVE
 *
 ****************************************************************************/
void fast_gaussian_blur(lv_color_t* dst, uint32_t dst_stride,
    lv_color_t* src, uint32_t src_stride, lv_area_t* blur_area, int r);
