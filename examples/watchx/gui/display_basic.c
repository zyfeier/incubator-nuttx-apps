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
#include "display_private.h"

static void display_common_init(void)
{
    /*watchx theme initialization*/
    lv_theme_t* th = lv_theme_watchx_init(
        LV_COLOR_WHITE,
        LV_COLOR_RED,
        0,
        &font_microsoft_yahei_28,
        &font_microsoft_yahei_28,
        &font_microsoft_yahei_28,
        &font_microsoft_yahei_28);
    lv_theme_set_act(th);

    /*APP window initialization*/
    app_window_create(lv_scr_act());

    /*Status bar initialization*/
    status_bar_create(lv_layer_sys());

    /*Page initialization*/
    display_page_init();
}

void display_init(void)
{
    display_common_init();
}
