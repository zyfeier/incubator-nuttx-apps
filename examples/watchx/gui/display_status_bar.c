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
#include "../bsp/bsp.h"
#include "display_private.h"

static lv_obj_t* label_name;
static lv_obj_t* label_time;
static lv_obj_t* cont_title;
static lv_task_t* task_update;

static void status_bar_update(lv_task_t* task)
{
    struct clock_value_s clock_value;
    clock_get_value(&clock_value);
    lv_label_set_text_fmt(label_time, "%02d:%02d", clock_value.hour, clock_value.min);
}

lv_obj_t* status_bar_create(lv_obj_t* par)
{
    lv_obj_t* cont = lv_cont_create(par, NULL);
    lv_obj_set_size(cont, lv_obj_get_width(par), 20);
    lv_obj_set_y(cont, -lv_obj_get_height(cont));
    lv_obj_set_style_local_border_width(cont, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, 0);

    lv_obj_t* label1 = lv_label_create(cont, NULL);

    lv_obj_set_style_local_text_font(label1, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &font_microsoft_yahei_16);
    lv_obj_set_style_local_text_color(label1, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);

    lv_label_set_text(label1, "-");
    lv_obj_align(label1, cont, LV_ALIGN_IN_LEFT_MID, 15, 0);

    lv_obj_t* label2 = lv_label_create(cont, NULL);

    lv_obj_set_style_local_text_font(label2, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &font_microsoft_yahei_16);
    lv_obj_set_style_local_text_color(label2, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);

    lv_label_set_text(label2, "00:00");
    lv_obj_align(label2, cont, LV_ALIGN_IN_RIGHT_MID, -15, 0);

    label_name = label1;
    label_time = label2;
    cont_title = cont;
    task_update = lv_task_create(status_bar_update, 1000, LV_TASK_PRIO_LOW, NULL);

    return cont;
}

void status_bar_set_name(const char* name)
{
    lv_label_set_text(label_name, name);
}

void status_bar_set_enable(bool en)
{
    lv_task_set_prio(task_update, en ? LV_TASK_PRIO_LOW : LV_TASK_PRIO_OFF);
    lv_coord_t y_target = en ? 0 : -lv_obj_get_height(cont_title);
    LV_OBJ_ADD_ANIM(cont_title, y, y_target, LV_ANIM_TIME_DEFAULT);
}
