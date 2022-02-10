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

/****************************************************************************
 * Private Type Declarations
 ****************************************************************************/

struct fbdev_obj_s
{
  lv_disp_draw_buf_t disp_draw_buf;
  lv_disp_drv_t disp_drv;
  bool color_match;
  bool double_buffer;
  int fd;
  struct fb_videoinfo_s vinfo;
  struct fb_planeinfo_s pinfo;
  FAR void *fbmem;
  FAR lv_disp_t *disp;
  FAR lv_color_t *last_buffer;
  lv_area_t inv_areas[LV_INV_BUF_SIZE];
  uint16_t inv_areas_len;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

#if defined(CONFIG_LV_FBDEV_USE_STATIC_BUFFER)
static lv_color_t g_fbdev_buffer[LV_FBDEV_BUFFER_SIZE]
                  LV_FBDEV_BUFFER_SECTION;
#endif

/****************************************************************************
 * Public Data
 ****************************************************************************/

/****************************************************************************
 * Private Functions
 ****************************************************************************/

#if !defined(CONFIG_LV_FBDEV_USE_DOUBLE_BUFFER)

/****************************************************************************
 * Name: fbdev_get_buffer
 ****************************************************************************/

static FAR lv_color_t *fbdev_get_buffer(int hor_res, int ver_res)
{
#if defined(CONFIG_LV_FBDEV_USE_STATIC_BUFFER)
  if (hor_res != CONFIG_LV_FBDEV_HOR_RES
      || ver_res != CONFIG_LV_FBDEV_VER_RES)
    {
      LV_LOG_ERROR("Resolution mismatch, fb: %dx%d, lvgl: %dx%d.",
                   hor_res, ver_res,
                   CONFIG_LV_FBDEV_HOR_RES, CONFIG_LV_FBDEV_VER_RES);
      return NULL;
    }

  return g_fbdev_buffer;
#else
  return malloc(hor_res * ver_res * sizeof(lv_color_t));
#endif
}

#else

/****************************************************************************
 * Name: fbdev_copy_area
 ****************************************************************************/

static void fbdev_copy_area(FAR lv_color_t *fb_dest,
                            FAR const lv_color_t *fb_src,
                            FAR const lv_area_t *areas,
                            uint16_t len,
                            int fb_width)
{
  LV_LOG_TRACE("%p -> %p, len = %d", fb_src, fb_dest, len);
  int i;

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

#endif

/****************************************************************************
 * Name: fbdev_flush_direct
 ****************************************************************************/

static void fbdev_flush_direct(FAR lv_disp_drv_t *disp_drv,
                               FAR const lv_area_t *area_p,
                               FAR lv_color_t *color_p)
{
  FAR struct fbdev_obj_s *fbdev_obj = disp_drv->user_data;

  if (lv_disp_flush_is_last(disp_drv))
    {
#if defined(CONFIG_LV_FBDEV_USE_DOUBLE_BUFFER)
      FAR lv_disp_t *disp_refr = fbdev_obj->disp;

      fbdev_obj->last_buffer = color_p;

      if (fbdev_obj->inv_areas_len != 0)
        {
          LV_LOG_ERROR("Repeated flush action detected! "
                       "inv_areas_len(%d) != 0",
                       fbdev_obj->inv_areas_len);
          fbdev_obj->inv_areas_len = 0;
        }

      uint16_t inv_index;

      for (inv_index = 0; inv_index < disp_refr->inv_p; inv_index++)
        {
          if (disp_refr->inv_area_joined[inv_index] == 0)
            {
              fbdev_obj->inv_areas[fbdev_obj->inv_areas_len] =
                  disp_refr->inv_areas[inv_index];
              fbdev_obj->inv_areas_len++;
            }
        }

      fbdev_obj->pinfo.yoffset = (color_p == fbdev_obj->fbmem)
                                 ? 0 : fbdev_obj->vinfo.yres;

      LV_LOG_TRACE("Commit buffer = %p, yoffset = %" PRIu32,
                  color_p,
                  fbdev_obj->pinfo.yoffset);

      ioctl(fbdev_obj->fd, FBIOPAN_DISPLAY,
            (unsigned long)((uintptr_t)&(fbdev_obj->pinfo)));

#else
      FAR void *target = fbdev_obj->fbmem;
      const size_t frame_size = fbdev_obj->vinfo.xres
                                * fbdev_obj->vinfo.yres
                                * sizeof(lv_color_t);

      if (fbdev_obj->double_buffer)
        {
          struct fb_planeinfo_s pinfo;
          int ret;
          ret = ioctl(fbdev_obj->fd, FBIOGET_PLANEINFO,
                      (unsigned long)((uintptr_t)&pinfo));

          if (ret == OK)
            {
              pinfo.yoffset = (pinfo.yoffset == 0)
                              ? fbdev_obj->vinfo.yres : 0;

              target = (FAR uint8_t *)fbdev_obj->fbmem
                        + pinfo.yoffset * pinfo.stride;
#if defined(CONFIG_LV_USE_GPU_INTERFACE)
              lv_gpu_color_fmt_convert_dsc_t dsc;
              dsc.dst = target;
              dsc.dst_bpp = pinfo.bpp;
              dsc.src = color_p;
              dsc.src_bpp = LV_COLOR_DEPTH;
              dsc.width = fbdev_obj->vinfo.xres;
              dsc.height = fbdev_obj->vinfo.yres;
              lv_gpu_color_fmt_convert(&dsc);
#else
              lv_memcpy(target, color_p, frame_size);
#endif /* CONFIG_LV_USE_GPU_INTERFACE */

              ioctl(fbdev_obj->fd, FBIOPAN_DISPLAY,
                    (unsigned long)((uintptr_t)&pinfo));
            }
        }
      else
        {
          lv_memcpy(target, color_p, frame_size);
        }

#endif /* CONFIG_LV_FBDEV_USE_DOUBLE_BUFFER */

#if defined(CONFIG_FB_UPDATE)
      struct fb_area_s fb_area;
      fb_area.x = area_p->x1;
      fb_area.y = area_p->y1;
      fb_area.w = area_p->x2 - area_p->x1 + 1;
      fb_area.h = area_p->y2 - area_p->y1 + 1;
      ioctl(fbdev_obj->fd, FBIO_UPDATE,
            (unsigned long)((uintptr_t)&fb_area));
#endif
    }

  /* Tell the flushing is ready */

  lv_disp_flush_ready(disp_drv);
}

#if !defined(CONFIG_LV_USE_GPU_INTERFACE)

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

#ifdef CONFIG_FB_UPDATE
  struct fb_area_s fb_area;
#endif

  const uint8_t bpp = fbdev_obj->pinfo.bpp;
  const fb_coord_t xres = fbdev_obj->vinfo.xres;
  if (bpp == 8)
    {
      int x;
      int y;
      FAR uint8_t *fbp = fbdev_obj->fbmem;
      for (y = y1; y <= y2; y++)
        {
          FAR uint8_t *fb_pos = fbp + (y * xres) + x1;
          for (x = x1; x <= x2; x++)
            {
              *fb_pos = lv_color_to8(*color_p);
              fb_pos++;
              color_p++;
            }
        }
    }
  else if (bpp == 16)
    {
      int x;
      int y;
      FAR uint16_t *fbp = fbdev_obj->fbmem;
      for (y = y1; y <= y2; y++)
        {
          FAR uint16_t *fb_pos = fbp + (y * xres) + x1;
          for (x = x1; x <= x2; x++)
            {
              *fb_pos = lv_color_to16(*color_p);
              fb_pos++;
              color_p++;
            }
        }
    }
  else if (bpp == 32 || bpp == 24)
    {
      int x;
      int y;
      FAR uint32_t *fbp = fbdev_obj->fbmem;
      for (y = y1; y <= y2; y++)
        {
          FAR uint32_t *fb_pos = fbp + (y * xres) + x1;
          for (x = x1; x <= x2; x++)
            {
              *fb_pos = lv_color_to32(*color_p);
              fb_pos++;
              color_p++;
            }
        }
    }

#ifdef CONFIG_FB_UPDATE
  fb_area.x = x1;
  fb_area.y = y1;
  fb_area.w = x2 - x1 + 1;
  fb_area.h = y2 - y1 + 1;
  ioctl(fbdev_obj->fd, FBIO_UPDATE,
        (unsigned long)((uintptr_t)&fb_area));
#endif

  /* Tell the flushing is ready */

  lv_disp_flush_ready(disp_drv);
}

#endif /* CONFIG_LV_USE_GPU_INTERFACE */

#if defined(CONFIG_LV_FBDEV_USE_DOUBLE_BUFFER)

/****************************************************************************
 * Name: fbdev_check_sync
 ****************************************************************************/

static bool fbdev_check_sync(FAR struct fbdev_obj_s *fbdev_obj)
{
  int ret = ERROR;
  LV_LOG_TRACE("Check sync");

#ifdef CONFIG_FB_SYNC
  ret = ioctl(fbdev_obj->fd, FBIO_WAITFORVSYNC, NULL);
#endif

  if (ret != OK)
    {
      LV_LOG_TRACE("No sync signal detect");
      return false;
    }

  LV_LOG_TRACE("Sync ok!, disp refr start");
  return true;
}

/****************************************************************************
 * Name: fbdev_disp_refr
 ****************************************************************************/

static void fbdev_disp_refr(FAR lv_timer_t *timer)
{
  FAR struct fbdev_obj_s *fbdev_obj = timer->user_data;
  FAR lv_disp_t *disp = fbdev_obj->disp;

  if (!fbdev_check_sync(fbdev_obj))
    {
      return;
    }

  if (fbdev_obj->inv_areas_len != 0)
    {
      FAR lv_disp_draw_buf_t *draw_buf = lv_disp_get_draw_buf(disp);

      fbdev_copy_area(draw_buf->buf_act, fbdev_obj->last_buffer,
                      fbdev_obj->inv_areas, fbdev_obj->inv_areas_len,
                      fbdev_obj->vinfo.xres);

      fbdev_obj->inv_areas_len = 0;
    }

  _lv_disp_refr_timer(NULL);
}

#endif /* CONFIG_LV_FBDEV_USE_DOUBLE_BUFFER */

/****************************************************************************
 * Name: fbdev_init
 ****************************************************************************/

static FAR lv_disp_t *fbdev_init(FAR struct fbdev_obj_s *state,
                                 int hor_res, int ver_res)
{
  FAR struct fbdev_obj_s *fbdev_obj = malloc(sizeof(struct fbdev_obj_s));
  FAR lv_color_t *buf;

  if (fbdev_obj == NULL)
    {
      LV_LOG_ERROR("fbdev_obj_s malloc failed");
      return NULL;
    }

  *fbdev_obj = *state;

  lv_disp_drv_init(&(fbdev_obj->disp_drv));

#if defined(CONFIG_LV_FBDEV_USE_DOUBLE_BUFFER)
  if (!fbdev_obj->double_buffer)
    {
      LV_LOG_ERROR("fbdev does not support double buffering");
      return NULL;
    }

  if (!fbdev_obj->color_match)
    {
      LV_LOG_ERROR("fbdev and lvgl color depth do not match");
      return NULL;
    }

  buf = fbdev_obj->fbmem;

  lv_disp_draw_buf_init(&(fbdev_obj->disp_draw_buf),
                        buf, buf + hor_res * ver_res,
                        hor_res * ver_res);

  fbdev_obj->disp_drv.direct_mode = 1;
  fbdev_obj->disp_drv.flush_cb = fbdev_flush_direct;

#else
  buf = fbdev_get_buffer(hor_res, ver_res);

  if (buf == NULL)
    {
      LV_LOG_ERROR("Unable to get buffer");
      return NULL;
    }

  lv_disp_draw_buf_init(&(fbdev_obj->disp_draw_buf), buf, NULL,
                        hor_res * ver_res);

#if defined(CONFIG_LV_USE_GPU_INTERFACE)
  fbdev_obj->disp_drv.direct_mode = 1;
  fbdev_obj->disp_drv.flush_cb = fbdev_flush_direct;
#else
  fbdev_obj->disp_drv.direct_mode = fbdev_obj->color_match;
  fbdev_obj->disp_drv.flush_cb = fbdev_obj->color_match
                                 ? fbdev_flush_direct
                                 : fbdev_flush_convert;
#endif /* CONFIG_LV_USE_GPU_INTERFACE */

#endif /* CONFIG_LV_FBDEV_USE_DOUBLE_BUFFER */

  fbdev_obj->disp_drv.draw_buf = &(fbdev_obj->disp_draw_buf);
  fbdev_obj->disp_drv.hor_res = hor_res;
  fbdev_obj->disp_drv.ver_res = ver_res;
#if ( LV_USE_USER_DATA != 0 )
  fbdev_obj->disp_drv.user_data = fbdev_obj;
#else
#error LV_USE_USER_DATA must be enabled
#endif

  fbdev_obj->disp = lv_disp_drv_register(&(fbdev_obj->disp_drv));

#if defined(CONFIG_LV_FBDEV_USE_DOUBLE_BUFFER)
  FAR lv_timer_t *refr_timer = _lv_disp_get_refr_timer(fbdev_obj->disp);
  lv_timer_set_cb(refr_timer, NULL);
  lv_timer_pause(refr_timer);
  lv_timer_create(fbdev_disp_refr, 1, fbdev_obj);
  LV_LOG_INFO("Enable direct mode in double framebuffer");
#endif /* CONFIG_LV_FBDEV_USE_DOUBLE_BUFFER */

  return fbdev_obj->disp;
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

  disp = fbdev_init(&state, state.vinfo.xres, state.vinfo.yres);

  if (disp == NULL)
    {
      munmap(state.fbmem, state.pinfo.fblen);
      close(state.fd);
    }

  return disp;
}
