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
#include "app.h"
#include "display.h"
#include "page/page.h"

static void theme_init(void)
{
    lv_theme_t *th = lv_theme_material_init(
        LV_THEME_DEFAULT_COLOR_PRIMARY,
        LV_THEME_DEFAULT_COLOR_SECONDARY,
        LV_THEME_MATERIAL_FLAG_DARK,
        &font_erasbd_23,
        &font_erasbd_28,
        &font_erasbd_23,
        &font_erasbd_28);

    lv_theme_set_act(th);
}

void app_create(void)
{
    theme_init();

    lv_disp_set_bg_color(lv_disp_get_default(), LV_COLOR_BLACK);

    lv_obj_t *scr = page_dialplate_create();
    lv_scr_load(scr);
}
