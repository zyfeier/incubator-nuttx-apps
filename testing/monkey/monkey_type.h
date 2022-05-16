/****************************************************************************
 * apps/testing/monkey/monkey_type.h
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

#ifndef __MONKEY_TYPE_H__
#define __MONKEY_TYPE_H__

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdint.h>
#include <stdbool.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MONKEY_FLAG_UINPUT_TYPE     (0x10)
#define MONKEY_IS_UINPUT_TYPE(type) (!!((type) & MONKEY_FLAG_UINPUT_TYPE))
#define MONKEY_GET_DEV_TYPE(type)   (type & ~MONKEY_FLAG_UINPUT_TYPE)

/****************************************************************************
 * Public Types
 ****************************************************************************/

struct monkey_port_dev_s;
struct monkey_recorder_s;

enum monkey_screen_type_e
{
  MONKEY_SCREEN_TYPE_RECT,
  MONKEY_SCREEN_TYPE_ROUND
};

enum monkey_mode_type_e
{
  MONKEY_MODE_RANDOM,
  MONKEY_MODE_ORDER,
};

enum monkey_dev_type_e
{
  MONKEY_DEV_TYPE_UNKNOW,
  MONKEY_DEV_TYPE_TOUCH = 1,
  MONKEY_DEV_TYPE_BUTTON = 2,
  MONKEY_DEV_TYPE_UTOUCH = MONKEY_DEV_TYPE_TOUCH | MONKEY_FLAG_UINPUT_TYPE,
  MONKEY_DEV_TYPE_UBUTTON = MONKEY_DEV_TYPE_BUTTON | MONKEY_FLAG_UINPUT_TYPE,
};

union monkey_dev_state_u
{
  struct
  {
    int x;
    int y;
    int is_pressed;
  } touch;

  struct
  {
    uint32_t value;
  } button;
};

struct monkey_screen_s
{
  int hor_res;
  int ver_res;
  enum monkey_screen_type_e type;
};

struct monkey_config_s
{
  struct monkey_screen_s screen;
  struct
  {
    uint32_t min;
    uint32_t max;
  } period;
};

struct monkey_s
{
  struct monkey_config_s config;
  enum monkey_mode_type_e mode;
  FAR struct monkey_port_dev_s *dev;
  FAR struct monkey_recorder_s *recorder;
  struct
  {
      bool not_first;
      union monkey_dev_state_u last_state;
      uint32_t last_time_stamp;
  } playback_ctx;
};

#endif /* __MONKEY_TYPE_H__ */
