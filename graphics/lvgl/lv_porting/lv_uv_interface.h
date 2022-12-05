/****************************************************************************
 * apps/graphics/lvgl/lv_porting/lv_uv_interface.h
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

#ifndef __APPS_GRAPHICS_LV_UV_INTERFACE_H
#define __APPS_GRAPHICS_LV_UV_INTERFACE_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifdef __cplusplus
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

/****************************************************************************
 * Type Definitions
 ****************************************************************************/

/****************************************************************************
 * Public Data
 ****************************************************************************/

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: lv_uv_start
 *
 * Description:
 *   Add the UI event loop to the uv_loop.
 *
 * Input Parameters:
 *   loop - Pointer to uv_loop.
 *
 ****************************************************************************/

void lv_uv_start(FAR void *loop);

/****************************************************************************
 * Name: lv_uv_close
 ****************************************************************************/

void lv_uv_close(void);

#undef EXTERN
#ifdef __cplusplus
}
#endif

#endif // __APPS_GRAPHICS_LV_UV_INTERFACE_H
