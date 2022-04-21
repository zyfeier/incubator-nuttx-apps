/****************************************************************************
 * apps/graphics/lvgl/lv_porting/lv_porting.h
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

#ifndef __LV_PORTING_H__
#define __LV_PORTING_H__

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <lv_porting/lv_button_interface.h>
#include <lv_porting/lv_encoder_interface.h>
#include <lv_porting/lv_fbdev_interface.h>
#include <lv_porting/lv_gpu_interface.h>
#include <lv_porting/lv_lcddev_interface.h>
#include <lv_porting/lv_mem_interface.h>
#include <lv_porting/lv_keypad_interface.h>
#include <lv_porting/lv_touchpad_interface.h>

#include <lv_porting/decoder/jpeg_turbo/lv_jpeg_turbo.h>
#include <lv_porting/decoder/lodepng/lv_lodepng.h>

#include <lv_porting/lv_sched_note.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Type Definitions
 ****************************************************************************/

/****************************************************************************
 * Public Data
 ****************************************************************************/

#ifdef __cplusplus
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: lv_porting_init
 *
 * Description:
 *   Initialize all porting.
 *
 ****************************************************************************/

void lv_porting_init(void);

#undef EXTERN
#ifdef __cplusplus
}
#endif

#endif /* __LV_PORTING_H__ */
