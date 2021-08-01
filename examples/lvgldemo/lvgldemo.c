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
#include "lv_examples/lv_demo.h"

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
 * Public Functions Prototypes
 ****************************************************************************/

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Private Data
 ****************************************************************************/

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

#if defined(CONFIG_LV_USE_FFMPEG_INTERFACE)
  lv_ffmpeg_interface_init();
#endif

#if defined(CONFIG_LV_USE_LCDDEV_INTERFACE)
  lv_lcddev_interface_init(NULL, 0);
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

  /* LVGL demo creation */

#if defined(CONFIG_EXAMPLES_LVGLDEMO_BENCHMARK)
  lv_demo_benchmark();
#elif defined(CONFIG_EXAMPLES_LVGLDEMO_KEYPAD_ENCODER)
  lv_demo_keypad_encoder();
#elif defined(CONFIG_EXAMPLES_LVGLDEMO_MUSIC)
  lv_demo_music();
#elif defined(CONFIG_EXAMPLES_LVGLDEMO_STRESS)
  lv_demo_stress();
#elif defined(CONFIG_EXAMPLES_LVGLDEMO_WIDGETS)
  lv_demo_widgets();
#endif

  /* Handle LVGL tasks */

  while (1)
    {
      lv_timer_handler();
      usleep(5000);
    }

  return EXIT_SUCCESS;
}
