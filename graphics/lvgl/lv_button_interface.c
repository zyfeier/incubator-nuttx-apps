/****************************************************************************
 * graphics/lvgl/lv_button_interface.c
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

#include <sys/types.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <debug.h>
#include <nuttx/input/buttons.h>
#include "lv_button_interface.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BUTTON_DEVICEPATH           CONFIG_LV_BUTTON_INTERFACE_BUTTON_DEVICEPATH

/****************************************************************************
 * Private Type Declarations
 ****************************************************************************/

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int button_get_pressed_id(void);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static int fd_button = -1;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: button_get_pressed_id
 ****************************************************************************/

static int button_get_pressed_id(void)
{
  int btn_act = -1;
  btn_buttonset_t buttonset;
  const int buttonset_bits = sizeof(btn_buttonset_t) * 8;

  if (fd_button < 0)
    {
      return -1;
    }

  int ret = read(fd_button, &buttonset, sizeof(btn_buttonset_t));
  if (ret < 0)
    {
      return -1;
    }

  for (int bit = 0; bit < buttonset_bits; bit++)
    {
      btn_buttonset_t mask = 1 << bit;

      if (buttonset & mask)
        {
          btn_act = bit;
          break;
        }
    }

  return btn_act;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: lv_button_interface_read
 ****************************************************************************/

bool lv_button_interface_read(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
  static uint8_t last_btn = 0;

  /* Get the pressed button's ID */

  int btn_act = button_get_pressed_id();

  if (btn_act >= 0)
    {
      data->state = LV_INDEV_STATE_PR;
      last_btn = btn_act;
    }
  else
    {
      data->state = LV_INDEV_STATE_REL;
    }

  /* Save the last pressed button's ID */

  data->btn_id = last_btn;

  /* Return `false` because we are not buffering and no more data to read */

  return false;
}

/****************************************************************************
 * Name: lv_button_interface_init
 ****************************************************************************/

void lv_button_interface_init(void)
{
  int ret;
  int fd;

  printf("button_daemon: Opening %s\n", BUTTON_DEVICEPATH);
  fd = open(BUTTON_DEVICEPATH, O_RDONLY | O_NONBLOCK);
  if (fd < 0)
    {
      int errcode = errno;
      printf("button: ERROR: Failed to open %s: %d\n",
             BUTTON_DEVICEPATH, errcode);
      return;
    }

  fd_button = fd;

  /* Get the set of BUTTONs supported */

  btn_buttonset_t supported;

  ret = ioctl(fd, BTNIOC_SUPPORTED,
              (unsigned long)((uintptr_t)&supported));
  if (ret < 0)
    {
      int errcode = errno;
      printf("button_daemon: ERROR: ioctl(BTNIOC_SUPPORTED) failed: %d\n",
             errcode);
      return;
    }

  printf("button: Supported BUTTONs 0x%02x\n", (unsigned int)supported);
}
