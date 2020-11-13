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
#include "backlight.h"
#include "../gui/display_private.h"

static uint16_t backlight_value = 0;

static void backlight_anim_callback(void* obj, int16_t brightness)
{
    backlight_set_value(brightness);
}

void backlight_set_gradual(uint16_t target, uint16_t time)
{
    lv_obj_add_anim(
        NULL, NULL,
        (lv_anim_exec_xcb_t)backlight_anim_callback,
        backlight_get_value(), target,
        time,
        0,
        NULL,
        lv_anim_path_ease_out);
}

uint16_t backlight_get_value(void)
{
    return backlight_value;
}

void backlight_set_value(int16_t val)
{
    backlight_value = val;
}
