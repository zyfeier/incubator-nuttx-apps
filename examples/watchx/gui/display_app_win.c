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

static lv_obj_t* app_window_grp[page_id_max];

lv_obj_t* app_window_get_obj(uint8_t page_id)
{
    return (page_id < ARRAY_SIZE(app_window_grp)) ? app_window_grp[page_id] : NULL;
}

void app_window_create(lv_obj_t* par)
{
    static lv_style_t style;
    lv_style_init(&style);
    lv_style_set_radius(&style, LV_STATE_DEFAULT, 0);
    lv_style_set_border_width(&style, LV_STATE_DEFAULT, 0);
    lv_style_set_bg_grad_color(&style, LV_STATE_DEFAULT, LV_COLOR_BLACK);
    lv_style_set_bg_color(&style, LV_STATE_DEFAULT, LV_COLOR_BLACK);
    lv_style_set_text_color(&style, LV_STATE_DEFAULT, LV_COLOR_WHITE);

    for (int i = 0; i < ARRAY_SIZE(app_window_grp); i++) {
        lv_obj_t* obj = lv_obj_create(par, NULL);
        lv_obj_add_style(obj, LV_CONT_PART_MAIN, &style);
        //lv_obj_set_size(obj, lv_obj_get_width(par), lv_obj_get_height(par));
        lv_obj_set_size(obj, 240, 240);
        lv_obj_align(obj, NULL, LV_ALIGN_CENTER, 0, 0);

        app_window_grp[i] = obj;
    }
}
