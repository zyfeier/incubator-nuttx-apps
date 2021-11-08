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
#include "lv_fbdev_interface.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Private Type Declarations
 ****************************************************************************/

struct fbdev_obj_s
{
  int fd;
  struct fb_videoinfo_s vinfo;
  struct fb_planeinfo_s pinfo;
  FAR void *fbmem;
  CODE int (*flush_cb)(FAR struct fbdev_obj_s *);
  lv_disp_draw_buf_t disp_draw_buf;
  lv_disp_drv_t disp_drv;
  lv_area_t area;
  FAR lv_color_t *color_p;

  pthread_t write_thread;
  sem_t flush_sem;
  sem_t wait_sem;
};

/****************************************************************************
 * Public Data
 ****************************************************************************/

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: fbdev_flush_convert
 ****************************************************************************/

static int fbdev_flush_convert(FAR struct fbdev_obj_s *fbdev_obj)
{
  int ret = OK;
  int x1 = fbdev_obj->area.x1;
  int y1 = fbdev_obj->area.y1;
  int x2 = fbdev_obj->area.x2;
  int y2 = fbdev_obj->area.y2;

#ifdef CONFIG_FB_UPDATE
  struct fb_area_s fb_area;
#endif

  FAR lv_color_t *color_p = fbdev_obj->color_p;
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
  ret = ioctl(fbdev_obj->fd, FBIO_UPDATE,
              (unsigned long)((uintptr_t)&fb_area));
#endif

  return ret;
}

/****************************************************************************
 * Name: fbdev_flush_direct
 ****************************************************************************/

static int fbdev_flush_direct(FAR struct fbdev_obj_s *fbdev_obj)
{
  int ret = OK;
  int x1 = fbdev_obj->area.x1;
  int y1 = fbdev_obj->area.y1;
  int x2 = fbdev_obj->area.x2;
  int y2 = fbdev_obj->area.y2;

#ifdef CONFIG_FB_UPDATE
  struct fb_area_s fb_area;
#endif

  FAR lv_color_t *color_p = fbdev_obj->color_p;
  const uint8_t bpp = fbdev_obj->pinfo.bpp;
  const fb_coord_t xres = fbdev_obj->vinfo.xres;
  const uint32_t width = x2 - x1 + 1;

  if (bpp == 8)
    {
      int y;
      FAR uint8_t *fbp8 = fbdev_obj->fbmem;
      const uint32_t hor_bytes = width * sizeof(uint8_t);

      for (y = y1; y <= y2; y++)
        {
          FAR uint8_t *fb_pos = fbp8 + (y * xres) + x1;
          lv_memcpy(fb_pos, color_p, hor_bytes);
          color_p += width;
        }
    }
  else if (bpp == 16)
    {
      int y;
      FAR uint16_t *fbp16 = fbdev_obj->fbmem;
      const uint32_t hor_bytes = width * sizeof(uint16_t);

      for (y = y1; y <= y2; y++)
        {
          FAR uint16_t *fb_pos = fbp16 + (y * xres) + x1;
          lv_memcpy(fb_pos, color_p, hor_bytes);
          color_p += width;
        }
    }
  else if (bpp == 32 || bpp == 24)
    {
      int y;
      FAR uint32_t *fbp32 = fbdev_obj->fbmem;
      const uint32_t hor_bytes = width * sizeof(uint32_t);

      for (y = y1; y <= y2; y++)
        {
          FAR uint32_t *fb_pos = fbp32 + (y * xres) + x1;
          lv_memcpy(fb_pos, color_p, hor_bytes);
          color_p += width;
        }
    }

#ifdef CONFIG_FB_UPDATE
  fb_area.x = x1;
  fb_area.y = y1;
  fb_area.w = x2 - x1 + 1;
  fb_area.h = y2 - y1 + 1;
  ret = ioctl(fbdev_obj->fd, FBIO_UPDATE,
              (unsigned long)((uintptr_t)&fb_area));
#endif

  return ret;
}

/****************************************************************************
 * Name: fbdev_update_thread
 ****************************************************************************/

static FAR void *fbdev_update_thread(FAR void *arg)
{
  int ret = OK;
  int errcode;
  FAR struct fbdev_obj_s *fbdev_obj = arg;

  while (ret == OK)
    {
      sem_wait(&(fbdev_obj->flush_sem));

      ret = fbdev_obj->flush_cb(fbdev_obj);
      if (ret < 0)
        {
          errcode = errno;
        }

      sem_post(&(fbdev_obj->wait_sem));
    }

  gerr("Failed to write buffer contents to display device: %d\n", errcode);

  return NULL;
}

/****************************************************************************
 * Name: fbdev_wait
 ****************************************************************************/

static void fbdev_wait(FAR lv_disp_drv_t *disp_drv)
{
  FAR struct fbdev_obj_s *fbdev_obj = disp_drv->user_data;

  sem_wait(&(fbdev_obj->wait_sem));

  /* Tell the flushing is ready */

  lv_disp_flush_ready(disp_drv);
}

/****************************************************************************
 * Name: fbdev_flush
 ****************************************************************************/

static void fbdev_flush(FAR lv_disp_drv_t *disp_drv,
                        FAR const lv_area_t *area_p,
                        FAR lv_color_t *color_p)
{
  FAR struct fbdev_obj_s *fbdev_obj = disp_drv->user_data;

  fbdev_obj->area = *area_p;
  fbdev_obj->color_p = color_p;

  sem_post(&(fbdev_obj->flush_sem));
}

/****************************************************************************
 * Name: fbdev_init
 ****************************************************************************/

static FAR lv_disp_t *fbdev_init(FAR struct fbdev_obj_s *state,
                                 int hor_res, int ver_res, int line_buf)
{
  FAR lv_color_t *buf1 = NULL;
  FAR lv_color_t *buf2 = NULL;
  const size_t buf_size = hor_res * line_buf * sizeof(lv_color_t);
  FAR struct fbdev_obj_s *fbdev_obj = malloc(sizeof(struct fbdev_obj_s));

  if (fbdev_obj == NULL)
    {
      LV_LOG_ERROR("fbdev_obj_s malloc failed");
      return NULL;
    }

  buf1 = malloc(buf_size);
  if (buf1 == NULL)
    {
      LV_LOG_ERROR("display buf1 malloc failed");
      free(fbdev_obj);
      return NULL;
    }

#ifdef CONFIG_LV_USE_DOUBLE_BUFFER
  buf2 = malloc(buf_size);
  if (buf2 == NULL)
    {
      LV_LOG_ERROR("display buf2 malloc failed");
      free(fbdev_obj);
      free(buf1);
      return NULL;
    }
#endif

  LV_LOG_INFO("display buffer malloc success, buf size = %lu", buf_size);

  *fbdev_obj = *state;

  lv_disp_draw_buf_init(&(fbdev_obj->disp_draw_buf), buf1, buf2,
                        hor_res * line_buf);

  lv_disp_drv_init(&(fbdev_obj->disp_drv));
  fbdev_obj->disp_drv.flush_cb = fbdev_flush;
  fbdev_obj->disp_drv.draw_buf = &(fbdev_obj->disp_draw_buf);
  fbdev_obj->disp_drv.hor_res = hor_res;
  fbdev_obj->disp_drv.ver_res = ver_res;
#if ( LV_USE_USER_DATA != 0 )
  fbdev_obj->disp_drv.user_data = fbdev_obj;
#else
#error LV_USE_USER_DATA must be enabled
#endif
  fbdev_obj->disp_drv.wait_cb = fbdev_wait;

  /* Initialize the mutexes for buffer flushing synchronization */

  sem_init(&(fbdev_obj->flush_sem), 0, 0);
  sem_init(&(fbdev_obj->wait_sem), 0, 0);

  /* Initialize the buffer flushing thread */

  pthread_create(&(fbdev_obj->write_thread), NULL,
                 fbdev_update_thread, fbdev_obj);

  return lv_disp_drv_register(&(fbdev_obj->disp_drv));
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
 *   dev_path - Framebuffer device path, set to NULL to use the default path
 *   line_buf - Number of line buffers,
 *              set to 0 to use the default line buffer
 *
 * Returned Value:
 *   lv_disp object address on success; NULL on failure.
 *
 ****************************************************************************/

FAR lv_disp_t *lv_fbdev_interface_init(FAR const char *dev_path,
                                       int line_buf)
{
  FAR const char *device_path = dev_path;
  int line_buffer = line_buf;
  struct fbdev_obj_s state;
  int ret;
  FAR lv_disp_t *disp;

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
      state.flush_cb = fbdev_flush_direct;
    }
  else
    {
      LV_LOG_WARN("fbdev bpp = %d, LV_COLOR_DEPTH = %d, "
                  "color depth does not match.",
                  state.pinfo.bpp, LV_COLOR_DEPTH);
      LV_LOG_WARN("Use software color conversion "
                  "which makes LVGL much slower.");
      state.flush_cb = fbdev_flush_convert;
    }

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

#ifdef CONFIG_LV_USE_FULL_SCREEN_BUFFER
  line_buffer = state.vinfo.yres;
#else
  if (line_buffer <= 0)
    {
      line_buffer = CONFIG_LV_DEFAULT_LINE_BUFFER;
    }
#endif

  disp = fbdev_init(&state, state.vinfo.xres, state.vinfo.yres,
                    line_buffer);
  if (disp == NULL)
    {
      close(state.fd);
    }

  return disp;
}
