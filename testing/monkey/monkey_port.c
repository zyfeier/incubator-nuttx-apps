/****************************************************************************
 * apps/testing/monkey/monkey_port.c
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

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <nuttx/input/buttons.h>
#include <nuttx/input/touchscreen.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "monkey_assert.h"
#include "monkey_log.h"
#include "monkey_port.h"
#include "monkey_utils.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: utouch_write
 ****************************************************************************/

static void utouch_write(int fd, int x, int y, int touch_down)
{
  struct touch_sample_s sample;

  if (touch_down)
    {
      sample.point[0].x        = x;
      sample.point[0].y        = y;
      sample.point[0].pressure = 42;
      sample.point[0].flags    = TOUCH_DOWN | TOUCH_ID_VALID |
                                 TOUCH_POS_VALID | TOUCH_PRESSURE_VALID;
    }
  else
    {
      sample.point[0].flags = TOUCH_UP | TOUCH_ID_VALID;
    }

  sample.npoints    = 1;
  sample.point[0].h = 1;
  sample.point[0].w = 1;

  write(fd, &sample, sizeof(struct touch_sample_s));
  MONKEY_LOG_INFO("%s at x = %d, y = %d",
                  touch_down ? "PRESS  " : "RELEASE", x, y);
}

/****************************************************************************
 * Name: ubutton_write
 ****************************************************************************/

static void ubutton_write(int fd, uint32_t btn_bits)
{
  btn_buttonset_t buttonset = btn_bits;
  write(fd, &buttonset, sizeof(buttonset));
  MONKEY_LOG_INFO("btn = 0x%08X", btn_bits);
}

/****************************************************************************
 * Name: touch_read
 ****************************************************************************/

static bool touch_read(int fd, FAR int *x, FAR int *y, FAR int *touch_down)
{
  bool retval = false;
  struct touch_sample_s sample;

  int nbytes = read(fd, &sample, sizeof(sample));

  if (nbytes == sizeof(sample))
    {
      retval = true;
      uint8_t touch_flags = sample.point[0].flags;

      if (touch_flags & TOUCH_DOWN || touch_flags & TOUCH_MOVE)
        {
          *x = sample.point[0].x;
          *y = sample.point[0].y;
          *touch_down = true;
        }
      else if (touch_flags & TOUCH_UP)
        {
          *x = 0;
          *y = 0;
          *touch_down = false;
        }
    }

  return retval;
}

/****************************************************************************
 * Name: button_read
 ****************************************************************************/

static bool button_read(int fd, FAR uint32_t *value)
{
  btn_buttonset_t buttonset;

  int ret = read(fd, &buttonset, sizeof(buttonset));
  if (ret < 0)
    {
      return false;
    }

  *value = buttonset;
  return true;
}

/****************************************************************************
 * Name: monkey_port_create
 ****************************************************************************/

FAR struct monkey_port_dev_s *monkey_port_create(FAR const char *dev_path,
                                              enum monkey_dev_type_e type)
{
  FAR struct monkey_port_dev_s *dev;
  int oflag;
  int fd;

  MONKEY_ASSERT_NULL(dev_path);

  dev = malloc(sizeof(struct monkey_port_dev_s));
  MONKEY_ASSERT_NULL(dev);
  memset(dev, 0, sizeof(struct monkey_port_dev_s));

  if (MONKEY_IS_UINPUT_TYPE(type))
    {
      oflag = O_RDWR | O_NONBLOCK;
    }
  else
    {
      oflag = O_RDONLY | O_NONBLOCK;
    }

  fd = open(dev_path, oflag);
  if (fd < 0)
    {
      MONKEY_LOG_ERROR("open %s failed: %d", dev_path, errno);
      goto failed;
    }

  dev->type = type;
  dev->fd = fd;

  MONKEY_LOG_NOTICE("open %s success, fd = %d, type: %s",
                    dev_path, fd, monkey_dev_type2name(type));

  return dev;

failed:
  if (fd > 0)
    {
      close(fd);
    }

  if (dev)
    {
      free(dev);
    }

  return NULL;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: monkey_port_delete
 ****************************************************************************/

void monkey_port_delete(FAR struct monkey_port_dev_s *dev)
{
  MONKEY_ASSERT_NULL(dev);

  if (dev->fd > 0)
    {
      /* Reset input state */

      union monkey_dev_state_u state;
      memset(&state, 0, sizeof(state));
      monkey_port_set_state(dev, &state);

      MONKEY_LOG_NOTICE("close fd: %d", dev->fd);
      close(dev->fd);
      dev->fd = -1;
    }

  free(dev);
}

/****************************************************************************
 * Name: monkey_port_set_state
 ****************************************************************************/

void monkey_port_set_state(FAR struct monkey_port_dev_s *dev,
                           FAR const union monkey_dev_state_u *state)
{
  MONKEY_ASSERT_NULL(dev);

  switch (dev->type)
    {
    case MONKEY_DEV_TYPE_UTOUCH:
      utouch_write(dev->fd,
                   state->touch.x,
                  state->touch.y,
                  state->touch.is_pressed);
      break;

    case MONKEY_DEV_TYPE_UBUTTON:
      ubutton_write(dev->fd, state->button.value);
      break;

    default:
      break;
    }
}

/****************************************************************************
 * Name: monkey_port_get_state
 ****************************************************************************/

bool monkey_port_get_state(FAR struct monkey_port_dev_s *dev,
                           FAR union monkey_dev_state_u *state)
{
  bool retval;
  MONKEY_ASSERT_NULL(dev);

  retval = false;

  switch (dev->type)
    {
    case MONKEY_DEV_TYPE_TOUCH:
      retval = touch_read(dev->fd,
                          &state->touch.x,
                          &state->touch.y,
                          &state->touch.is_pressed);
      break;

    case MONKEY_DEV_TYPE_BUTTON:
      retval = button_read(dev->fd, &state->button.value);
      if (dev->last_state.button.value == state->button.value)
        {
          /* Do not report if the status has not changed */

          retval = false;
        }
      break;

    default:
      break;
    }

  dev->last_state = *state;

  return retval;
}

/****************************************************************************
 * Name: monkey_port_get_state
 ****************************************************************************/

enum monkey_dev_type_e monkey_port_get_type(
                       FAR struct monkey_port_dev_s *dev)
{
  MONKEY_ASSERT_NULL(dev);
  return dev->type;
}
