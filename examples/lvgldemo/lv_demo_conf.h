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
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Enable printf-ing data in demoes and examples */

#define LV_EX_PRINTF                            1

/* Add PC keyboard support to some examples */

#ifdef CONFIG_EXAMPLES_LVGLDEMO_EX_KEYBOARD
#  define LV_EX_KEYBOARD                        1
#else
#  define LV_EX_KEYBOARD                        0
#endif /* CONFIG_EXAMPLES_LVGLDEMO_EX_KEYBOARD */

/* Add encoder (mouse wheel) support to some examples */

#ifdef CONFIG_EXAMPLES_LVGLDEMO_EX_MOUSEWHEEL
#  define LV_EX_MOUSEWHEEL                      1
#else
#  define LV_EX_MOUSEWHEEL                      0
#endif /* CONFIG_EXAMPLES_LVGLDEMO_EX_MOUSEWHEEL */

/* Show some widget */

#ifdef CONFIG_EXAMPLES_LVGLDEMO_WIDGETS
#  define LV_USE_DEMO_WIDGETS                   1
#else
#  define LV_USE_DEMO_WIDGETS                   0
#endif /* CONFIG_EXAMPLES_LVGLDEMO_WIDGETS */

#if LV_USE_DEMO_WIDGETS

#ifdef CONFIG_EXAMPLES_LVGLDEMO_WIDGETS_SLIDESHOW
#  define LV_DEMO_WIDGETS_SLIDESHOW             1
#else
#  define LV_DEMO_WIDGETS_SLIDESHOW             0
#endif /* CONFIG_EXAMPLES_LVGLDEMO_WIDGETS_SLIDESHOW */

#endif /* LV_USE_DEMO_WIDGETS */

/* Demonstrate the usage of encoder and keyboard */

#ifdef CONFIG_EXAMPLES_LVGLDEMO_KEYPAD_AND_ENCODER
#  define LV_USE_DEMO_KEYPAD_AND_ENCODER        1
#else
#  define LV_USE_DEMO_KEYPAD_AND_ENCODER        0
#endif /* CONFIG_EXAMPLES_LVGLDEMO_KEYPAD_AND_ENCODER */

/* Benchmark your system */

#ifdef CONFIG_EXAMPLES_LVGLDEMO_BENCHMARK
#  define LV_USE_DEMO_BENCHMARK                 1
#else
#  define LV_USE_DEMO_BENCHMARK                 0
#endif /* CONFIG_EXAMPLES_LVGLDEMO_BENCHMARK */

/* Stress test for LVGL */

#ifdef CONFIG_EXAMPLES_LVGLDEMO_STRESS
#  define LV_USE_DEMO_STRESS                    1
#else
#  define LV_USE_DEMO_STRESS                    0
#endif /* CONFIG_EXAMPLES_LVGLDEMO_STRESS */

/* Music player demo */

#ifdef CONFIG_EXAMPLES_LVGLDEMO_MUSIC
#  define LV_USE_DEMO_MUSIC                     1
#else
#  define LV_USE_DEMO_MUSIC                     0
#endif /* CONFIG_EXAMPLES_LVGLDEMO_MUSIC */

#if LV_USE_DEMO_MUSIC

#ifdef CONFIG_EXAMPLES_LVGLDEMO_MUSIC_SQUARE
#  define LV_DEMO_MUSIC_SQUARE                  1
#else
#  define LV_DEMO_MUSIC_SQUARE                  0
#endif /* CONFIG_EXAMPLES_LVGLDEMO_MUSIC_SQUARE */

#ifdef CONFIG_EXAMPLES_LVGLDEMO_MUSIC_LANDSCAPE
#  define LV_DEMO_MUSIC_LANDSCAPE               1
#else
#  define LV_DEMO_MUSIC_LANDSCAPE               0
#endif /* CONFIG_EXAMPLES_LVGLDEMO_MUSIC_LANDSCAPE */

#ifdef CONFIG_EXAMPLES_LVGLDEMO_MUSIC_ROUND
#  define LV_DEMO_MUSIC_ROUND                   1
#else
#  define LV_DEMO_MUSIC_ROUND                   0
#endif /* CONFIG_EXAMPLES_LVGLDEMO_MUSIC_ROUND */

#ifdef CONFIG_EXAMPLES_LVGLDEMO_MUSIC_LARGE
#  define LV_DEMO_MUSIC_LARGE                   1
#else
#  define LV_DEMO_MUSIC_LARGE                   0
#endif

#ifdef CONFIG_EXAMPLES_LVGLDEMO_MUSIC_AUTO_PLAY
#  define LV_DEMO_MUSIC_AUTO_PLAY               1
#else
#  define LV_DEMO_MUSIC_AUTO_PLAY               0
#endif /* CONFIG_EXAMPLES_LVGLDEMO_MUSIC_AUTO_PLAY */

#endif /* LV_USE_DEMO_MUSIC */

#endif /* __APPS_EXAMPLES_LVGLDEMO_LV_DEMO_CONF_H */
