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
#include "page.h"

static bool autoshow_enable = false;
static lv_auto_event_t *auto_event_act = NULL;

void page_set_autoshow_ae_act(lv_auto_event_t *ae)
{
    auto_event_act = ae;
}

bool page_get_autoshow_enable()
{
    return autoshow_enable;
}

void page_set_autoshow_enable(bool en)
{
    if (en == false && auto_event_act != NULL)
    {
        lv_auto_event_del(auto_event_act);
        auto_event_act = NULL;
    }

    autoshow_enable = en;
}

void lv_obj_set_style_default(lv_obj_t *obj)
{
    static lv_style_t style = {NULL};

    if (style.map == NULL)
    {
        lv_style_init(&style);
        lv_style_set_bg_color(&style, LV_OBJ_PART_MAIN, LV_COLOR_BLACK);
        lv_style_set_border_width(&style, LV_OBJ_PART_MAIN, 0);
        lv_style_set_radius(&style, LV_OBJ_PART_MAIN, 0);
    }

    lv_obj_add_style(obj, LV_OBJ_PART_MAIN, &style);
}

void lv_obj_set_click_anim_default(lv_obj_t *obj)
{
    static lv_style_t style = {NULL};

    if (style.map == NULL)
    {
        lv_style_init(&style);
        lv_style_set_radius(&style, LV_STATE_DEFAULT, 10);
        lv_style_set_border_width(&style, LV_STATE_DEFAULT, 0);
        lv_style_set_bg_color(&style, LV_STATE_PRESSED, LV_COLOR_GRAY);
        lv_style_set_transform_width(&style, LV_STATE_PRESSED, +5);
        lv_style_set_transform_height(&style, LV_STATE_PRESSED, -5);

        static lv_anim_path_t path_ease_in_out;
        lv_anim_path_init(&path_ease_in_out);
        lv_anim_path_set_cb(&path_ease_in_out, lv_anim_path_ease_in_out);

        static lv_anim_path_t path_overshoot;
        lv_anim_path_init(&path_overshoot);
        lv_anim_path_set_cb(&path_overshoot, lv_anim_path_overshoot);

        lv_style_set_transition_path(&style, LV_STATE_PRESSED, &path_ease_in_out);
        lv_style_set_transition_path(&style, LV_STATE_DEFAULT, &path_overshoot);

        lv_style_set_transition_time(&style, LV_STATE_DEFAULT, 200);
        lv_style_set_transition_prop_1(&style, LV_STATE_DEFAULT, LV_STYLE_BG_COLOR);
        lv_style_set_transition_prop_2(&style, LV_STATE_DEFAULT, LV_STYLE_TRANSFORM_WIDTH);
        lv_style_set_transition_prop_3(&style, LV_STATE_DEFAULT, LV_STYLE_TRANSFORM_HEIGHT);
    }

    lv_obj_add_style(obj, LV_OBJ_PART_MAIN, &style);
}

void page_return_menu(bool auto_del)
{
    lv_obj_t *scr = page_menu_create();
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_MOVE_RIGHT, LV_ANIM_TIME_DEFAULT, 0, auto_del);
}

lv_obj_t *page_screen_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL, NULL);

    lv_obj_set_style_default(scr);
    lv_obj_set_size(scr, PAGE_HOR_RES, PAGE_VER_RES);
    return scr;
}
