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
#ifndef __PAGE_H
#define __PAGE_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "../display.h"

    /*common*/
    void lv_obj_set_style_default(lv_obj_t *obj);
    void lv_obj_set_click_anim_default(lv_obj_t *obj);
    void page_return_menu(bool auto_del);
    bool page_get_autoshow_enable(void);
    void page_set_autoshow_enable(bool en);
    void page_set_autoshow_ae_act(lv_auto_event_t *ae);
    lv_obj_t *page_screen_create(void);

    /*pages*/
    lv_obj_t *page_dialplate_create(void);
    lv_obj_t *page_menu_create(void);
    lv_obj_t *page_stop_watch_create(void);
    lv_obj_t *page_sport_create(void);
    lv_obj_t *page_music_create(void);
    lv_obj_t *page_heart_rate_create(void);
    lv_obj_t *page_sleep_create(void);
    lv_obj_t *page_settings_create(void);
    lv_obj_t *page_flashlight_create(void);

#ifdef __cplusplus
}
#endif

#endif
