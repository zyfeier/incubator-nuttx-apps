/****************************************************************************
 * apps/examples/lvgldemo/lv_demo_conf.h
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

#ifndef __APPS_EXAMPLES_LVGLDEMO_LV_DEMO_CONF_H
#define __APPS_EXAMPLES_LVGLDEMO_LV_DEMO_CONF_H

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Enable printf-ing data in demoes and examples */

#define LV_EX_PRINTF       1

/* Add PC keyboard support to some examples
 * (`lv_drivers` repository is required)
 */

#define LV_EX_KEYBOARD     0

/* Add 'encoder' (mouse wheel) support to some examples
 * (`lv_drivers` repository is required)
 */

#define LV_EX_MOUSEWHEEL   0

/* Show some widget */

#define LV_USE_DEMO_WIDGETS        1

#if LV_USE_DEMO_WIDGETS
#  ifdef CONFIG_EXAMPLES_LVGLDEMO_WIDGETS_SLIDESHOW
#    define LV_DEMO_WIDGETS_SLIDESHOW      CONFIG_EXAMPLES_LVGLDEMO_WIDGETS_SLIDESHOW
#  else
#    define LV_DEMO_WIDGETS_SLIDESHOW      0
#  endif
#endif /* LV_USE_DEMO_WIDGETS */

/* Printer demo, optimized for 800x480 */

#define LV_USE_DEMO_PRINTER        1

/* Demonstrate the usage of encoder and keyboard */

#define LV_USE_DEMO_KEYPAD_AND_ENCODER     1

/* Benchmark your system */

#define LV_USE_DEMO_BENCHMARK      1

/* Stress test for LVGL */

#define LV_USE_DEMO_STRESS         1

/* Music player demo */

#define LV_USE_DEMO_MUSIC          1

#if LV_USE_DEMO_MUSIC

#ifdef CONFIG_EXAMPLES_LVGLDEMO_MUSIC_LANDSCAPE
#  define LV_DEMO_MUSIC_LANDSCAPE  CONFIG_EXAMPLES_LVGLDEMO_MUSIC_LANDSCAPE
#else
#  define LV_DEMO_MUSIC_LANDSCAPE  0
#endif

#ifdef CONFIG_EXAMPLES_LVGLDEMO_MUSIC_LARGE
#  define LV_DEMO_MUSIC_LARGE      CONFIG_EXAMPLES_LVGLDEMO_MUSIC_LARGE
#else
#  define LV_DEMO_MUSIC_LARGE      0
#endif

#ifdef CONFIG_EXAMPLES_LVGLDEMO_MUSIC_AUTO_PLAY
#  define LV_DEMO_MUSIC_AUTO_PLAY  CONFIG_EXAMPLES_LVGLDEMO_MUSIC_AUTO_PLAY
#else
#  define LV_DEMO_MUSIC_AUTO_PLAY  0
#endif 

#endif /* LV_USE_DEMO_MUSIC */

#endif /* __APPS_EXAMPLES_LVGLDEMO_LV_DEMO_CONF_H */
