/****************************************************************************
 * graphics/lvgl/lv_touchpad_interface.c
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
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <debug.h>
#include <nuttx/input/touchscreen.h>
#include "lv_touchpad_interface.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define TOUCHPAD_DEVICEPATH      CONFIG_LV_TOUCHPAD_INTERFACE_DEVICEPATH

/****************************************************************************
 * Private Type Declarations
 ****************************************************************************/

struct touchpad_drv_s
{
  int fd;
  lv_coord_t last_x;
  lv_coord_t last_y;
  lv_indev_state_t last_state;
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: touchpad_read
 ****************************************************************************/

static bool touchpad_read(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
  struct touchpad_drv_s *touchpad_drv =
    (struct touchpad_drv_s *)drv->user_data;

  /* Read one sample */

  struct touch_sample_s sample;

  int nbytes =
    read(touchpad_drv->fd, &sample, sizeof(struct touch_sample_s));

  /* Handle unexpected return values */

  if (nbytes < 0 || nbytes != sizeof(struct touch_sample_s))
    {
      goto update_points;
    }

  uint8_t touch_flags = sample.point[0].flags;

  if (touch_flags & TOUCH_DOWN || touch_flags & TOUCH_MOVE)
    {
      touchpad_drv->last_x = sample.point[0].x;
      touchpad_drv->last_y = sample.point[0].y;
      touchpad_drv->last_state = LV_INDEV_STATE_PR;
    }
  else if (touch_flags & TOUCH_UP)
    {
      touchpad_drv->last_state = LV_INDEV_STATE_REL;
    }

update_points:

  /* Update touchpad data */

  data->point.x = touchpad_drv->last_x;
  data->point.y = touchpad_drv->last_y;
  data->state = touchpad_drv->last_state;

  return false;
}

/****************************************************************************
 * Name: touchpad_init
 ****************************************************************************/

static lv_indev_t *touchpad_init(int fd)
{
  struct touchpad_drv_s *touchpad_drv =
    (struct touchpad_drv_s *)lv_mem_alloc(sizeof(struct touchpad_drv_s));

  if (touchpad_drv == NULL)
    {
      LV_LOG_ERROR("touchpad_drv malloc failed");
      return NULL;
    }

  touchpad_drv->fd = fd;
  touchpad_drv->last_x = 0;
  touchpad_drv->last_y = 0;
  touchpad_drv->last_state = LV_INDEV_STATE_REL;

  lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = touchpad_read;
#if ( LV_USE_USER_DATA != 0 )
  indev_drv.user_data = touchpad_drv;
#else
#error LV_USE_USER_DATA must be enabled
#endif
  return lv_indev_drv_register(&indev_drv);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: lv_touchpad_interface_init
 ****************************************************************************/

lv_indev_t *lv_touchpad_interface_init(void)
{
  LV_LOG_INFO("touchpad opening %s", TOUCHPAD_DEVICEPATH);
  int fd = open(TOUCHPAD_DEVICEPATH, O_RDONLY | O_NONBLOCK);
  if (fd < 0)
    {
      int errcode = errno;
      LV_LOG_ERROR("touchpad failed to open %s ! errcode: %d",
                   TOUCHPAD_DEVICEPATH, errcode);
      return NULL;
    }

  LV_LOG_INFO("touchpad %s open success", TOUCHPAD_DEVICEPATH);

  return touchpad_init(fd);
}
