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

PAGE_EXPORT(heart_rate);
#define ANIM_TIME 300

LV_IMG_DECLARE(img_src_heart_rate_tiny_heart);
LV_IMG_DECLARE(img_src_stopwatch_reset);

static const lv_coord_t btn_width = 70;
static const lv_coord_t btn_height = 45;

static lv_obj_t* label_current_hr;
static lv_obj_t* label_hr_unit;
static lv_obj_t* img_tiny_heart;

static lv_obj_t* label_range;

static lv_obj_t* btn_rst;

static lv_obj_t* chart_hr;
static lv_chart_series_t* ser_hr;

static void btn_event_handler(lv_obj_t* obj, lv_event_t event)
{
    if (event == LV_EVENT_CLICKED) {
        if (obj == btn_rst) {
        }
    }
}

static void label_current_hr_create(lv_obj_t* par)
{
    lv_obj_t* label1 = lv_label_create(par, NULL);
    lv_obj_set_style_local_text_font(label1, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &font_microsoft_yahei_50);
    lv_obj_set_style_local_text_color(label1, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_MAKE(0xFF, 0x54, 0x2A));
    lv_label_set_text(label1, "75");
    lv_label_set_align(label1, LV_LABEL_ALIGN_CENTER);
    lv_obj_align(label1, NULL, LV_ALIGN_IN_TOP_LEFT, 15, 20);

    lv_obj_t* img = lv_img_create(par, NULL);
    lv_img_set_src(img, &img_src_heart_rate_tiny_heart);
    lv_obj_align(img, label1, LV_ALIGN_OUT_RIGHT_MID, 5, -10);

    lv_obj_t* label2 = lv_label_create(par, NULL);
    lv_obj_set_style_local_text_font(label2, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &font_microsoft_yahei_28);
    lv_obj_set_style_local_text_color(label2, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);
    lv_label_set_text(label2, "bpm");
    lv_obj_align(label2, label1, LV_ALIGN_OUT_RIGHT_BOTTOM, 5, -5);

    label_current_hr = label1;
    label_hr_unit = label2;
    img_tiny_heart = img;
}

static void btn_rst_create(lv_obj_t* par)
{
    lv_obj_t* btn = lv_btn_create(par, NULL);
    lv_obj_set_size(btn, btn_width, btn_height);
    lv_obj_align(btn, NULL, LV_ALIGN_IN_BOTTOM_LEFT, 15, -10);
    lv_obj_set_event_cb(btn, btn_event_handler);
    lv_obj_set_style_local_bg_color(btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_MAKE(0xFF, 0x54, 0x2A));
    lv_obj_set_style_local_bg_color(btn, LV_BTN_PART_MAIN, LV_STATE_PRESSED, LV_COLOR_MAKE(0xB0, 0x31, 0x00));
    lv_obj_set_style_local_bg_color(btn, LV_BTN_PART_MAIN, LV_STATE_DISABLED, LV_COLOR_MAKE(0xB0, 0x31, 0x00));
    lv_obj_set_style_local_radius(btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, 8);
    lv_obj_set_style_local_border_color(btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);
    lv_obj_set_style_local_border_width(btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, 0);
    lv_obj_align(btn, label_current_hr, LV_ALIGN_OUT_RIGHT_MID, 80, 0);

    lv_obj_t* img = lv_img_create(btn, NULL);
    lv_img_set_src(img, &img_src_stopwatch_reset);
    lv_obj_align(img, btn, LV_ALIGN_CENTER, 0, 0);

    btn_rst = btn;
}

static void label_range_create(lv_obj_t* par)
{
    lv_obj_t* label = lv_label_create(par, NULL);
    lv_obj_set_style_local_text_font(label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &font_microsoft_yahei_28);
    lv_obj_set_style_local_text_color(label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);
    lv_label_set_text(label, "Range 50-120");
    lv_obj_align(label, label_current_hr, LV_ALIGN_OUT_BOTTOM_LEFT, 0, -10);

    label_range = label;
}

static void chart_hr_create(lv_obj_t* par)
{
    lv_obj_t* chart;
    chart = lv_chart_create(par, NULL);
    lv_obj_set_size(chart, 220, 100);
    lv_obj_align(chart, NULL, LV_ALIGN_IN_BOTTOM_MID, 0, -20);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_obj_set_style_local_bg_opa(chart, LV_CHART_PART_SERIES, LV_STATE_DEFAULT, LV_OPA_50);
    lv_obj_set_style_local_bg_grad_dir(chart, LV_CHART_PART_SERIES, LV_STATE_DEFAULT, LV_GRAD_DIR_VER);
    lv_obj_set_style_local_bg_main_stop(chart, LV_CHART_PART_SERIES, LV_STATE_DEFAULT, 255);
    lv_obj_set_style_local_bg_grad_stop(chart, LV_CHART_PART_SERIES, LV_STATE_DEFAULT, 0);
    lv_obj_set_style_local_border_width(chart, LV_CHART_PART_BG, LV_STATE_DEFAULT, 2);
    lv_obj_set_style_local_radius(chart, LV_CHART_PART_BG, LV_STATE_DEFAULT, 5);
    lv_obj_set_style_local_border_color(chart, LV_CHART_PART_BG, LV_STATE_DEFAULT, LV_COLOR_MAKE(0x66, 0x66, 0x66));

    lv_chart_set_div_line_count(chart, 0, 4);
    lv_obj_set_style_local_line_color(chart, LV_CHART_PART_SERIES_BG, LV_STATE_DEFAULT, LV_COLOR_MAKE(0x66, 0x66, 0x66));

    lv_chart_set_y_range(chart, LV_CHART_AXIS_PRIMARY_Y, 50, 130);

    lv_chart_series_t* ser1 = lv_chart_add_series(chart, LV_COLOR_MAKE(0xfc, 0x5c, 0x00));

    lv_chart_set_next(chart, ser1, 66);
    lv_chart_set_next(chart, ser1, 89);
    lv_chart_set_next(chart, ser1, 63);
    lv_chart_set_next(chart, ser1, 56);
    lv_chart_set_next(chart, ser1, 57);
    lv_chart_set_next(chart, ser1, 85);

    chart_hr = chart;
    ser_hr = ser1;
}

static void page_play_anim(bool playback)
{
#define ANIM_Y_DEF(start_time, obj)                                                                                               \
    {                                                                                                                             \
        start_time, obj, LV_ANIM_EXEC(y), -lv_obj_get_height(obj), lv_obj_get_y(obj), LV_ANIM_TIME_DEFAULT, lv_anim_path_ease_out \
    }
#define ANIM_Y_REV_DEF(start_time, obj) { start_time, obj, LV_ANIM_EXEC(y), APP_WIN_HEIGHT + lv_obj_get_height(obj), lv_obj_get_y(obj), LV_ANIM_TIME_DEFAULT, lv_anim_path_ease_out }
    lv_anim_timeline_t anim_timeline[] = {
        ANIM_Y_DEF(100, label_current_hr),
        ANIM_Y_DEF(100, label_hr_unit),
        ANIM_Y_DEF(100, img_tiny_heart),
        ANIM_Y_DEF(100, btn_rst),
        ANIM_Y_REV_DEF(100, label_range),
        ANIM_Y_REV_DEF(120, chart_hr),
    };

    uint32_t playtime = lv_anim_timeline_start(anim_timeline, ARRAY_SIZE(anim_timeline), playback);
    display_page_delay(playtime);
    status_bar_set_name(page_manager->get_current_name(page_manager));
    status_bar_set_enable(!playback);
}

static void setup(void)
{
    lv_obj_move_foreground(app_window);
    label_current_hr_create(app_window);
    btn_rst_create(app_window);
    label_range_create(app_window);
    chart_hr_create(app_window);
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
        if (event == LV_GESTURE_DIR_RIGHT) {
            page_manager->pop(page_manager);
        }
    }
}
