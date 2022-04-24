/****************************************************************************
 * apps/graphics/lvgl/lv_porting/lv_porting.c
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

#include "lv_porting.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: lv_porting_init
 *
 * Description:
 *   Initialize all porting.
 *
 ****************************************************************************/

void lv_porting_init(void)
{
#if defined(CONFIG_LV_USE_SYSLOG_INTERFACE)
  lv_syslog_interface_init();
#endif

#if defined(CONFIG_LV_USE_LCDDEV_INTERFACE)
  lv_lcddev_interface_init(NULL, 0);
#endif

#if defined(CONFIG_LV_USE_FBDEV_INTERFACE)
  lv_fbdev_interface_init(NULL, 0);
#endif

#if defined(CONFIG_LV_USE_GPU_INTERFACE)
  lv_gpu_interface_init();
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

#if defined(CONFIG_LV_USE_DECODER_JPEG_TURBO)
  lv_jpeg_turbo_init();
#endif

#if defined(CONFIG_LV_USE_DECODER_LODEPNG)
  lv_lodepng_init();
#endif
}
