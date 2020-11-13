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
#include "../display_private.h"

PAGE_EXPORT(stopwatch);

LV_IMG_DECLARE(img_src_stopwatch_add);
LV_IMG_DECLARE(img_src_stopwatch_pause);
LV_IMG_DECLARE(img_src_stopwatch_start);
LV_IMG_DECLARE(img_src_stopwatch_reset);

static lv_obj_t* label_time;
static lv_obj_t* list_history;

struct btn_node_s {
    const lv_img_dsc_t* img_src;
    const lv_color_t color;
    lv_obj_t* btn;
    lv_obj_t* img;
};

struct list_btn_s {
    lv_obj_t* list_btn;
    const char* text;
    const uint16_t btn_val;
};

enum btn_id {
    btn_id_reset,
    btn_id_start,
    btn_id_add
};

static struct btn_node_s btn_grp[] = {
    { &img_src_stopwatch_reset, LV_COLOR_MAKE(0x00, 0xFF, 0xD7) },
    { &img_src_stopwatch_start, LV_COLOR_MAKE(0x46, 0xF4, 0xB0) },
    { &img_src_stopwatch_add, LV_COLOR_MAKE(0x51, 0xFF, 0x9C) }
};

static const lv_coord_t btn_width = 70;
static const lv_coord_t btn_height = 45;

static lv_task_t* task_stopwatch = NULL;
static bool sw_is_pause = true;
static uint32_t sw_current_time = 0;
static uint32_t sw_interval_time = 0;
static uint32_t sw_last_time = 0;
static uint8_t sw_history_rec_cnt = 0;

static lv_coord_t label_time_y_end;
static lv_coord_t btn_y_end;
static lv_coord_t list_width_end;
static lv_coord_t list_height_end;

static void sw_show_time(uint32_t ms)
{
    uint16_t min = 0, sec = 0, msec = 0;
    min = ms / 60000;
    sec = (ms - (min * 60000)) / 1000;
    msec = ms - (sec * 1000) - (min * 60000);
    lv_label_set_text_fmt(label_time, "%02d:%02d.%03d", min, sec, msec);
}

static void sw_pause(void)
{
    if (sw_is_pause) {
        sw_interval_time = lv_tick_get() - sw_last_time;
        lv_task_set_prio(task_stopwatch, LV_TASK_PRIO_MID);
    } else {
        sw_last_time = sw_current_time;
        lv_task_set_prio(task_stopwatch, LV_TASK_PRIO_OFF);
    }
    sw_is_pause = !sw_is_pause;
}

static void sw_reset(void)
{
    sw_last_time = 0;
    sw_current_time = 0;
    sw_interval_time = 0;
    sw_is_pause = true;
    sw_history_rec_cnt = 0;
    lv_task_set_prio(task_stopwatch, LV_TASK_PRIO_OFF);
    sw_show_time(sw_current_time);
    lv_list_clean(list_history);
}

static void sw_update(lv_task_t* task)
{
    sw_current_time = lv_tick_get() - sw_interval_time;
    sw_show_time(sw_current_time);
}

static void btn_grp_event_handler(lv_obj_t* obj, lv_event_t event)
{

    if (event == LV_EVENT_CLICKED) {
        if (obj == btn_grp[btn_id_reset].btn) {
            if (sw_is_pause) {
                sw_reset();
                lv_obj_set_state(btn_grp[btn_id_reset].btn, LV_STATE_DISABLED);
                sw_history_rec_cnt = 0;
            }
        } else if (obj == btn_grp[btn_id_start].btn) {
            sw_pause();
            if (sw_is_pause) {
                lv_img_set_src(btn_grp[btn_id_start].img, &img_src_stopwatch_start);
                lv_obj_set_state(btn_grp[btn_id_reset].btn, LV_STATE_DEFAULT);
            } else {
                lv_img_set_src(btn_grp[btn_id_start].img, &img_src_stopwatch_pause);
                lv_obj_set_state(btn_grp[btn_id_reset].btn, LV_STATE_DISABLED);
            }
        } else if (obj == btn_grp[btn_id_add].btn) {
            lv_obj_t* list_btn;
            lv_obj_t* label;
            uint16_t min = 0, sec = 0, msec = 0;

            if (sw_history_rec_cnt < 10 && !sw_is_pause) {
                min = sw_current_time / 60000;
                sec = (sw_current_time - (min * 60000)) / 1000;
                msec = sw_current_time - (sec * 1000) - (min * 60000);

                char buf[64];
                lv_snprintf(buf, sizeof(buf), "#87FFCE %d. #%02d:%02d.%03d", sw_history_rec_cnt + 1, min, sec, msec);
                list_btn = lv_list_add_btn(list_history, NULL, buf);
                label = lv_obj_get_child(list_btn, NULL);
                lv_label_set_recolor(label, true);
                lv_list_focus(list_btn, LV_ANIM_ON);
                lv_obj_set_style_local_text_decor(list_btn, LV_BTN_PART_MAIN, LV_STATE_FOCUSED, LV_TEXT_DECOR_NONE);
                sw_history_rec_cnt++;
            }
        }
    }
}

static void label_time_create(lv_obj_t* par)
{
    lv_obj_t* label = lv_label_create(par, NULL);

    lv_obj_set_style_local_text_font(label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &font_microsoft_yahei_50);
    lv_obj_set_style_local_text_color(label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_MAKE(0x87, 0xFF, 0xCE));

    lv_label_set_text(label, "00:00.000");
    lv_obj_align(label, NULL, LV_ALIGN_IN_TOP_MID, 0, 0);
    //lv_obj_set_auto_realign(label,true);

    label_time = label;

    label_time_y_end = lv_obj_get_y(label_time);
}

static void list_history_create(lv_obj_t* par)
{
    lv_obj_t* list = lv_list_create(par, NULL);

    lv_obj_set_style_local_text_font(list, LV_LIST_PART_BG, LV_STATE_DEFAULT, &font_microsoft_yahei_28);
    lv_obj_set_style_local_border_width(list, LV_LIST_PART_BG, LV_STATE_DEFAULT, 2);
    lv_obj_set_style_local_radius(list, LV_LIST_PART_BG, LV_STATE_DEFAULT, 5);
    lv_obj_set_style_local_border_color(list, LV_LIST_PART_BG, LV_STATE_DEFAULT, LV_COLOR_MAKE(0x66, 0x66, 0x66));
    lv_obj_set_size(list, APP_WIN_WIDTH - 36, 105);
    lv_obj_align(list, NULL, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_auto_realign(list, true);
    list_history = list;

    list_width_end = lv_obj_get_width(list_history);
    list_height_end = lv_obj_get_height(list_history);
}

static void btn_grp_create(lv_obj_t* par)
{
    static lv_style_t style;
    lv_style_init(&style);
    lv_style_set_radius(&style, LV_STATE_DEFAULT, 5);
    lv_style_set_border_color(&style, LV_STATE_DEFAULT, LV_COLOR_BLACK);
    lv_style_set_border_width(&style, LV_STATE_DEFAULT, 2);

    lv_style_set_bg_color(&style, LV_STATE_PRESSED, LV_COLOR_GRAY);
    lv_style_set_bg_color(&style, LV_STATE_DISABLED, LV_COLOR_GRAY);

    for (int i = 0; i < ARRAY_SIZE(btn_grp); i++) {
        lv_color_t color = btn_grp[i].color;

        lv_obj_t* btn = lv_btn_create(par, NULL);
        lv_obj_set_size(btn, btn_width, btn_height);
        lv_obj_align(btn, NULL, LV_ALIGN_IN_BOTTOM_LEFT, 15 + (i * btn_width), -10);
        lv_obj_add_style(btn, LV_BTN_PART_MAIN, &style);
        lv_obj_set_event_cb(btn, btn_grp_event_handler);
        lv_obj_set_style_local_bg_color(btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, color);

        lv_obj_t* img = lv_img_create(btn, NULL);
        lv_img_set_src(img, btn_grp[i].img_src);
        lv_obj_align(img, btn, LV_ALIGN_CENTER, 0, 0);

        btn_grp[i].btn = btn;
        btn_grp[i].img = img;
    }

    lv_obj_set_state(btn_grp[btn_id_reset].btn, LV_STATE_DISABLED);
    btn_y_end = lv_obj_get_y(btn_grp[0].btn);
}

static void page_play_anim(bool playback)
{
    lv_coord_t btn_y_start = APP_WIN_HEIGHT + lv_obj_get_height(btn_grp[0].btn);

    lv_anim_timeline_t anim_timeline[] = {
        { 0, label_time, LV_ANIM_EXEC(y), -lv_obj_get_height(label_time), label_time_y_end, 300, lv_anim_path_ease_out },
        { 0, label_time, LV_ANIM_EXEC(opa_scale), LV_OPA_TRANSP, LV_OPA_COVER, 300, lv_anim_path_ease_out },

        { 100, btn_grp[0].btn, LV_ANIM_EXEC(y), btn_y_start, btn_y_end, 300, lv_anim_path_ease_out },
        { 150, btn_grp[1].btn, LV_ANIM_EXEC(y), btn_y_start, btn_y_end, 300, lv_anim_path_ease_out },
        { 200, btn_grp[2].btn, LV_ANIM_EXEC(y), btn_y_start, btn_y_end, 300, lv_anim_path_ease_out },

        { 100, list_history, LV_ANIM_EXEC(width), 0, list_width_end, 200, lv_anim_path_ease_out },
        { 300, list_history, LV_ANIM_EXEC(height), 2, list_height_end, 200, lv_anim_path_ease_out },
    };

    uint32_t playtime = lv_anim_timeline_start(anim_timeline, ARRAY_SIZE(anim_timeline), playback);
    display_page_delay(playtime);
}

static void setup_once(void)
{
    list_history_create(app_window);
    label_time_create(app_window);
    btn_grp_create(app_window);
}

static void setup(void)
{
    lv_obj_move_foreground(app_window);

    static bool is_init = false;
    if (!is_init) {
        setup_once();
        is_init = true;
    }

    task_stopwatch = lv_task_create(sw_update, 51, (sw_is_pause ? LV_TASK_PRIO_OFF : LV_TASK_PRIO_MID), NULL);
    lv_obj_set_hidden(app_window, false);
    page_play_anim(false);
}

static void quit(void)
{
    page_play_anim(true);
    lv_task_del(task_stopwatch);
    lv_obj_set_hidden(app_window, true);
}

static void event_handler(void* obj, uint8_t event)
{
    if (obj == lv_scr_act()) {
        if (event == LV_GESTURE_DIR_LEFT || event == LV_GESTURE_DIR_RIGHT) {
            page_manager->pop(page_manager);
        }
    }
}
