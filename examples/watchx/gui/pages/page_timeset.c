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
#include "../../bsp/bsp.h"
#include "../display_private.h"

PAGE_EXPORT(timeset);

static lv_obj_t* btn_confirm;
static lv_obj_t* btn_switch;
static lv_obj_t* cont_timeset;
static lv_obj_t* cont_dateset;

static bool is_dateset = false;

static lv_coord_t cont_timeset_x_target = 0;
static lv_coord_t cont_dateset_x_target = 0;

struct roller_time_s {
    const char* text;
    int pos;
    lv_obj_t* roller;
    lv_obj_t* label;
};

static struct roller_time_s roller_time_grp[] = {
    { "hour", 24 },
    { "min", 60 },
    { "sec", 60 }
};

static struct roller_time_s roller_date_grp[] = {
    { "year", 30 },
    { "mon", 12 },
    { "day", 31 }
};

static const char* roller_timeset_str = "00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n"
                                        "10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n"
                                        "20\n21\n22\n23\n24\n25\n26\n27\n28\n29\n"
                                        "30\n31\n32\n33\n34\n35\n36\n37\n38\n39\n"
                                        "40\n41\n42\n43\n44\n45\n46\n47\n48\n49\n"
                                        "50\n51\n52\n53\n54\n55\n56\n57\n58\n59\n";

static const char* roller_dateset_str = "01\n02\n03\n04\n05\n06\n07\n08\n09\n"
                                        "10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n"
                                        "20\n21\n22\n23\n24\n25\n26\n27\n28\n29\n"
                                        "30\n31\n";

#define ROLLER_STR_SET_ENT(buf, pos) (buf)[3 * pos - 1] = '\0'

static void btn_grp_event_handler(lv_obj_t* obj, lv_event_t event)
{
    if (event == LV_EVENT_CLICKED) {
        if (obj == btn_confirm) {
            struct clock_value_s clock_value;
            clock_value.year = lv_roller_get_selected(roller_date_grp[0].roller) + 2001;
            clock_value.month = lv_roller_get_selected(roller_date_grp[1].roller) + 1;
            clock_value.date = lv_roller_get_selected(roller_date_grp[2].roller) + 1;
            clock_value.hour = lv_roller_get_selected(roller_time_grp[0].roller);
            clock_value.min = lv_roller_get_selected(roller_time_grp[1].roller);
            clock_value.sec = lv_roller_get_selected(roller_time_grp[2].roller);
            clock_set_value(&clock_value);
        } else if (obj == btn_switch) {
            if (!is_dateset) {
                LV_OBJ_ADD_ANIM(cont_timeset, x, cont_timeset_x_target - APP_WIN_WIDTH, 100);
                LV_OBJ_ADD_ANIM(cont_dateset, x, cont_dateset_x_target - APP_WIN_WIDTH, 100);
            } else {
                LV_OBJ_ADD_ANIM(cont_timeset, x, cont_timeset_x_target, 100);
                LV_OBJ_ADD_ANIM(cont_dateset, x, cont_dateset_x_target, 100);
            }
            is_dateset = !is_dateset;
        }
    }
}

static void roller_timeset_create(lv_obj_t* par)
{
    lv_obj_t* cont = lv_cont_create(par, NULL);
    lv_obj_set_size(cont, 180, 150);
    lv_obj_align(cont, NULL, LV_ALIGN_CENTER, 0, -10);
    lv_obj_set_style_local_border_width(cont, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, 0);
    for (int i = 0; i < ARRAY_SIZE(roller_time_grp); i++) {
        lv_obj_t* roller = lv_roller_create(cont, NULL);

        char buf[200];
        strcpy(buf, roller_timeset_str);

        ROLLER_STR_SET_ENT(buf, roller_time_grp[i].pos);

        lv_roller_set_options(roller, buf, LV_ROLLER_MODE_INIFINITE);
        lv_roller_set_visible_row_count(roller, 2);

        lv_obj_set_style_local_text_font(roller, LV_ROLLER_PART_BG, LV_STATE_DEFAULT, &font_microsoft_yahei_28);
        lv_obj_set_style_local_border_width(roller, LV_ROLLER_PART_BG, LV_STATE_DEFAULT, 0);
        lv_obj_set_style_local_radius(roller, LV_ROLLER_PART_BG, LV_STATE_DEFAULT, 8);
        lv_obj_set_style_local_bg_color(roller, LV_ROLLER_PART_SELECTED, LV_STATE_DEFAULT, LV_COLOR_BLACK);
        lv_obj_set_style_local_text_color(roller, LV_ROLLER_PART_SELECTED, LV_STATE_DEFAULT, LV_COLOR_WHITE);
        lv_obj_set_style_local_border_width(roller, LV_ROLLER_PART_SELECTED, LV_STATE_DEFAULT, 2);
        lv_obj_set_style_local_border_color(roller, LV_ROLLER_PART_SELECTED, LV_STATE_DEFAULT, lv_color_make(0x00, 0x89, 0xff));

        lv_obj_align(roller, cont, LV_ALIGN_IN_LEFT_MID, i * (lv_obj_get_width(roller) + 2), 0);

        lv_obj_t* label = lv_label_create(cont, NULL);
        lv_obj_set_style_local_text_font(label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &font_microsoft_yahei_16);
        lv_label_set_text(label, roller_time_grp[i].text);
        lv_obj_align(label, roller, LV_ALIGN_OUT_TOP_MID, 0, 0);
        roller_time_grp[i].label = label;
        roller_time_grp[i].roller = roller;
    }

    cont_timeset = cont;

    cont_timeset_x_target = lv_obj_get_x(cont_timeset);
}

static void roller_dateset_create(lv_obj_t* par)
{
    lv_obj_t* cont = lv_cont_create(par, NULL);
    lv_obj_set_size(cont, 180, 150);
    lv_obj_align(cont, NULL, LV_ALIGN_CENTER, 240, -10);
    lv_obj_set_style_local_border_width(cont, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, 0);

    for (int i = 0; i < ARRAY_SIZE(roller_date_grp); i++) {

        lv_obj_t* roller = lv_roller_create(cont, NULL);

        char buf[200];
        strcpy(buf, roller_dateset_str);

        ROLLER_STR_SET_ENT(buf, roller_date_grp[i].pos);

        lv_roller_set_options(roller, buf, LV_ROLLER_MODE_INIFINITE);
        lv_roller_set_visible_row_count(roller, 2);

        lv_obj_set_style_local_text_font(roller, LV_ROLLER_PART_BG, LV_STATE_DEFAULT, &font_microsoft_yahei_28);
        lv_obj_set_style_local_border_width(roller, LV_ROLLER_PART_BG, LV_STATE_DEFAULT, 0);
        lv_obj_set_style_local_radius(roller, LV_ROLLER_PART_BG, LV_STATE_DEFAULT, 8);
        lv_obj_set_style_local_bg_color(roller, LV_ROLLER_PART_SELECTED, LV_STATE_DEFAULT, LV_COLOR_BLACK);
        lv_obj_set_style_local_text_color(roller, LV_ROLLER_PART_SELECTED, LV_STATE_DEFAULT, LV_COLOR_WHITE);
        lv_obj_set_style_local_border_width(roller, LV_ROLLER_PART_SELECTED, LV_STATE_DEFAULT, 2);
        lv_obj_set_style_local_border_color(roller, LV_ROLLER_PART_SELECTED, LV_STATE_DEFAULT, lv_color_make(0x00, 0x89, 0xff));

        lv_obj_align(roller, cont, LV_ALIGN_IN_LEFT_MID, i * (lv_obj_get_width(roller) + 2), 0);

        lv_obj_t* label = lv_label_create(cont, NULL);
        lv_obj_set_style_local_text_font(label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &font_microsoft_yahei_16);
        lv_label_set_text(label, roller_date_grp[i].text);
        lv_obj_align(label, roller, LV_ALIGN_OUT_TOP_MID, 0, 0);
        roller_date_grp[i].label = label;
        roller_date_grp[i].roller = roller;
    }
    cont_dateset = cont;

    cont_dateset_x_target = lv_obj_get_x(cont_dateset);
}

static void btn_confirm_create(lv_obj_t* par)
{
    lv_obj_t* btn = lv_btn_create(par, NULL);
    lv_obj_set_size(btn, 80, 45);
    lv_obj_set_style_local_bg_color(btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, lv_color_make(0x00, 0x89, 0xff));
    lv_obj_set_style_local_border_width(btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, 0);
    lv_obj_set_style_local_radius(btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, 8);
    lv_obj_align(btn, NULL, LV_ALIGN_IN_BOTTOM_MID, -41, -15);
    lv_obj_t* label = lv_label_create(btn, NULL);
    lv_obj_set_style_local_text_font(label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &font_microsoft_yahei_28);
    lv_label_set_text(label, "OK");
    lv_obj_set_style_local_text_decor(label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_TEXT_DECOR_NONE);

    lv_obj_set_event_cb(btn, btn_grp_event_handler);
    btn_confirm = btn;
}

static void btn_switch_create(lv_obj_t* par)
{
    lv_obj_t* btn = lv_btn_create(par, NULL);
    lv_obj_set_size(btn, 80, 45);
    lv_obj_set_style_local_bg_color(btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, lv_color_make(0x00, 0x89, 0xff));
    lv_obj_set_style_local_border_width(btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, 0);
    lv_obj_set_style_local_radius(btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, 8);
    lv_obj_align(btn, btn_confirm, LV_ALIGN_OUT_RIGHT_MID, 2, 0);
    lv_obj_t* label = lv_label_create(btn, NULL);
    lv_obj_set_style_local_text_font(label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &font_microsoft_yahei_28);
    lv_label_set_text(label, ">");
    lv_obj_set_style_local_text_decor(label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_TEXT_DECOR_NONE);

    lv_obj_set_event_cb(btn, btn_grp_event_handler);
    btn_switch = btn;
}

static void roller_grp_update(void)
{
    struct clock_value_s clock_value;
    clock_get_value(&clock_value);
    lv_roller_set_selected(roller_date_grp[0].roller, clock_value.year - 2001, LV_ANIM_OFF);
    lv_roller_set_selected(roller_date_grp[1].roller, clock_value.month - 1, LV_ANIM_OFF);
    lv_roller_set_selected(roller_date_grp[2].roller, clock_value.date - 1, LV_ANIM_OFF);
    lv_roller_set_selected(roller_time_grp[0].roller, clock_value.hour, LV_ANIM_OFF);
    lv_roller_set_selected(roller_time_grp[1].roller, clock_value.min, LV_ANIM_OFF);
    lv_roller_set_selected(roller_time_grp[2].roller, clock_value.sec, LV_ANIM_OFF);
}

static void page_play_anim(bool playback)
{
#define ANIM_Y_DEF(start_time, obj)                                                                                               \
    {                                                                                                                             \
        start_time, obj, LV_ANIM_EXEC(y), -lv_obj_get_height(obj), lv_obj_get_y(obj), LV_ANIM_TIME_DEFAULT, lv_anim_path_ease_out \
    }
#define ANIM_Y_REV_DEF(start_time, obj) { start_time, obj, LV_ANIM_EXEC(y), APP_WIN_HEIGHT + lv_obj_get_height(obj), lv_obj_get_y(obj), LV_ANIM_TIME_DEFAULT, lv_anim_path_ease_out }

    lv_anim_timeline_t anim_timeline[] = {
        ANIM_Y_REV_DEF(0, cont_timeset),
        ANIM_Y_REV_DEF(20, btn_confirm),
        ANIM_Y_REV_DEF(20, btn_switch),
    };

    uint32_t playtime = lv_anim_timeline_start(anim_timeline, ARRAY_SIZE(anim_timeline), playback);
    display_page_delay(playtime);
    status_bar_set_name(page_manager->get_current_name(page_manager));
    status_bar_set_enable(!playback);
}

static void setup(void)
{
    lv_obj_move_foreground(app_window);
    roller_timeset_create(app_window);
    roller_dateset_create(app_window);
    btn_confirm_create(app_window);
    btn_switch_create(app_window);
    roller_grp_update();
    page_play_anim(false);
}

static void quit(void)
{
    page_play_anim(true);
    lv_obj_clean(app_window);
}

static void event_handler(void* obj, uint8_t event)
{
    if (obj == lv_scr_act()) {
        if (event == LV_GESTURE_DIR_LEFT || event == LV_GESTURE_DIR_RIGHT) {
            page_manager->pop(page_manager);
        }
    }
}
