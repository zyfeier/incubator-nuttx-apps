/*
 * Copyright (C) 2020 Xiaomi Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef __DISPLAY_PRIVATE_H
#define __DISPLAY_PRIVATE_H

#ifdef __cplusplus
extern "C"
{
#endif

/*LVGL*/
#include "lvgl/lvgl.h"
#include "lv_ext/lv_anim_timeline.h"
#include "lv_ext/lv_label_anim_effect.h"
#include "lv_ext/lv_obj_ext_func.h"
#include "lv_ext/lv_auto_event.h"

    /*fonts declare*/
    LV_FONT_DECLARE(font_erasbd_23);
    LV_FONT_DECLARE(font_erasbd_28);
    LV_FONT_DECLARE(font_erasbd_128);
    LV_FONT_DECLARE(font_bahnschrift_15);
    LV_FONT_DECLARE(font_bahnschrift_20);
    LV_FONT_DECLARE(font_bahnschrift_48);
    LV_FONT_DECLARE(font_bahnschrift_72);

#define PAGE_VER_RES 368
#define PAGE_HOR_RES 194

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof(arr[0]))
#define MAP(x, in_min, in_max, out_min, out_max) \
    (((x) - (in_min)) * ((out_max) - (out_min)) / ((in_max) - (in_min)) + (out_min))

#ifdef _MSC_VER
#if LV_COLOR_DEPTH == 16
#if LV_COLOR_16_SWAP == 0
#define _LV_COLOR_MAKE(r8, g8, b8)                                                                  \
    {                                                                                               \
        (uint16_t)((b8 >> 3) & 0x1FU), (uint16_t)((g8 >> 2) & 0x3FU), (uint16_t)((r8 >> 3) & 0x1FU) \
    }
#else
#define _LV_COLOR_MAKE(r8, g8, b8)                                                                                               \
    {                                                                                                                            \
        (uint16_t)((g8 >> 5) & 0x7U), (uint16_t)((r8 >> 3) & 0x1FU), (uint16_t)((b8 >> 3) & 0x1FU), (uint16_t)((g8 >> 2) & 0x7U) \
    }
#endif
#elif LV_COLOR_DEPTH == 32
#define _LV_COLOR_MAKE(r8, g8, b8) \
    {                              \
        b8, g8, r8, 0xff           \
    } /*Fix 0xff alpha*/
#endif
#else
#define _LV_COLOR_MAKE LV_COLOR_MAKE
#endif

#define AUTO_EVENT_CREATE(ae_data)                                                    \
    do                                                                                \
    {                                                                                 \
        if (page_get_autoshow_enable())                                               \
        {                                                                             \
            lv_auto_event_t *ae = lv_auto_event_create(ae_data, ARRAY_SIZE(ae_data)); \
            page_set_autoshow_ae_act(ae);                                             \
        }                                                                             \
    } while (0)

#ifdef __cplusplus
}
#endif

#endif
