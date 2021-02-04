/****************************************************************************
 * graphics/lvgl/lv_fbdev_interface.c
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
#include <errno.h>
#include "lv_fbdev_interface.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define FB_DEVICEPATH      CONFIG_LV_FBDEV_INTERFACE_DEVICEPATH
#define FB_BUFFER_SIZE     (CONFIG_LV_HOR_RES * \
                             CONFIG_LV_FBDEV_INTERFACE_BUFF_SIZE)

/****************************************************************************
 * Private Type Declarations
 ****************************************************************************/

struct fb_drv_s
{
  int fd;
  struct fb_videoinfo_s vinfo;
  struct fb_planeinfo_s pinfo;
  void *fbmem;
};

/****************************************************************************
 * Public Data
 ****************************************************************************/

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: fbdev_flush
 ****************************************************************************/

static void fbdev_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area_p,
                        lv_color_t *color_p)
{
  struct fb_drv_s *state = (struct fb_drv_s *)disp_drv->user_data;

  int32_t x1 = area_p->x1;
  int32_t y1 = area_p->y1;
  int32_t x2 = area_p->x2;
  int32_t y2 = area_p->y2;
  int32_t act_x1;
  int32_t act_y1;
  int32_t act_x2;
  int32_t act_y2;

  if (state->fbmem == NULL)
    {
      return;
    }

  /* Return if the area is out the screen */

  if (x2 < 0)
    {
      return;
    }

  if (y2 < 0)
    {
      return;
    }

  if (x1 > state->vinfo.xres - 1)
    {
      return;
    }

  if (y1 > state->vinfo.yres - 1)
    {
      return;
    }

  /* Truncate the area to the screen */

  act_x1 = x1 < 0 ? 0 : x1;
  act_y1 = y1 < 0 ? 0 : y1;
  act_x2 = x2 > state->vinfo.xres - 1 ? state->vinfo.xres - 1 : x2;
  act_y2 = y2 > state->vinfo.yres - 1 ? state->vinfo.yres - 1 : y2;

  const uint32_t color_step = x2 - act_x1 + 1;
  const fb_coord_t xres = state->vinfo.xres;
  const uint8_t bpp = state->pinfo.bpp;
  uint32_t y;

  if (bpp == 8)
    {
      uint8_t *fbp8 = (uint8_t *)state->fbmem;
      const uint32_t hor_len = (act_x2 - act_x1 + 1) * sizeof(uint8_t);

      for (y = act_y1; y <= act_y2; y++)
        {
          uint8_t *fb_pos = fbp8 + (y * xres) + act_x1;
          memcpy(fb_pos, color_p, hor_len);
          color_p += color_step;
        }
    }
  else if (bpp == 16)
    {
      uint16_t *fbp16 = (uint16_t *)state->fbmem;
      const uint32_t hor_len = (act_x2 - act_x1 + 1) * sizeof(uint16_t);

      for (y = act_y1; y <= act_y2; y++)
        {
          uint16_t *fb_pos = fbp16 + (y * xres) + act_x1;
          memcpy(fb_pos, color_p, hor_len);
          color_p += color_step;
        }
    }
  else if (bpp == 24 || bpp == 32)
    {
      uint32_t *fbp32 = (uint32_t *)state->fbmem;
      const uint32_t hor_len = (act_x2 - act_x1 + 1) * sizeof(uint32_t);

      for (y = act_y1; y <= act_y2; y++)
        {
          uint32_t *fb_pos = fbp32 + (y * xres) + act_x1;
          memcpy(fb_pos, color_p, hor_len);
          color_p += color_step;
        }
    }

#ifdef CONFIG_FB_UPDATE
  struct fb_area_s fb_area;
  fb_area.x = act_x1;
  fb_area.y = act_y1;
  fb_area.w = act_x2 - act_x1 + 1;
  fb_area.h = act_y2 - cat_y1 + 1;
  ioctl(state->fd, FBIO_UPDATE, (unsigned long)((uintptr_t)&fb_area));
#endif

  /* Tell the flushing is ready */

  lv_disp_flush_ready(disp_drv);
}

/****************************************************************************
 * Name: fbdev_init
 ****************************************************************************/

static lv_disp_t *fbdev_init(struct fb_drv_s *state)
{
  struct fb_drv_s *fb_drv =
    (struct fb_drv_s *)lv_mem_alloc(sizeof(struct fb_drv_s));

  if (fb_drv == NULL)
    {
      LV_LOG_ERROR("fb_drv malloc failed!");
      return NULL;
    }

  memcpy(fb_drv, state, sizeof(struct fb_drv_s));

  static lv_disp_buf_t disp_buf;
  static lv_color_t buf[FB_BUFFER_SIZE];
  lv_disp_buf_init(&disp_buf, buf, NULL, FB_BUFFER_SIZE);

  lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.flush_cb = fbdev_flush;
  disp_drv.buffer = &disp_buf;
#if ( LV_USE_USER_DATA != 0 )
  disp_drv.user_data = fb_drv;
#else
#error LV_USE_USER_DATA must be enabled
#endif
  return lv_disp_drv_register(&disp_drv);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: lv_fbdev_interface_init
 ****************************************************************************/

lv_disp_t *lv_fbdev_interface_init(void)
{
  LV_LOG_INFO("fbdev opening %s", FB_DEVICEPATH);

  struct fb_drv_s state;

  state.fd = open(FB_DEVICEPATH, O_RDWR);
  if (state.fd < 0)
    {
      int errcode = errno;
      LV_LOG_ERROR("fbdev open failed! errcode: %d", errcode);
      return NULL;
    }

  /* Get the characteristics of the framebuffer */

  int ret = ioctl(state.fd, FBIOGET_VIDEOINFO,
                    (unsigned long)((uintptr_t)&state.vinfo));
  if (ret < 0)
    {
      int errcode = errno;
      LV_LOG_ERROR("ioctl(FBIOGET_VIDEOINFO) failed: %d", errcode);
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
      int errcode = errno;
      LV_LOG_ERROR("ERROR: ioctl(FBIOGET_PLANEINFO) failed: %d",
              errcode);
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
      int errcode = errno;
      LV_LOG_ERROR("ioctl(FBIOGET_PLANEINFO) failed: %d", errcode);
      close(state.fd);
      return NULL;
    }

  LV_LOG_INFO("Mapped FB: %p", state.fbmem);

  return fbdev_init(&state);
}
