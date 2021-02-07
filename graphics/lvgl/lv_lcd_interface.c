/****************************************************************************
 * graphics/lvgl/lv_lcd_interface.c
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
#include <nuttx/lcd/lcd_dev.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include "lv_lcd_interface.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Private Type Declarations
 ****************************************************************************/

struct lcd_obj_s
{
  int fd;
  lv_disp_buf_t disp_buf;
};

/****************************************************************************
 * Public Data
 ****************************************************************************/

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: lcd_flush
 ****************************************************************************/

static void lcd_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area_p,
                      lv_color_t *color_p)
{
  struct lcd_obj_s *lcd_obj = (struct lcd_obj_s *)disp_drv->user_data;

  struct lcddev_area_s lcd_area;

  lcd_area.row_start = area_p->y1;
  lcd_area.row_end = area_p->y2;
  lcd_area.col_start = area_p->x1;
  lcd_area.col_end = area_p->x2;
  lcd_area.data = (uint8_t *)color_p;

  ioctl(lcd_obj->fd, LCDDEVIO_PUTAREA, &lcd_area);

  /* Tell the flushing is ready */

  lv_disp_flush_ready(disp_drv);
}

/****************************************************************************
 * Name: lcd_init
 ****************************************************************************/

static lv_disp_t *lcd_init(int fd, int line_buf)
{
  struct lcd_obj_s *lcd_obj =
    (struct lcd_obj_s *)malloc(sizeof(struct lcd_obj_s));

  if (lcd_obj == NULL)
    {
      LV_LOG_ERROR("lcd_obj_s malloc failed");
      return NULL;
    }

  const size_t buf_size = LV_HOR_RES_MAX * line_buf * sizeof(lv_color_t);

  lv_color_t *buf = (lv_color_t *)malloc(buf_size);

  if (buf == NULL)
    {
      LV_LOG_ERROR("display buffer malloc failed");
      free(lcd_obj);
      return NULL;
    }

  LV_LOG_INFO("display buffer malloc success, size = %ld", buf_size);

  lcd_obj->fd = fd;

  lv_disp_buf_init(&(lcd_obj->disp_buf), buf, NULL,
                       LV_HOR_RES_MAX * line_buf);

  lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.flush_cb = lcd_flush;
  disp_drv.buffer = &(lcd_obj->disp_buf);
#if ( LV_USE_USER_DATA != 0 )
  disp_drv.user_data = lcd_obj;
#else
#error LV_USE_USER_DATA must be enabled
#endif
  return lv_disp_drv_register(&disp_drv);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: lv_lcd_interface_init
 *
 * Description:
 *   Lcd interface initialization.
 *
 * Input Parameters:
 *   dev_path - lcd device path, set to NULL to use the default path
 *   line_buf - Number of line buffers,
 *              set to 0 to use the default line buffer
 *
 * Returned Value:
 *   lv_disp object address on success; NULL on failure.
 *
 ****************************************************************************/

lv_disp_t *lv_lcd_interface_init(const char *dev_path, int line_buf)
{
  const char *device_path = dev_path;
  int line_buffer = line_buf;

  if (device_path == NULL)
    {
      device_path = CONFIG_LV_LCD_INTERFACE_DEFAULT_DEVICEPATH;
    }

  if (line_buffer <= 0)
    {
      line_buffer = CONFIG_LV_LCD_INTERFACE_DEFAULT_LINE_BUFF;
    }

  LV_LOG_INFO("lcddev opening %s", device_path);
  int fd = open(device_path, 0);
  if (fd < 0)
    {
      int errcode = errno;
      LV_LOG_ERROR("lcddev open failed: %d", errcode);
      return NULL;
    }

  LV_LOG_INFO("lcddev %s open success", device_path);

  return lcd_init(fd, line_buffer);
}
