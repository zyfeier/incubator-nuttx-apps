/****************************************************************************
 * apps/testing/monkey/monkey_main.c
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
#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "monkey.h"
#include "monkey_utils.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MONKEY_PREFIX "monkey"

/****************************************************************************
 * Private Type Declarations
 ****************************************************************************/

struct monkey_param_s
{
  FAR const char *dev_path;
  FAR const char *dev_type;
  FAR const char *file_path;
  int hor_res;
  int ver_res;
  int period_min;
  int period_max;
};

enum monkey_wait_res_e
{
  MONKEY_WAIT_RES_AGAIN,
  MONKEY_WAIT_RES_PAUSE,
  MONKEY_WAIT_RES_STOP,
  MONKEY_WAIT_RES_ERROR,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: show_usage
 ****************************************************************************/

static void show_usage(FAR const char *progname, int exitcode)
{
  printf("\nUsage: %s"
         " -d <string> -t <string> -f <string> -p <string> -s <string>\n",
         progname);
  printf("\nWhere:\n");
  printf("  -d <string> Device path.\n");
  printf("  -t <string> Device type: touch; button; utouch; ubutton.\n");
  printf("  -f <string> Recorder playback file path.\n");
  printf("  -p <string> Period(ms) range: "
         "<decimal-value min>-<decimal-value max>\n");
  printf("  -s <string> Screen resolution: "
         "<decimal-value hor_res>x<decimal-value ver_res>\n");

  exit(exitcode);
}

/****************************************************************************
 * Name: monkey_init
 ****************************************************************************/

static FAR struct monkey_s *monkey_init(
                            FAR const struct monkey_param_s *param)
{
  enum monkey_dev_type_e type;
  FAR struct monkey_s *monkey;
  struct monkey_config_s config;

  if (!param->dev_path)
    {
      printf(MONKEY_PREFIX ": NO device path\n");
      show_usage(MONKEY_PREFIX, EXIT_FAILURE);
      return NULL;
    }

  type = monkey_dev_name2type(param->dev_type);

  if (type == MONKEY_DEV_TYPE_UNKNOW)
    {
      printf(MONKEY_PREFIX ": Unknow device type\n");
      show_usage(MONKEY_PREFIX, EXIT_FAILURE);
      return NULL;
    }

  monkey = monkey_create(param->dev_path, type);

  if (!monkey)
    {
      goto failed;
    }

  monkey_config_default_init(&config);
#ifdef CONFIG_TESTING_MONKEY_SCREEN_IS_ROUND
  config.screen.type = MONKEY_SCREEN_TYPE_ROUND;
#endif
  config.screen.hor_res = param->hor_res;
  config.screen.ver_res = param->ver_res;
  config.period.min = param->period_min;
  config.period.max = param->period_max;
  monkey_set_config(monkey, &config);

  printf(MONKEY_PREFIX ": screen: %dx%d %s\n",
         config.screen.hor_res,
         config.screen.ver_res,
         (config.screen.type == MONKEY_SCREEN_TYPE_ROUND)
         ? "ROUND" : "RECT");

  printf(MONKEY_PREFIX ": period: %" PRIu32 " ~ %" PRIu32 "ms\n",
         config.period.min,
         config.period.max);

  if (MONKEY_IS_UINPUT_TYPE(type))
    {
      if (param->file_path)
        {
          monkey_set_mode(monkey, MONKEY_MODE_ORDER);
          if (!monkey_set_recorder_path(monkey, param->file_path))
            {
              goto failed;
            }
        }
      else
        {
          monkey_set_mode(monkey, MONKEY_MODE_RANDOM);
        }
    }
  else
    {
      if (!monkey_set_recorder_path(monkey,
                                    CONFIG_TESTING_MONKEY_REC_DIR_PATH))
        {
          goto failed;
        }

      monkey_set_period(monkey, config.period.min);
    }

  return monkey;

failed:
  if (monkey)
    {
      monkey_delete(monkey);
    }

  return NULL;
}

/****************************************************************************
 * Name: parse_commandline
 ****************************************************************************/

static void parse_commandline(int argc, FAR char **argv,
                              FAR struct monkey_param_s *param)
{
  int ch;
  int hor_res = -1;
  int ver_res = -1;
  int period_min = -1;
  int period_max = -1;
  int converted;

  memset(param, 0, sizeof(struct monkey_param_s));
  param->hor_res = CONFIG_TESTING_MONKEY_SCREEN_HOR_RES;
  param->ver_res = CONFIG_TESTING_MONKEY_SCREEN_VER_RES;
  param->period_min = CONFIG_TESTING_MONKEY_PERIOD_MIN_DEFAULT;
  param->period_max = CONFIG_TESTING_MONKEY_PERIOD_MAX_DEFAULT;

  while ((ch = getopt(argc, argv, "d:t:f:p:s:")) != ERROR)
    {
      switch (ch)
        {
          case 'd':
            param->dev_path = optarg;
            break;
          case 't':
            param->dev_type = optarg;
            break;
          case 'f':
            param->file_path = optarg;
            break;
          case 'p':
            converted = sscanf(optarg, "%d-%d", &period_min, &period_max);
            if (converted == 2 && period_min >= 0 && period_max >= 0)
              {
                param->period_min = period_min;
                param->period_max = period_max;
              }
            else
              {
                printf(MONKEY_PREFIX ": Error period range: %s\n", optarg);
                show_usage(argv[0], EXIT_FAILURE);
              }
            break;
          case 's':
            converted = sscanf(optarg, "%dx%d", &hor_res, &ver_res);
            if (converted == 2 && hor_res > 0 && ver_res > 0)
              {
                param->hor_res = hor_res;
                param->ver_res = ver_res;
              }
            else
              {
                printf(MONKEY_PREFIX ": Error screen resolution: %s\n",
                       optarg);
                show_usage(argv[0], EXIT_FAILURE);
              }
            break;
          case '?':
            printf(MONKEY_PREFIX ": Unknown option: %c\n", optopt);
            show_usage(argv[0], EXIT_FAILURE);
            break;
        }
    }
}

/****************************************************************************
 * Name: monkey_pause
 ****************************************************************************/

static int monkey_pause(void)
{
  sigset_t set;

  sigemptyset(&set);
  sigaddset(&set, SIGCONT);

  return sigwaitinfo(&set, NULL);
}

/****************************************************************************
 * Name: monkey_wait
 ****************************************************************************/

static enum monkey_wait_res_e monkey_wait(uint32_t ms)
{
  sigset_t set;
  struct timespec timeout;
  int ret;

  enum monkey_wait_res_e res = MONKEY_WAIT_RES_ERROR;

  if (ms == 0)
    {
      return MONKEY_WAIT_RES_AGAIN;
    }

  timeout.tv_sec = ms / 1000;
  timeout.tv_nsec = (ms % 1000) * 1000000;

  sigemptyset(&set);
  sigaddset(&set, SIGTSTP);

  ret = sigtimedwait(&set, NULL, &timeout);

  if (ret < 0)
    {
      int errcode = errno;
      if (errcode == EINTR)
        {
          res = MONKEY_WAIT_RES_STOP;
        }
      else if(errcode == EAGAIN)
        {
          res = MONKEY_WAIT_RES_AGAIN;
        }
      else
        {
          printf(MONKEY_PREFIX ": Unknow error: %d\n", errcode);
        }
    }
  else if (ret == SIGTSTP)
    {
      res = MONKEY_WAIT_RES_PAUSE;
    }

  return res;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: main or monkey_main
 *
 * Description:
 *
 * Input Parameters:
 *   Standard argc and argv
 *
 * Returned Value:
 *   Zero on success; a positive, non-zero value on failure.
 *
 ****************************************************************************/

int main(int argc, FAR char *argv[])
{
  struct monkey_param_s param;
  FAR struct monkey_s *monkey;
  parse_commandline(argc, argv, &param);

  monkey = monkey_init(&param);

  if (!monkey)
    {
      return ERROR;
    }

  while (1)
    {
      enum monkey_wait_res_e res;
      int sleep_ms;

      sleep_ms = monkey_update(monkey);

      if (sleep_ms < 0)
        {
          break;
        }

      res = monkey_wait(sleep_ms);

      if (res == MONKEY_WAIT_RES_AGAIN)
        {
          continue;
        }
      else if (res == MONKEY_WAIT_RES_PAUSE)
        {
          printf(MONKEY_PREFIX ": pause\n");
          if (monkey_pause() < 0)
            {
              break;
            }

          printf(MONKEY_PREFIX ": continue...\n");
        }
      else
        {
          /* STOP or ERROR */

          break;
        }
    }

  monkey_delete(monkey);

  return OK;
}
