/****************************************************************************
 * apps/graphics/lvgl/lv_porting/lv_fbdev_interface.c
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

#include <nuttx/config.h>
#include <nuttx/video/fb.h>
#include <nuttx/video/rgbcolors.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <debug.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <lv_porting/lv_porting.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#if defined(CONFIG_LV_FBDEV_USE_STATIC_BUFFER)

#define LV_FBDEV_BUFFER_SIZE \
       (CONFIG_LV_FBDEV_HOR_RES * CONFIG_LV_FBDEV_VER_RES)

#if defined(CONFIG_LV_FBDEV_USE_CUSTOM_SECTION)
#define LV_FBDEV_BUFFER_SECTION \
        __attribute__((section(CONFIG_LV_FBDEV_SECTION_NAME)))
#else
#define LV_FBDEV_BUFFER_SECTION
#endif

#endif

#if defined(CONFIG_FB_UPDATE)
#define FBDEV_UPDATE_AREA(obj, area) fbdev_update_area(obj, area)
#else
#define FBDEV_UPDATE_AREA(obj, area)
#endif

/****************************************************************************
 * Private Type Declarations
 ****************************************************************************/

struct fbdev_obj_s
{
  lv_disp_draw_buf_t disp_draw_buf;
  lv_disp_drv_t disp_drv;
  FAR lv_disp_t *disp;
  FAR void *last_buffer;
  FAR void *act_buffer;
  lv_area_t inv_areas[LV_INV_BUF_SIZE];
  uint16_t inv_areas_len;
  lv_area_t final_area;

  int fd;
  FAR void *fbmem;
  struct fb_videoinfo_s vinfo;
  struct fb_planeinfo_s pinfo;

  bool color_match;
  bool double_buffer;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

/****************************************************************************
 * Public Data
 ****************************************************************************/

/****************************************************************************
 * Private Functions
 ****************************************************************************/

#if defined(CONFIG_FB_UPDATE)
static void fbdev_update_area(FAR struct fbdev_obj_s *fbdev_obj,
                              FAR const lv_area_t *area_p)
{
  struct fb_area_s fb_area;
  fb_area.x = area_p->x1;
  fb_area.y = area_p->y1;
  fb_area.w = area_p->x2 - area_p->x1 + 1;
  fb_area.h = area_p->y2 - area_p->y1 + 1;

  LV_LOG_TRACE("area: (%d, %d) %d x %d",
               fb_area.x, fb_area.y, fb_area.w, fb_area.h);

  ioctl(fbdev_obj->fd, FBIO_UPDATE,
        (unsigned long)((uintptr_t)&fb_area));

  LV_LOG_TRACE("finished");
}
#endif

/****************************************************************************
 * Name: fbdev_copy_areas
 ****************************************************************************/

static void fbdev_copy_areas(FAR lv_color_t *fb_dest,
                             FAR const lv_color_t *fb_src,
                             FAR const lv_area_t *areas,
                             uint16_t len,
                             int fb_width)
{
  int i;
  LV_LOG_TRACE("%p -> %p, len = %d", fb_src, fb_dest, len);

  for (i = 0; i < len; i++)
    {
      int y;
      FAR const lv_area_t *area = &(areas[i]);
      int width = lv_area_get_width(area);
      int height = lv_area_get_height(area);
      FAR lv_color_t *dest_pos =
                      fb_dest + area->y1 * fb_width + area->x1;
      FAR const lv_color_t *src_pos =
                            fb_src + area->y1 * fb_width + area->x1;
      size_t hor_size = width * sizeof(lv_color_t);

      LV_LOG_TRACE("area[%d]: (%d, %d) %d x %d",
                   i, area->x1, area->y1, width, height);

      for (y = 0; y < height; y++)
        {
          lv_memcpy(dest_pos, src_pos, hor_size);
          dest_pos += fb_width;
          src_pos += fb_width;
        }
    }
}

/****************************************************************************
 * Name: fbdev_switch_buffer
 ****************************************************************************/

static void fbdev_switch_buffer(FAR struct fbdev_obj_s *fbdev_obj)
{
  FAR lv_disp_t *disp_refr = fbdev_obj->disp;
  uint16_t inv_index;

  /* check inv_areas_len, it must == 0 */

  if (fbdev_obj->inv_areas_len != 0)
    {
      LV_LOG_ERROR("Repeated flush action detected! "
                    "inv_areas_len(%d) != 0",
                    fbdev_obj->inv_areas_len);
      fbdev_obj->inv_areas_len = 0;
    }

  /* Save dirty area table for next synchronizationn */

  for (inv_index = 0; inv_index < disp_refr->inv_p; inv_index++)
    {
      if (disp_refr->inv_area_joined[inv_index] == 0)
        {
          fbdev_obj->inv_areas[fbdev_obj->inv_areas_len] =
              disp_refr->inv_areas[inv_index];
          fbdev_obj->inv_areas_len++;
        }
    }

  /* Save the buffer address for the next synchronization */

  fbdev_obj->last_buffer = fbdev_obj->act_buffer;

  LV_LOG_TRACE("Commit buffer = %p, yoffset = %" PRIu32,
               fbdev_obj->act_buffer,
               fbdev_obj->pinfo.yoffset);

  if (fbdev_obj->act_buffer == fbdev_obj->fbmem)
    {
      fbdev_obj->pinfo.yoffset = 0;
      fbdev_obj->act_buffer = fbdev_obj->fbmem
          + fbdev_obj->vinfo.yres * fbdev_obj->pinfo.stride;
    }
  else
    {
      fbdev_obj->pinfo.yoffset = fbdev_obj->vinfo.yres;
      fbdev_obj->act_buffer = fbdev_obj->fbmem;
    }

  /* Commit buffer to fb driver */

  ioctl(fbdev_obj->fd, FBIOPAN_DISPLAY,
        (unsigned long)((uintptr_t)&(fbdev_obj->pinfo)));

  LV_LOG_TRACE("finished");
}

#ifdef CONFIG_FB_SYNC

/****************************************************************************
 * Name: fbdev_check_sync
 ****************************************************************************/

static bool fbdev_check_sync(FAR struct fbdev_obj_s *fbdev_obj)
{
  int ret;
  LV_LOG_TRACE("Check sync");

  ret = ioctl(fbdev_obj->fd, FBIO_WAITFORVSYNC, NULL);
  if (ret != OK)
    {
      LV_LOG_TRACE("No sync signal detect");
      return false;
    }

  LV_LOG_TRACE("Sync ok!, disp refr start");
  return true;
}

#endif /* CONFIG_FB_SYNC */

/****************************************************************************
 * Name: fbdev_disp_refr
 ****************************************************************************/

static void fbdev_disp_refr(FAR lv_timer_t *timer)
{
#ifdef CONFIG_FB_SYNC
  FAR struct fbdev_obj_s *fbdev_obj = timer->user_data;

  if (!fbdev_check_sync(fbdev_obj))
    {
      return;
    }
#endif

  _lv_disp_refr_timer(NULL);
}

/****************************************************************************
 * Name: fbdev_render_start
 ****************************************************************************/

static void fbdev_render_start(FAR lv_disp_drv_t *disp_drv)
{
  FAR struct fbdev_obj_s *fbdev_obj = disp_drv->user_data;
  FAR lv_disp_t *disp_refr;
  lv_coord_t hor_res;
  lv_coord_t ver_res;
  int i;

  /* No need sync buffer */

  if (fbdev_obj->inv_areas_len == 0)
    {
      return;
    }

  disp_refr = _lv_refr_get_disp_refreshing();

#if !defined(CONFIG_LV_FBDEV_USE_DOUBLE_BUFFER)
  if (disp_refr->driver->sw_rotate)
    {
      LV_LOG_TRACE("Copy full screen buffer %p -> %p",
                   fbdev_obj->last_buffer, fbdev_obj->act_buffer);
      lv_memcpy(fbdev_obj->act_buffer,
                fbdev_obj->last_buffer,
                fbdev_obj->vinfo.xres
                * fbdev_obj->vinfo.yres * sizeof(lv_color_t));
      fbdev_obj->inv_areas_len = 0;
      return;
    }
#endif

  hor_res = LV_HOR_RES;
  ver_res = LV_VER_RES;

  for (i = 0; i < disp_refr->inv_p; i++)
    {
      if (disp_refr->inv_area_joined[i] == 0)
        {
          FAR const lv_area_t *area_p = &disp_refr->inv_areas[i];

          /* If a full screen redraw is detected, skip dirty areas sync */

          if (lv_area_get_width(area_p) == hor_res
           && lv_area_get_height(area_p) == ver_res)
            {
              LV_LOG_TRACE("Full screen redraw, skip dirty areas sync");
              fbdev_obj->inv_areas_len = 0;
              return;
            }
        }
    }

  /* Sync the dirty area of ​​the previous frame */

  fbdev_copy_areas(fbdev_obj->act_buffer, fbdev_obj->last_buffer,
                   fbdev_obj->inv_areas, fbdev_obj->inv_areas_len,
                   fbdev_obj->vinfo.xres);

  fbdev_obj->inv_areas_len = 0;
}

#if defined(CONFIG_LV_FBDEV_USE_DOUBLE_BUFFER)

/****************************************************************************
 * Name: fbdev_flush_direct
 ****************************************************************************/

static void fbdev_flush_direct(FAR lv_disp_drv_t *disp_drv,
                               FAR const lv_area_t *area_p,
                               FAR lv_color_t *color_p)
{
  FAR struct fbdev_obj_s *fbdev_obj = disp_drv->user_data;

  /* Commit the buffer after the last flush */

  if (!lv_disp_flush_is_last(disp_drv))
    {
      lv_disp_flush_ready(disp_drv);
      return;
    }

  fbdev_switch_buffer(fbdev_obj);

  FBDEV_UPDATE_AREA(fbdev_obj, area_p);

  /* Tell the flushing is ready */

  lv_disp_flush_ready(disp_drv);
}

#else

/****************************************************************************
 * Name: fbdev_get_buffer
 ****************************************************************************/

static FAR lv_color_t *fbdev_get_buffer(int hor_res, int ver_res)
{
  FAR lv_color_t *retval;
#if defined(CONFIG_LV_FBDEV_USE_STATIC_BUFFER)
  static lv_color_t buffer[LV_FBDEV_BUFFER_SIZE] LV_FBDEV_BUFFER_SECTION;

  /* Check if buffer size match */

  if (hor_res != CONFIG_LV_FBDEV_HOR_RES
      || ver_res != CONFIG_LV_FBDEV_VER_RES)
    {
      LV_LOG_ERROR("Resolution mismatch, fb: %dx%d, lvgl: %dx%d.",
                   hor_res, ver_res,
                   CONFIG_LV_FBDEV_HOR_RES, CONFIG_LV_FBDEV_VER_RES);
      return NULL;
    }

  retval = buffer;
#else
  retval = malloc(hor_res * ver_res * sizeof(lv_color_t));
#endif
  LV_LOG_TRACE("%p", retval);
  return retval;
}

/****************************************************************************
 * Name: fbdev_update_part
 ****************************************************************************/

static void fbdev_update_part(FAR struct fbdev_obj_s *fbdev_obj,
                              FAR lv_disp_drv_t *disp_drv,
                              FAR const lv_area_t *area_p)
{
  FAR lv_area_t *final_area = &fbdev_obj->final_area;

  if (final_area->x1 < 0)
    {
      *final_area = *area_p;
    }
  else
    {
      _lv_area_join(final_area, final_area, area_p);
    }

  if (!lv_disp_flush_is_last(disp_drv))
    {
      lv_disp_flush_ready(disp_drv);
      return;
    }

  if (fbdev_obj->double_buffer)
    {
      fbdev_switch_buffer(fbdev_obj);
    }

  FBDEV_UPDATE_AREA(fbdev_obj, final_area);

  /* Mark it is invalid */

  final_area->x1 = -1;

  /* Tell the flushing is ready */

  lv_disp_flush_ready(disp_drv);
}

/****************************************************************************
 * Name: fbdev_flush_normal
 ****************************************************************************/

static void fbdev_flush_normal(FAR lv_disp_drv_t *disp_drv,
                               FAR const lv_area_t *area_p,
                               FAR lv_color_t *color_p)
{
  FAR struct fbdev_obj_s *fbdev_obj = disp_drv->user_data;
  int x1 = area_p->x1;
  int y1 = area_p->y1;
  int y2 = area_p->y2;
  int y;
  const int w = lv_area_get_width(area_p);
  const int hor_size = w * sizeof(lv_color_t);
  const fb_coord_t xres = fbdev_obj->vinfo.xres;
  FAR lv_color_t *fbp = fbdev_obj->act_buffer;

  for (y = y1; y <= y2; y++)
    {
      FAR lv_color_t *cur_pos = fbp + (y * xres) + x1;
      lv_memcpy(cur_pos, color_p, hor_size);
      color_p += w;
    }

  fbdev_update_part(fbdev_obj, disp_drv, area_p);
}

/****************************************************************************
 * Name: fbdev_flush_convert
 ****************************************************************************/

static void fbdev_flush_convert(FAR lv_disp_drv_t *disp_drv,
                                FAR const lv_area_t *area_p,
                                FAR lv_color_t *color_p)
{
  FAR struct fbdev_obj_s *fbdev_obj = disp_drv->user_data;
  int x1 = area_p->x1;
  int y1 = area_p->y1;
  int x2 = area_p->x2;
  int y2 = area_p->y2;

  const uint8_t bpp = fbdev_obj->pinfo.bpp;
  const fb_coord_t xres = fbdev_obj->vinfo.xres;
  int x;
  int y;

  switch (bpp)
    {
      case 32:
      case 24:
        {
          FAR uint32_t *fbp = fbdev_obj->act_buffer;
          for (y = y1; y <= y2; y++)
            {
              FAR uint32_t *cur_pos = fbp + (y * xres) + x1;
              for (x = x1; x <= x2; x++)
                {
                  *cur_pos = lv_color_to32(*color_p);
                  cur_pos++;
                  color_p++;
                }
            }
        }
        break;
      case 16:
        {
          FAR uint16_t *fbp = fbdev_obj->act_buffer;
          for (y = y1; y <= y2; y++)
            {
              FAR uint16_t *cur_pos = fbp + (y * xres) + x1;
              for (x = x1; x <= x2; x++)
                {
                  *cur_pos = lv_color_to16(*color_p);
                  cur_pos++;
                  color_p++;
                }
            }
        }
        break;
      case 8:
        {
          FAR uint8_t *fbp = fbdev_obj->act_buffer;
          for (y = y1; y <= y2; y++)
            {
              FAR uint8_t *cur_pos = fbp + (y * xres) + x1;
              for (x = x1; x <= x2; x++)
                {
                  *cur_pos = lv_color_to8(*color_p);
                  cur_pos++;
                  color_p++;
                }
            }
        }
        break;
      default:
        break;
    }

  fbdev_update_part(fbdev_obj, disp_drv, area_p);
}

#endif /* CONFIG_LV_FBDEV_USE_DOUBLE_BUFFER */

/****************************************************************************
 * Name: fbdev_init
 ****************************************************************************/

static FAR lv_disp_t *fbdev_init(FAR struct fbdev_obj_s *state)
{
  FAR struct fbdev_obj_s *fbdev_obj = malloc(sizeof(struct fbdev_obj_s));
  FAR lv_disp_drv_t *disp_drv;
  FAR lv_color_t *buf;
  int hor_res = state->vinfo.xres;
  int ver_res = state->vinfo.yres;

  if (fbdev_obj == NULL)
    {
      LV_LOG_ERROR("fbdev_obj_s malloc failed");
      return NULL;
    }

  *fbdev_obj = *state;
  disp_drv = &(fbdev_obj->disp_drv);

  lv_disp_drv_init(disp_drv);
  disp_drv->draw_buf = &(fbdev_obj->disp_draw_buf);
  disp_drv->hor_res = hor_res;
  disp_drv->ver_res = ver_res;
  disp_drv->screen_transp = false;
  disp_drv->user_data = fbdev_obj;

#if defined(CONFIG_LV_USE_GPU_INTERFACE)
  disp_drv->draw_ctx_init = lv_gpu_draw_ctx_init;
  disp_drv->draw_ctx_size = sizeof(gpu_draw_ctx_t);
#endif /* CONFIG_LV_USE_GPU_INTERFACE */

#if defined(CONFIG_LV_FBDEV_USE_DOUBLE_BUFFER)
  if (!fbdev_obj->double_buffer)
    {
      LV_LOG_ERROR("fbdev does not support double buffering");
      goto failed;
    }

  if (!fbdev_obj->color_match)
    {
      LV_LOG_ERROR("fbdev and lvgl color depth do not match");
      goto failed;
    }

  buf = fbdev_obj->fbmem;

  lv_disp_draw_buf_init(&(fbdev_obj->disp_draw_buf),
                        buf, buf + hor_res * ver_res,
                        hor_res * ver_res);

  disp_drv->direct_mode = true;
  disp_drv->flush_cb = fbdev_flush_direct;
#else
  buf = fbdev_get_buffer(hor_res, ver_res);

  if (!buf)
    {
      LV_LOG_ERROR("Unable to get buffer");
      goto failed;
    }

  lv_disp_draw_buf_init(&(fbdev_obj->disp_draw_buf), buf, NULL,
                        hor_res * ver_res);

  disp_drv->flush_cb = fbdev_obj->color_match
                        ? fbdev_flush_normal
                        : fbdev_flush_convert;

#if defined(CONFIG_LV_FBDEV_ROTATE_90)
  disp_drv->sw_rotate = true;
  disp_drv->rotated = LV_DISP_ROT_90;
#elif defined(CONFIG_LV_FBDEV_ROTATE_180)
  disp_drv->sw_rotate = true;
  disp_drv->rotated = LV_DISP_ROT_180;
#elif defined(CONFIG_LV_FBDEV_ROTATE_270)
  disp_drv->sw_rotate = true;
  disp_drv->rotated = LV_DISP_ROT_270;
#endif

#endif /* CONFIG_LV_FBDEV_USE_DOUBLE_BUFFER */

  if (fbdev_obj->double_buffer)
    {
      disp_drv->render_start_cb = fbdev_render_start;
    }

  fbdev_obj->act_buffer = fbdev_obj->fbmem;
  fbdev_obj->disp = lv_disp_drv_register(&(fbdev_obj->disp_drv));

  /* If double buffer is supported, use active refresh method */

  if (fbdev_obj->double_buffer)
    {
      FAR lv_timer_t *refr_timer = _lv_disp_get_refr_timer(fbdev_obj->disp);
      lv_timer_del(refr_timer);
      fbdev_obj->disp->refr_timer = NULL;
      lv_timer_create(fbdev_disp_refr,
                      LV_DISP_DEF_REFR_PERIOD / 4,
                      fbdev_obj);
      LV_LOG_INFO("Enable double framebuffer mode");
    }

  return fbdev_obj->disp;

failed:
  free(fbdev_obj);
  return NULL;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: lv_fbdev_interface_init
 *
 * Description:
 *   Framebuffer device interface initialization.
 *
 * Input Parameters:
 *   dev_path - Framebuffer device path, set to NULL to use the default path.
 *
 * Returned Value:
 *   lv_disp object address on success; NULL on failure.
 *
 ****************************************************************************/

FAR lv_disp_t *lv_fbdev_interface_init(FAR const char *dev_path,
                                       int line_buf)
{
  FAR const char *device_path = dev_path;
  struct fbdev_obj_s state;
  int ret;
  FAR lv_disp_t *disp;

  memset(&state, 0, sizeof(state));

  if (device_path == NULL)
    {
      device_path = CONFIG_LV_FBDEV_INTERFACE_DEFAULT_DEVICEPATH;
    }

  LV_LOG_INFO("fbdev %s opening", device_path);

  state.fd = open(device_path, O_RDWR);
  if (state.fd < 0)
    {
      LV_LOG_ERROR("fbdev %s open failed: %d", device_path, errno);
      return NULL;
    }

  /* Get the characteristics of the framebuffer */

  ret = ioctl(state.fd, FBIOGET_VIDEOINFO,
              (unsigned long)((uintptr_t)&state.vinfo));
  if (ret < 0)
    {
      LV_LOG_ERROR("ioctl(FBIOGET_VIDEOINFO) failed: %d", errno);
      close(state.fd);
      return NULL;
    }

  LV_LOG_INFO("VideoInfo:");
  LV_LOG_INFO("      fmt: %u", state.vinfo.fmt);
  LV_LOG_INFO("     xres: %u", state.vinfo.xres);
  LV_LOG_INFO("     yres: %u", state.vinfo.yres);
  LV_LOG_INFO("  nplanes: %u", state.vinfo.nplanes);

  ret = ioctl(state.fd, FBIOGET_PLANEINFO,
              (unsigned long)((uintptr_t)&state.pinfo));
  if (ret < 0)
    {
      LV_LOG_ERROR("ERROR: ioctl(FBIOGET_PLANEINFO) failed: %d", errno);
      close(state.fd);
      return NULL;
    }

  LV_LOG_INFO("PlaneInfo (plane 0):");
  LV_LOG_INFO("    fbmem: %p", state.pinfo.fbmem);
  LV_LOG_INFO("    fblen: %lu", (unsigned long)state.pinfo.fblen);
  LV_LOG_INFO("   stride: %u", state.pinfo.stride);
  LV_LOG_INFO("  display: %u", state.pinfo.display);
  LV_LOG_INFO("      bpp: %u", state.pinfo.bpp);

  /* Only these pixel depths are supported.  viinfo.fmt is ignored, only
   * certain color formats are supported.
   */

  if (state.pinfo.bpp != 32 && state.pinfo.bpp != 16 &&
      state.pinfo.bpp != 8  && state.pinfo.bpp != 1)
    {
      LV_LOG_ERROR("bpp = %u not supported", state.pinfo.bpp);
      close(state.fd);
      return NULL;
    }

  if (state.pinfo.bpp == LV_COLOR_DEPTH ||
      (state.pinfo.bpp == 24 && LV_COLOR_DEPTH == 32))
    {
      state.color_match = true;
    }
  else
    {
      LV_LOG_WARN("fbdev bpp = %d, LV_COLOR_DEPTH = %d, "
                  "color depth does not match.",
                  state.pinfo.bpp, LV_COLOR_DEPTH);
      state.color_match = false;
    }

  state.double_buffer = (state.pinfo.yres_virtual == (state.vinfo.yres * 2));

  /* mmap() the framebuffer.
   *
   * NOTE: In the FLAT build the frame buffer address returned by the
   * FBIOGET_PLANEINFO IOCTL command will be the same as the framebuffer
   * address.  mmap(), however, is the preferred way to get the framebuffer
   * address because in the KERNEL build, it will perform the necessary
   * address mapping to make the memory accessible to the application.
   */

  state.fbmem = mmap(NULL, state.pinfo.fblen, PROT_READ | PROT_WRITE,
                     MAP_SHARED | MAP_FILE, state.fd, 0);
  if (state.fbmem == MAP_FAILED)
    {
      LV_LOG_ERROR("ioctl(FBIOGET_PLANEINFO) failed: %d", errno);
      close(state.fd);
      return NULL;
    }

  LV_LOG_INFO("Mapped FB: %p", state.fbmem);

  disp = fbdev_init(&state);

  if (disp == NULL)
    {
      munmap(state.fbmem, state.pinfo.fblen);
      close(state.fd);
    }

  return disp;
}
