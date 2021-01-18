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

static lv_obj_t *screen;
static lv_obj_t *obj_light;

static lv_auto_event_data_t ae_grp[] = {
    {&obj_light, LV_EVENT_CLICKED, 1000},
    {&obj_light, LV_EVENT_CLICKED, 1000},
    {&screen, LV_EVENT_LEAVE, 1000},
};

static void obj_light_set_brightness(uint8_t brightness_target, uint16_t time)
{
    LV_OBJ_ADD_ANIM(obj_light, brightness, brightness_target, time);
}

static void obj_light_event_handler(lv_obj_t *obj, lv_event_t event)
{
    if (event == LV_EVENT_CLICKED)
    {
        lv_color_t color = lv_obj_get_style_bg_color(obj, LV_OBJ_PART_MAIN);
        lv_color_t color_white = LV_COLOR_WHITE;
        uint8_t color_target;

        if (memcmp(&color, &color_white, sizeof(color_white)) == 0)
        {
            color_target = 0xAA;
        }
        else
        {
            color_target = 0xFF;
        }
        obj_light_set_brightness(color_target, 500);
    }
}

static void obj_light_create(lv_obj_t *par)
{
    lv_obj_t *obj = lv_obj_create(par, NULL);
    lv_obj_set_size(obj, lv_obj_get_width(par), lv_obj_get_height(par));
    lv_obj_set_style_default(obj);
    lv_obj_set_style_local_bg_color(obj, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);
    lv_obj_set_event_cb(obj, obj_light_event_handler);

    obj_light = obj;
}

static void page_event_handler(lv_obj_t *obj, lv_event_t event)
{
    if (event == LV_EVENT_GESTURE)
    {
        lv_gesture_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
        if (dir == LV_GESTURE_DIR_RIGHT)
        {
            lv_event_send(obj, LV_EVENT_LEAVE, NULL);
        }
    }
    else if (event == LV_EVENT_LEAVE)
    {
        obj_light_set_brightness(0, 200);
        page_return_menu(true);
    }
    else if (event == LV_EVENT_DELETE)
    {
    }
}

lv_obj_t *page_flashlight_create(void)
{
    AUTO_EVENT_CREATE(ae_grp);

    lv_obj_t *scr = page_screen_create();
    screen = scr;
    lv_obj_set_event_cb(scr, page_event_handler);

    obj_light_create(scr);

    lv_obj_set_brightness(obj_light, 0);
    obj_light_set_brightness(0xFF, 1000);

    return scr;
}
