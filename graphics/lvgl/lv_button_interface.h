/****************************************************************************
 * graphics/lvgl/lv_button_interface.h
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

#ifndef __LV_BUTTON_INTERFACE_H__
#define __LV_BUTTON_INTERFACE_H__

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <lvgl/lvgl.h>
#include <nuttx/config.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifdef CONFIG_LV_USE_BUTTON_INTERFACE
#define LV_USE_BUTTON_INTERFACE     CONFIG_LV_USE_BUTTON_INTERFACE
#else
#define LV_USE_BUTTON_INTERFACE     0
#endif

/****************************************************************************
 * Type Definitions
 ****************************************************************************/

/****************************************************************************
 * Public Data
 ****************************************************************************/

#if LV_USE_BUTTON_INTERFACE

void lv_button_interface_init(void);
bool lv_button_interface_read(lv_indev_drv_t *drv, lv_indev_data_t *data);

#endif /* LV_USE_BUTTON_INTERFACE */

#endif /* __LV_BUTTON_INTERFACE_H__ */
