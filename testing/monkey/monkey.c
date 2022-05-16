/****************************************************************************
 * apps/testing/monkey/monkey.c
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
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "monkey.h"
#include "monkey_recorder.h"
#include "monkey_assert.h"
#include "monkey_log.h"
#include "monkey_port.h"
#include "monkey_utils.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: monkey_create
 ****************************************************************************/

FAR struct monkey_s *monkey_create(FAR const char *dev_path,
                                   enum monkey_dev_type_e type)
{
  FAR struct monkey_s *monkey = malloc(sizeof(struct monkey_s));
  MONKEY_ASSERT_NULL(monkey);
  memset(monkey, 0, sizeof(struct monkey_s));

  monkey->dev = monkey_port_create(dev_path, type);
  if (!monkey->dev)
    {
      free(monkey);
      return NULL;
    }

  MONKEY_LOG_NOTICE("OK");

  return monkey;
}

/****************************************************************************
 * Name: monkey_delete
 ****************************************************************************/

void monkey_delete(FAR struct monkey_s *monkey)
{
  MONKEY_ASSERT_NULL(monkey);
  monkey_port_delete(monkey->dev);

  if (monkey->recorder)
    {
      monkey_recorder_delete(monkey->recorder);
      monkey->recorder = NULL;
    }

  free(monkey);
  MONKEY_LOG_NOTICE("OK");
}

/****************************************************************************
 * Name: monkey_config_default_init
 ****************************************************************************/

void monkey_config_default_init(FAR struct monkey_config_s *config)
{
  MONKEY_ASSERT_NULL(config);
  memset(config, 0, sizeof(struct monkey_config_s));
  config->screen.type = MONKEY_SCREEN_TYPE_RECT;
  config->screen.hor_res = CONFIG_TESTING_MONKEY_SCREEN_HOR_RES;
  config->screen.ver_res = CONFIG_TESTING_MONKEY_SCREEN_VER_RES;
  config->period.min = CONFIG_TESTING_MONKEY_PERIOD_MIN_DEFAULT;
  config->period.max = CONFIG_TESTING_MONKEY_PERIOD_MAX_DEFAULT;
}

/****************************************************************************
 * Name: monkey_set_config
 ****************************************************************************/

void monkey_set_config(FAR struct monkey_s *monkey,
                       FAR const struct monkey_config_s *config)
{
  MONKEY_ASSERT_NULL(monkey);
  MONKEY_ASSERT_NULL(config);
  monkey->config = *config;
}

/****************************************************************************
 * Name: monkey_set_mode
 ****************************************************************************/

void monkey_set_mode(FAR struct monkey_s *monkey,
                     enum monkey_mode_type_e mode)
{
  MONKEY_ASSERT_NULL(monkey);
  MONKEY_LOG_NOTICE("%s", mode == MONKEY_MODE_RANDOM ? "random" : "order");
  monkey->mode = mode;
}

/****************************************************************************
 * Name: monkey_set_period
 ****************************************************************************/

void monkey_set_period(FAR struct monkey_s *monkey, uint32_t period)
{
  MONKEY_ASSERT_NULL(monkey);
  monkey->config.period.min = period;
  monkey->config.period.max = period;
}

/****************************************************************************
 * Name: monkey_set_recorder_path
 ****************************************************************************/

bool monkey_set_recorder_path(FAR struct monkey_s *monkey,
                              FAR const char *path)
{
  enum monkey_dev_type_e type;

  MONKEY_ASSERT_NULL(monkey);

  type = monkey_port_get_type(monkey->dev);

  if (MONKEY_IS_UINPUT_TYPE(type))
    {
      monkey->recorder = monkey_recorder_create(path,
                         type, MONKEY_RECORDER_MODE_PLAYBACK);
    }
  else
    {
      monkey->recorder = monkey_recorder_create(path,
                         type, MONKEY_RECORDER_MODE_RECORD);
    }

  return (monkey->recorder != NULL);
}
