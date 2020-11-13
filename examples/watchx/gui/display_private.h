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
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/*LVGL*/
#include "lv_ext/lv_anim_timeline.h"
#include "lv_ext/lv_label_anim_effect.h"
#include "lv_ext/lv_obj_ext_func.h"
#include "lv_ext/lv_theme_watchx.h"
#include "lvgl/lvgl.h"

/*fonts declare*/
LV_FONT_DECLARE(font_rexbold_28);
LV_FONT_DECLARE(font_rexbold_89);
LV_FONT_DECLARE(font_microsoft_yahei_16);
LV_FONT_DECLARE(font_microsoft_yahei_28);
LV_FONT_DECLARE(font_microsoft_yahei_50);

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof(arr[0]))
#define MAP(x, in_min, in_max, out_min, out_max) \
    (((x) - (in_min)) * ((out_max) - (out_min)) / ((in_max) - (in_min)) + (out_min))

/*pages*/
#include "page_manager/page_manager.h"
enum page_id {
    /*Keep it*/
    page_id_none,

    /*User page*/
    page_id_main_menu,
    page_id_dialplate,
    page_id_bluetooth,
    page_id_calculator,
    page_id_fileexplorer,
    page_id_game,
    page_id_heart_rate,
    page_id_music,
    page_id_settings,
    page_id_sleep,
    page_id_sport,
    page_id_stopwatch,
    page_id_timeset,

    /*Keep it*/
    page_id_max
};

/*display*/
void display_init(void);
void display_update(void);
void display_page_delay(uint32_t ms);
void display_page_init(void);

#define PAGE_EXPORT(name)                                                 \
    static struct page_manager_s* page_manager;                           \
    static lv_obj_t* app_window;                                          \
    static void setup(void);                                              \
    static void quit(void);                                               \
    static void event_handler(void* obj, uint8_t event);                  \
    static void pm_event_handler(void* obj, uint8_t event)                \
    {                                                                     \
        if (obj == page_manager) {                                        \
            switch (event) {                                              \
            case page_manager_event_setup:                                \
                setup();                                                  \
                break;                                                    \
            case page_manager_event_quit:                                 \
                quit();                                                   \
                break;                                                    \
            case page_manager_event_loop:                                 \
                /*loop();*/                                               \
                break;                                                    \
            default:                                                      \
                break;                                                    \
            }                                                             \
        } else {                                                          \
            event_handler(obj, event);                                    \
        }                                                                 \
    }                                                                     \
    void page_register_##name(struct page_manager_s* pm, uint8_t page_id) \
    {                                                                     \
        app_window = app_window_get_obj(page_id);                         \
        lv_obj_set_event_cb(app_window, (lv_event_cb_t)event_handler);    \
        pm->register_page(pm, page_id, pm_event_handler, #name);          \
        page_manager = pm;                                                \
    }

/*app_window*/
void app_window_create(lv_obj_t* par);
lv_obj_t* app_window_get_obj(uint8_t page_id);
#define APP_WIN_HEIGHT lv_obj_get_height(app_window)
#define APP_WIN_WIDTH lv_obj_get_width(app_window)

/*status_bar*/
lv_obj_t* status_bar_create(lv_obj_t* par);
void status_bar_set_name(const char* name);
void status_bar_set_enable(bool en);

#ifdef __cplusplus
}
#endif

#endif
