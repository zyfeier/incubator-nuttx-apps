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

PAGE_EXPORT(dialplate);

LV_IMG_DECLARE(img_src_heart);
LV_IMG_DECLARE(img_src_power);
LV_IMG_DECLARE(img_src_chn_second);

static lv_obj_t* cont_batt;
static lv_obj_t* led_batt_grp[10];

static lv_obj_t* cont_date;
static lv_obj_t* label_date;
static lv_obj_t* label_week;

static lv_obj_t* cont_time;
static lv_label_anim_effect_t label_time_effect[4];

static lv_obj_t* img_power;
static lv_obj_t* led_sec_grp[2];
static lv_task_t* task_update[2];

static lv_obj_t* cont_heart_rate;
static lv_obj_t* label_heart_rate;

static lv_obj_t* cont_steps;
static lv_obj_t* label_steps;

static lv_obj_t* img_chn;

static struct clock_value_s clock_value;

static void cont_batt_update_batt_usage(uint8_t usage)
{
    int8_t max_index_target = MAP(usage, 0, 100, 0, ARRAY_SIZE(led_batt_grp));

    for (int i = 0; i < ARRAY_SIZE(led_batt_grp); i++) {
        lv_obj_t* led = led_batt_grp[i];

        (i < max_index_target) ? lv_led_on(led) : lv_led_off(led);
    }
}

static void cont_batt_anim_callback(void* obj, int16_t usage)
{
    cont_batt_update_batt_usage(usage);
}

static void cont_batt_create(lv_obj_t* par)
{
    lv_obj_t* cont = lv_cont_create(par, NULL);
    lv_obj_set_size(cont, 222, 20);
    lv_obj_align(cont, NULL, LV_ALIGN_IN_TOP_MID, 0, 5);
    lv_obj_set_style_local_bg_color(cont, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);
    lv_obj_set_style_local_border_width(cont, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, 0);
    cont_batt = cont;

    lv_obj_t* img = lv_img_create(cont, NULL);
    lv_img_set_src(img, &img_src_power);
    lv_obj_align(img, NULL, LV_ALIGN_IN_LEFT_MID, 0, 0);
    img_power = img;

    const lv_coord_t led_w = (lv_obj_get_width(cont_batt) - lv_obj_get_width(img)) / 10 - 2;
    const lv_coord_t led_h = lv_obj_get_height(cont_batt);

    lv_obj_t* led = lv_led_create(cont_batt, NULL);
    lv_obj_set_size(led, led_w, led_h);
    lv_obj_set_style_local_radius(led, LV_LED_PART_MAIN, LV_STATE_DEFAULT, 0);
    lv_obj_set_style_local_border_width(led, LV_LED_PART_MAIN, LV_STATE_DEFAULT, 0);
    lv_obj_set_style_local_bg_color(led, LV_LED_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_RED);
    lv_obj_align(led, NULL, LV_ALIGN_IN_RIGHT_MID, 0, 0);
    led_batt_grp[0] = led;
    lv_led_off(led);

    for (int i = 1; i < ARRAY_SIZE(led_batt_grp); i++) {
        led = lv_led_create(cont_batt, led_batt_grp[0]);
        lv_obj_align(led, led_batt_grp[i - 1], LV_ALIGN_OUT_LEFT_MID, -2, 0);
        lv_led_off(led);
        led_batt_grp[i] = led;
    }
}

static void cont_week_create(lv_obj_t* par)
{
    lv_obj_t* cont = lv_cont_create(par, NULL);
    lv_obj_set_size(cont, lv_obj_get_width(par) / 2, lv_obj_get_height(par));
    lv_obj_align(cont, NULL, LV_ALIGN_IN_RIGHT_MID, 0, 0);
    lv_obj_set_style_local_radius(cont, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, 0);
    lv_obj_set_style_local_bg_color(cont, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);
    lv_obj_set_style_local_text_color(cont, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);

    lv_obj_t* label = lv_label_create(cont, NULL);
    lv_obj_set_style_local_text_font(label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &font_rexbold_28);
    lv_label_set_text(label, "SUN");
    lv_obj_align(label, NULL, LV_ALIGN_CENTER, 0, 0);
    label_week = label;
}

static void cont_date_create(lv_obj_t* par)
{
    lv_obj_t* cont = lv_cont_create(par, NULL);
    lv_obj_set_size(cont, 222, 32);
    lv_obj_align(cont, cont_batt, LV_ALIGN_OUT_BOTTOM_MID, 0, 5);
    lv_obj_set_style_local_radius(cont, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, 0);
    lv_obj_set_style_local_border_width(cont, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, 2);
    cont_date = cont;

    lv_obj_t* label = lv_label_create(cont, NULL);
    lv_obj_set_style_local_text_font(label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &font_rexbold_28);
    lv_label_set_text(label, "00.00.00");
    lv_obj_align(label, NULL, LV_ALIGN_IN_LEFT_MID, 10, 0);
    label_date = label;

    cont_week_create(cont_date);
}

static void label_date_update(void)
{
    lv_label_set_text_fmt(label_date, "%02d.%02d.%02d", clock_value.year % 100, clock_value.month, clock_value.date);

    static const char* week_str[] = { "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };
    lv_label_set_text(label_week, week_str[clock_value.week]);
}

static void label_time_update(lv_anim_enable_t anim_enable)
{
    clock_get_value(&clock_value);

    lv_label_anim_effect_check_value(&label_time_effect[3], clock_value.min % 10, anim_enable);
    lv_label_anim_effect_check_value(&label_time_effect[2], clock_value.min / 10, anim_enable);
    lv_label_anim_effect_check_value(&label_time_effect[1], clock_value.hour % 10, anim_enable);
    lv_label_anim_effect_check_value(&label_time_effect[0], clock_value.hour / 10, anim_enable);

    lv_led_toggle(led_sec_grp[0]);
    lv_led_toggle(led_sec_grp[1]);
}

static void label_time_create(lv_obj_t* par)
{
    const lv_coord_t x_mod[4] = { -70, -30, 30, 70 };

    for (int i = 0; i < ARRAY_SIZE(label_time_effect); i++) {
        lv_obj_t* label = lv_label_create(par, NULL);
        lv_label_set_text(label, "0");
        lv_obj_align(label, NULL, LV_ALIGN_CENTER, x_mod[i], 0);
        lv_obj_set_auto_realign(label, true);

        lv_label_anim_effect_init(&label_time_effect[i], par, label, 200);
    }

    lv_obj_t* led = lv_led_create(par, NULL);
    lv_obj_set_size(led, 8, 8);
    lv_obj_set_hidden(led, true);
    lv_obj_set_style_local_radius(led, LV_LED_PART_MAIN, LV_STATE_DEFAULT, 0);
    lv_obj_set_style_local_border_width(led, LV_LED_PART_MAIN, LV_STATE_DEFAULT, 0);
    lv_obj_set_style_local_bg_color(led, LV_LED_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_RED);
    lv_obj_align(led, NULL, LV_ALIGN_CENTER, 0, -15);
    led_sec_grp[0] = led;

    led = lv_led_create(par, led);
    lv_obj_align(led, NULL, LV_ALIGN_CENTER, 0, 15);
    led_sec_grp[1] = led;
}

static void cont_time_create(lv_obj_t* par)
{
    lv_obj_t* cont = lv_cont_create(par, NULL);

    lv_obj_set_size(cont, 222, 93);
    lv_obj_align(cont, cont_date, LV_ALIGN_OUT_BOTTOM_MID, 0, 5);

    lv_obj_set_style_local_radius(cont, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, 0);
    lv_obj_set_style_local_border_color(cont, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_RED);
    lv_obj_set_style_local_border_width(cont, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, 2);
    lv_obj_set_style_local_text_font(cont, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &font_rexbold_89);
    lv_obj_set_style_local_text_color(cont, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_RED);

    cont_time = cont;

    label_time_create(cont_time);
}

static void label_heart_rate_update(void)
{
    lv_label_set_text_fmt(label_heart_rate, "%04.01f", particle_sensor_get_beats());
}

static void cont_heart_rate_create(lv_obj_t* par)
{
    lv_obj_t* cont = lv_cont_create(par, NULL);
    lv_obj_set_size(cont, 150, 32);
    lv_obj_align(cont, cont_time, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 5);
    cont_heart_rate = cont;

    cont = lv_cont_create(cont_heart_rate, NULL);
    lv_obj_set_size(cont, lv_obj_get_width(cont_heart_rate) / 3, lv_obj_get_height(cont_heart_rate));
    lv_obj_align(cont, NULL, LV_ALIGN_IN_LEFT_MID, 0, 0);
    lv_obj_set_style_local_bg_color(cont, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);
    lv_obj_set_style_local_text_color(cont, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);

    lv_obj_t* label = lv_label_create(cont, NULL);
    lv_obj_set_style_local_text_font(label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &font_rexbold_28);
    lv_label_set_text(label, "HRT");
    lv_obj_align(label, NULL, LV_ALIGN_CENTER, 0, 0);

    label = lv_label_create(cont_heart_rate, NULL);
    lv_obj_set_style_local_text_font(label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &font_rexbold_28);
    lv_label_set_text(label, "00.0");
    lv_obj_align(label, cont, LV_ALIGN_OUT_RIGHT_MID, 10, 0);
    label_heart_rate = label;

    cont = lv_cont_create(cont_heart_rate, NULL);
    lv_obj_set_size(cont, lv_obj_get_width(cont_heart_rate) / 4, lv_obj_get_height(cont_heart_rate));
    lv_obj_align(cont, NULL, LV_ALIGN_IN_RIGHT_MID, 0, 0);
    lv_obj_set_style_local_bg_color(cont, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);

    lv_obj_t* img = lv_img_create(cont, NULL);
    lv_img_set_src(img, &img_src_heart);
    lv_obj_align(img, NULL, LV_ALIGN_CENTER, 0, 0);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, img);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_img_set_zoom);
    lv_anim_set_values(&a, LV_IMG_ZOOM_NONE, 160);
    lv_anim_set_time(&a, 300);
    lv_anim_set_playback_time(&a, 300);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_repeat_delay(&a, 800);

    lv_anim_path_t path;
    lv_anim_path_init(&path);
    lv_anim_path_set_cb(&path, lv_anim_path_ease_in_out);
    lv_anim_set_path(&a, &path);

    lv_anim_start(&a);
}

static void label_steps_update(void)
{
    lv_label_set_text_fmt(label_steps, "%05d", imu_get_steps());
}

static void cont_steps_create(lv_obj_t* par)
{
    lv_obj_t* cont = lv_cont_create(par, NULL);
    lv_obj_set_size(cont, 150, 32);
    lv_obj_align(cont, cont_heart_rate, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 5);
    cont_steps = cont;

    cont = lv_cont_create(cont_steps, NULL);
    lv_obj_set_size(cont, lv_obj_get_width(cont_steps) / 3, lv_obj_get_height(cont_steps));
    lv_obj_align(cont, NULL, LV_ALIGN_IN_LEFT_MID, 0, 0);
    lv_obj_set_style_local_bg_color(cont, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);
    lv_obj_set_style_local_text_color(cont, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);

    lv_obj_t* label = lv_label_create(cont, NULL);
    lv_obj_set_style_local_text_font(label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &font_rexbold_28);
    lv_label_set_text(label, "STP");
    lv_obj_align(label, NULL, LV_ALIGN_CENTER, 0, 0);

    label = lv_label_create(cont_steps, NULL);
    lv_obj_set_style_local_text_font(label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &font_rexbold_28);
    lv_label_set_text(label, "00000");
    lv_obj_align(label, cont, LV_ALIGN_OUT_RIGHT_MID, 10, 0);
    label_steps = label;
}

static void img_chn_create(lv_obj_t* par)
{
    lv_obj_t* img = lv_img_create(par, NULL);
    lv_img_set_src(img, &img_src_chn_second);
    lv_obj_align(img, cont_time, LV_ALIGN_OUT_BOTTOM_RIGHT, 0, 5);
    img_chn = img;
}

static void page_play_anim(bool playback)
{
#define ANIM_WIDTH_DEF(start_time, obj)                                                                             \
    {                                                                                                               \
        start_time, obj, LV_ANIM_EXEC(width), 0, lv_obj_get_width(obj), LV_ANIM_TIME_DEFAULT, lv_anim_path_ease_out \
    }
#define ANIM_OPA_DEF(start_time, obj) { start_time, obj, LV_ANIM_EXEC(opa_scale), LV_OPA_TRANSP, LV_OPA_COVER, 500, lv_anim_path_bounce }

    lv_anim_timeline_t anim_timeline[] = {
        ANIM_WIDTH_DEF(0, cont_batt),
        ANIM_WIDTH_DEF(100, cont_date),
        ANIM_WIDTH_DEF(200, cont_time),
        ANIM_WIDTH_DEF(300, cont_heart_rate),
        ANIM_WIDTH_DEF(400, cont_steps),

        { 400, NULL, (lv_anim_exec_xcb_t)cont_batt_anim_callback, 0, power_get_battery_usage(), 400, lv_anim_path_linear },

        ANIM_OPA_DEF(800, label_time_effect[0].label_1),
        ANIM_OPA_DEF(800, label_time_effect[1].label_1),
        ANIM_OPA_DEF(800, label_time_effect[2].label_1),
        ANIM_OPA_DEF(800, label_time_effect[3].label_1),
        ANIM_OPA_DEF(800, label_time_effect[0].label_2),
        ANIM_OPA_DEF(800, label_time_effect[1].label_2),
        ANIM_OPA_DEF(800, label_time_effect[2].label_2),
        ANIM_OPA_DEF(800, label_time_effect[3].label_2),
        ANIM_OPA_DEF(800, label_date),
        ANIM_OPA_DEF(800, label_heart_rate),
        ANIM_OPA_DEF(800, label_steps),
        ANIM_OPA_DEF(800, img_chn),
    };

    uint32_t playtime = lv_anim_timeline_start(anim_timeline, ARRAY_SIZE(anim_timeline), playback);
    display_page_delay(playtime);
}

static void task_1000ms_update(lv_task_t* task)
{
    label_date_update();
    label_steps_update();
    label_heart_rate_update();

    if (power_get_battery_is_charging()) {
        lv_obj_set_hidden(img_power, !lv_obj_get_hidden(img_power));
    } else {
        lv_obj_set_hidden(img_power, false);
    }
}

static void task_500ms_update(lv_task_t* task)
{
    label_time_update(LV_ANIM_ON);
    cont_batt_update_batt_usage(power_get_battery_usage());
}

static void tasks_create(void)
{
    task_update[0] = lv_task_create(task_500ms_update, 500, LV_TASK_PRIO_MID, NULL);
    task_500ms_update(task_update[0]);

    task_update[1] = lv_task_create(task_1000ms_update, 1000, LV_TASK_PRIO_MID, NULL);
    task_1000ms_update(task_update[1]);
}

static void setup(void)
{
    lv_obj_move_foreground(app_window);

    cont_batt_create(app_window);
    cont_date_create(app_window);
    cont_time_create(app_window);

    cont_heart_rate_create(app_window);

    cont_steps_create(app_window);

    img_chn_create(app_window);

    label_time_update(LV_ANIM_OFF);
    label_date_update();
    label_steps_update();

    page_play_anim(false);

    lv_obj_set_hidden(led_sec_grp[0], false);
    lv_obj_set_hidden(led_sec_grp[1], false);

    tasks_create();
}

static void quit(void)
{
    lv_obj_set_hidden(led_sec_grp[0], true);
    lv_obj_set_hidden(led_sec_grp[1], true);

    lv_task_del(task_update[0]);
    lv_task_del(task_update[1]);

    page_play_anim(true);

    lv_obj_clean(app_window);
}

static void event_handler(void* obj, uint8_t event)
{
    if (obj == lv_scr_act()) {
        if (event == LV_GESTURE_DIR_LEFT) {
            page_manager->push(page_manager, page_id_main_menu);
        }
    }
}
