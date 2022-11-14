/****************************************************************************
 * apps/testing/drivertest/drivertest_watchdog.c
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
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/boardctl.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <debug.h>

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <cmocka.h>

#include <nuttx/timers/watchdog.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define WDG_DEFAULT_DEVPATH "/dev/watchdog0"
#define WDG_DEFAULT_DATAPATH "/data/wdg"
#define WDG_DEFAULT_PINGTIMER 5000
#define WDG_DEFAULT_PINGDELAY 500
#define WDG_DEFAULT_TIMEOUT 2000
#define WDG_RESET_OK 1
#define WDG_RESET_ERROR -1

#define OPTARG_TO_VALUE(value, type, base)                            \
  do                                                                  \
    {                                                                 \
      FAR char *ptr;                                                  \
      value = (type)strtoul(optarg, &ptr, base);                      \
      if (*ptr != '\0')                                               \
        {                                                             \
          printf("Parameter error: -%c %s\n", ch, optarg);            \
          show_usage(argv[0], wdg_state, EXIT_FAILURE);               \
        }                                                             \
    } while (0)

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct wdg_state_s
{
  char devpath[PATH_MAX];
  char datapath[PATH_MAX];
  uint32_t pingtime;
  uint32_t pingdelay;
  uint32_t timeout;
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: get_timestamp
 ****************************************************************************/

static uint32_t get_timestamp(void)
{
  struct timespec ts;
  uint32_t ms;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  ms = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
  return ms;
}

/****************************************************************************
 * Name: get_time_elaps
 ****************************************************************************/

static uint32_t get_time_elaps(uint32_t prev_tick)
{
  uint32_t act_time = get_timestamp();

  /* If there is no overflow in sys_time simple subtract */

  if (act_time >= prev_tick)
    {
      prev_tick = act_time - prev_tick;
    }
  else
    {
      prev_tick = UINT32_MAX - prev_tick + 1;
      prev_tick += act_time;
    }

  return prev_tick;
}

/****************************************************************************
 * Name: mark_reset_cause
 ****************************************************************************/

static void mark_reset_cause(FAR const char * path, int reset_result)
{
  int data;

  int fd = open(path, O_WRONLY | O_CREAT);
  assert_return_code(fd, errno);

  data = write(fd, &reset_result, sizeof(reset_result));
  assert_int_equal(data, sizeof(reset_result));

  close(fd);
}

/****************************************************************************
 * Name: show_usage
 ****************************************************************************/

static void show_usage(FAR const char *progname,
                       FAR struct wdg_state_s *wdg_state, int exitcode)
{
  printf("Usage: %s -d <devpath> -p <datapath> -t <pingtime>"
         "-l <pingdelay> -o <timeout>\n", progname);
  printf("  [-d devpath] selects the WATCHDOG device.\n"
         "  Default: %s Current: %s\n", WDG_DEFAULT_DEVPATH,
         wdg_state->devpath);
  printf("  [-p datapath] selects the WATCHDOG reset cause path.\n"
         "  Default: %s Current: %s\n", WDG_DEFAULT_DATAPATH,
         wdg_state->datapath);
  printf("  [-t pingtime] Selects the <delay> time in milliseconds.\n"
         "  Default: %" PRIu32 "Current: %" PRIu32 "\n",
         WDG_DEFAULT_PINGTIMER, wdg_state->pingtime);
  printf("  [-l pingdelay] Time delay between pings in milliseconds.\n"
         "  Default: %" PRIu32 "Current: %" PRIu32 "\n",
         WDG_DEFAULT_PINGDELAY, wdg_state->pingdelay);
  printf("  [-o timeout] Time in milliseconds that the testcase will\n"
         "  Default: %" PRIu32 "Current: %" PRIu32 "\n",
         WDG_DEFAULT_TIMEOUT, wdg_state->timeout);
  printf("  [-h] = Shows this message and exits\n");

  exit(exitcode);
}

/****************************************************************************
 * Name: parse_commandline
 ****************************************************************************/

static void parse_commandline(FAR struct wdg_state_s *wdg_state, int argc,
                              FAR char **argv)
{
  int ch;
  int converted;

  while ((ch = getopt(argc, argv, "d:p:t:l:o:h")) != ERROR)
    {
      switch (ch)
        {
          case 'd':
            strncpy(wdg_state->devpath, optarg, sizeof(wdg_state->devpath));
            wdg_state->devpath[sizeof(wdg_state->devpath) - 1] = '\0';
            break;

          case 'p':
            strncpy(wdg_state->datapath, optarg,
                    sizeof(wdg_state->datapath));
            wdg_state->datapath[sizeof(wdg_state->datapath) - 1] = '\0';
            break;

          case 't':
            OPTARG_TO_VALUE(converted, uint32_t, 10);
            if (converted < 1 || converted > INT_MAX)
              {
                printf("signal out of range: %" PRIu32 "\n", converted);
                show_usage(argv[0], wdg_state, EXIT_FAILURE);
              }

            wdg_state->pingtime = (uint32_t)converted;
            break;

          case 'l':
            OPTARG_TO_VALUE(converted, uint32_t, 10);
            if (converted < 1 || converted > INT_MAX)
              {
                printf("signal out of range: %" PRIu32 "\n", converted);
                show_usage(argv[0], wdg_state, EXIT_FAILURE);
              }

            wdg_state->pingdelay = (uint32_t)converted;
            break;

          case 'o':
            OPTARG_TO_VALUE(converted, uint32_t, 10);
            if (converted < 1 || converted > INT_MAX)
              {
                printf("signal out of range: %" PRIu32 "\n", converted);
                show_usage(argv[0], wdg_state, EXIT_FAILURE);
              }

            wdg_state->timeout = (uint32_t)converted;
            break;

          case '?':
            printf("Unsupported option: %s\n", optarg);
            show_usage(argv[0], wdg_state, EXIT_FAILURE);
            break;
        }
    }

  printf("devpath = %s\n"
        "datapath = %s\n"
        "pingtime = %" PRIu32 "\n"
        "pingdelay =  %" PRIu32 "\n"
        "timeout =  %" PRIu32 "\n",
        wdg_state->devpath,
        wdg_state->datapath,
        wdg_state->pingtime,
        wdg_state->pingdelay,
        wdg_state->timeout);
}

/****************************************************************************
 * Name: test_case_wdog
 ****************************************************************************/

static void test_case_wdog(FAR void **state)
{
  int data_fd;
  int dev_fd;
  int ret;
  int test_result;
  ssize_t data;
  uint32_t start_ms;
  FAR struct wdg_state_s *wdg_state;
#ifdef CONFIG_BOARDCTL_RESET_CAUSE
  struct boardioc_reset_cause_s cause;
#endif

  wdg_state = (FAR struct wdg_state_s *)*state;

  /* Get reset reason. */

  data_fd = open(wdg_state->datapath, O_RDONLY);

  if (data_fd > 0)
    {
      data = read(data_fd, &test_result, sizeof(test_result));
      assert_int_equal(data, sizeof(test_result));

      close(data_fd);
      ret = remove(wdg_state->datapath);
      assert_return_code(ret, OK);
      assert_int_equal(test_result, WDG_RESET_OK);

#ifdef CONFIG_BOARDCTL_RESET_CAUSE
      memset(&cause, 0, sizeof(cause));
      ret = boardctl(BOARDIOC_RESET_CAUSE, (uintptr_t)&cause);
      assert_return_code(ret, OK);
      assert_int_equal(cause.cause, BOARDIOC_RESETCAUSE_CORE_MWDT);
#endif
      return;
    }

  /* Open the watchdog device for reading */

  dev_fd = open(wdg_state->devpath, O_RDONLY);
  assert_return_code(dev_fd, errno);

  /* Set the watchdog timeout */

  ret = ioctl(dev_fd, WDIOC_SETTIMEOUT, wdg_state->timeout);
  assert_return_code(ret, errno);

  /* Then start the watchdog timer. */

  ret = ioctl(dev_fd, WDIOC_START, 0);
  assert_return_code(ret, OK);

  /* Get the starting time */

  start_ms = get_timestamp();

  /* Then ping */

  while (get_time_elaps(start_ms) < wdg_state->pingtime)
    {
      /* Sleep for the requested amount of time */

      usleep(wdg_state->pingdelay * 1000);

      /* Then ping */

      ret = ioctl(dev_fd, WDIOC_KEEPALIVE, 0);
      assert_return_code(ret, OK);
    }

  mark_reset_cause(wdg_state->datapath, WDG_RESET_OK);

  /* Then stop pinging */

  /* Sleep for the requested amount of time */

  usleep(2 * wdg_state->timeout * 1000);

  mark_reset_cause(wdg_state->datapath, WDG_RESET_ERROR);

  /* We should not get here */

  ret = ioctl(dev_fd, WDIOC_STOP, 0);
  assert_return_code(ret, errno);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: main
 ****************************************************************************/

int main(int argc, FAR char *argv[])
{
  struct wdg_state_s wdg_state =
  {
    .devpath = WDG_DEFAULT_DEVPATH,
    .datapath = WDG_DEFAULT_DATAPATH,
    .pingtime = WDG_DEFAULT_PINGTIMER,
    .pingdelay = WDG_DEFAULT_PINGDELAY,
    .timeout = WDG_DEFAULT_TIMEOUT
  };

  parse_commandline(&wdg_state, argc, argv);

  const struct CMUnitTest tests[] =
  {
    cmocka_unit_test_prestate(test_case_wdog, &wdg_state)
  };

  return cmocka_run_group_tests(tests, NULL, NULL);
}
