/****************************************************************************
 * apps/testing/monkey/monkey_proc.c
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
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "monkey.h"
#include "monkey_assert.h"
#include "monkey_log.h"
#include "monkey_port.h"
#include "monkey_recorder.h"
#include "monkey_utils.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: monkey_get_random_press
 ****************************************************************************/

static int monkey_get_random_press(int probability)
{
  return monkey_random(0, 100) < probability ? 0 : 1;
}

/****************************************************************************
 * Name: monkey_update_uinput
 ****************************************************************************/

static bool monkey_update_uinput(FAR struct monkey_s *monkey)
{
  enum monkey_dev_type_e type;
  type = MONKEY_GET_DEV_TYPE(monkey_port_get_type(monkey->dev));

  if (monkey->mode == MONKEY_MODE_RANDOM)
    {
      union monkey_dev_state_u state;
      if (type == MONKEY_DEV_TYPE_TOUCH)
        {
          int x_max = monkey->config.screen.hor_res - 1;
          int y_max = monkey->config.screen.ver_res - 1;
          state.touch.x = monkey_random(0, x_max);
          state.touch.y = monkey_random(0, y_max);
          state.touch.is_pressed = monkey_get_random_press(50);
          monkey_port_set_state(monkey->dev, &state);
        }
      else
        {
          const int btn_num = CONFIG_TESTING_MONKEY_BUTTON_NUM;
          int btn_bits;
          if (!btn_num)
            {
              MONKEY_LOG_ERROR("Button test number is 0");
              return false;
            }

          btn_bits = monkey_random(0, btn_num - 1);

          /* press button */

          state.button.value = 1 << btn_bits;
          monkey_port_set_state(monkey->dev, &state);

          usleep(CONFIG_TESTING_MONKEY_BUTTON_CLICK_TIME * 1000);

          /* release button */

          state.button.value = 0;
          monkey_port_set_state(monkey->dev, &state);
        }

      return true;
    }

  if (monkey->mode == MONKEY_MODE_ORDER)
    {
      uint32_t next_time_stamp;
      uint32_t tick_elaps;
      FAR uint32_t *last_time_stamp_p;
      union monkey_dev_state_u next_state;
      FAR union monkey_dev_state_u *last_state_p;
      enum monkey_recorder_res_e res;

      MONKEY_ASSERT_NULL(monkey->recorder);

      last_time_stamp_p = &monkey->playback_ctx.last_time_stamp;
      last_state_p = &monkey->playback_ctx.last_state;

      if (!monkey->playback_ctx.not_first)
        {
          res = monkey_recorder_read(monkey->recorder,
                                     last_state_p,
                                     last_time_stamp_p);

          if (res != MONKEY_RECORDER_RES_OK)
            {
              MONKEY_LOG_ERROR("read first line error: %d", res);
              return false;
            }

          res = monkey_recorder_read(monkey->recorder,
                                     &next_state,
                                     &next_time_stamp);

          if (res != MONKEY_RECORDER_RES_OK)
            {
              MONKEY_LOG_ERROR("read second line error: %d", res);
              return false;
            }

          monkey_port_set_state(monkey->dev, last_state_p);

          tick_elaps = monkey_tick_elaps(next_time_stamp,
                                         *last_time_stamp_p);
          monkey_set_period(monkey, tick_elaps);

          monkey->playback_ctx.not_first = true;
          *last_time_stamp_p = next_time_stamp;
          *last_state_p = next_state;
          return true;
        }

      res = monkey_recorder_read(monkey->recorder,
                                 &next_state,
                                 &next_time_stamp);
      if (res != MONKEY_RECORDER_RES_OK)
        {
          if (res == MONKEY_RECORDER_RES_END_OF_FILE)
            {
              MONKEY_LOG_WARN("end of file, reset recorder...");
              monkey_recorder_reset(monkey->recorder);

              monkey_port_set_state(monkey->dev, last_state_p);
              monkey_set_period(monkey, 100);

              monkey->playback_ctx.not_first = false;
              return true;
            }

          MONKEY_LOG_ERROR("read error: %d", res);
          return false;
        }

      monkey_port_set_state(monkey->dev, last_state_p);

      tick_elaps = monkey_tick_elaps(next_time_stamp, *last_time_stamp_p);
      monkey_set_period(monkey, tick_elaps);

      *last_time_stamp_p = next_time_stamp;
      *last_state_p = next_state;
    }

  return true;
}

/****************************************************************************
 * Name: monkey_update_input
 ****************************************************************************/

static void monkey_update_input(FAR struct monkey_s *monkey)
{
  union monkey_dev_state_u state;
  enum monkey_dev_type_e type;

  MONKEY_ASSERT_NULL(monkey);

  type = monkey_port_get_type(monkey->dev);

  if (monkey_port_get_state(monkey->dev, &state))
    {
      if (type == MONKEY_DEV_TYPE_TOUCH)
        {
          MONKEY_LOG_INFO("touch %s at x = %d, y = %d",
                          state.touch.is_pressed ? "PRESS  " : "RELEASE",
                          state.touch.x, state.touch.y);
        }
      else if (type == MONKEY_DEV_TYPE_BUTTON)
        {
          MONKEY_LOG_INFO("btn = 0x%08X", state.button.value);
        }

      if (monkey->recorder)
        {
          monkey_recorder_write(monkey->recorder, &state);
        }
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: monkey_update
 ****************************************************************************/

int monkey_update(FAR struct monkey_s *monkey)
{
  enum monkey_dev_type_e type;
  int next_period;
  MONKEY_ASSERT_NULL(monkey);

  srand(monkey_tick_get());

  type = monkey_port_get_type(monkey->dev);

  if (MONKEY_IS_UINPUT_TYPE(type))
    {
      if (!monkey_update_uinput(monkey))
        {
          return -1;
        }
    }
  else
    {
      monkey_update_input(monkey);
    }

  next_period = monkey_random(monkey->config.period.min,
                              monkey->config.period.max);

  return next_period;
}
