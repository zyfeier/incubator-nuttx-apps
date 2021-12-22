/****************************************************************************
 * apps/examples/lvgldemo/lvgldemo.c
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

#include <sys/boardctl.h>
#include <unistd.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <debug.h>

#include <lvgl/lvgl.h>
#include <lv_porting/lv_porting.h>
#include "lv_demos/lv_demo.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Should we perform board-specific driver initialization?  There are two
 * ways that board initialization can occur:  1) automatically via
 * board_late_initialize() during bootupif CONFIG_BOARD_LATE_INITIALIZE
 * or 2).
 * via a call to boardctl() if the interface is enabled
 * (CONFIG_BOARDCTL=y).
 * If this task is running as an NSH built-in application, then that
 * initialization has probably already been performed otherwise we do it
 * here.
 */

#undef NEED_BOARDINIT

#if defined(CONFIG_BOARDCTL) && !defined(CONFIG_NSH_ARCHINIT)
#  define NEED_BOARDINIT 1
#endif

/****************************************************************************
 * Private Type Declarations
 ****************************************************************************/

typedef void (*demo_create_func_t)(void);

struct func_key_pair_s
{
  FAR const char *name;
  demo_create_func_t func;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct func_key_pair_s func_key_pair[] =
{
  { "benchmark",      lv_demo_benchmark      },
  { "keypad_encoder", lv_demo_keypad_encoder },
  { "music",          lv_demo_music          },
  { "stress",         lv_demo_stress         },
  { "widgets",        lv_demo_widgets        }
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: show_usage
 ****************************************************************************/

static void show_usage(void)
{
  printf("\nUsage: lvgldemo demo_name\n");
  printf("\ndemo_name:\n");
  printf("  benchmark\n");
  printf("  keypad_encoder\n");
  printf("  music\n");
  printf("  stress\n");
  printf("  widgets\n");
  exit(EXIT_FAILURE);
}

/****************************************************************************
 * Name: find_demo_create_func
 ****************************************************************************/

static demo_create_func_t find_demo_create_func(FAR const char *name)
{
  int i;
  const int len = sizeof(func_key_pair)
                  / sizeof(struct func_key_pair_s);

  for (i = 0; i < len; i++)
    {
      if (strcmp(name, func_key_pair[i].name) == 0)
        {
          return func_key_pair[i].func;
        }
    }

  printf("lvgldemo: %s not found.\n", name);
  return NULL;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: main or lvgldemo_main
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
  demo_create_func_t demo_create_func;

  if (argc != 2)
    {
      show_usage();
      return EXIT_FAILURE;
    }

  demo_create_func = find_demo_create_func(argv[1]);

  if (demo_create_func == NULL)
    {
      show_usage();
      return EXIT_FAILURE;
    }

#ifdef NEED_BOARDINIT
  /* Perform board-specific driver initialization */

  boardctl(BOARDIOC_INIT, 0);

#ifdef CONFIG_BOARDCTL_FINALINIT
  /* Perform architecture-specific final-initialization (if configured) */

  boardctl(BOARDIOC_FINALINIT, 0);
#endif
#endif

  /* LVGL initialization */

  lv_init();

  /* LVGL interface initialization */

  lv_fs_interface_init();

#if defined(CONFIG_LV_USE_LCDDEV_INTERFACE)
  lv_lcddev_interface_init(NULL, 0);
#endif

#if defined(CONFIG_LV_USE_GPU_INTERFACE)
  lv_gpu_interface_init();
#endif

#if defined(CONFIG_LV_USE_FBDEV_INTERFACE)
  lv_fbdev_interface_init(NULL, 0);
#endif

#if defined(CONFIG_LV_USE_BUTTON_INTERFACE)
  lv_button_interface_init(NULL);
#endif

#if defined(CONFIG_LV_USE_KEYPAD_INTERFACE)
  lv_keypad_interface_init(NULL);
#endif

#if defined(CONFIG_LV_USE_TOUCHPAD_INTERFACE)
  lv_touchpad_interface_init(NULL);
#endif

#if defined(CONFIG_LV_USE_ENCODER_INTERFACE)
  lv_encoder_interface_init(NULL);
#endif

  /* LVGL demo creation */

  demo_create_func();

  /* Handle LVGL tasks */

  while (1)
    {
      lv_timer_handler();
      usleep(1000);
    }

  return EXIT_SUCCESS;
}
