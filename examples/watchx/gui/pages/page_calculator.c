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

PAGE_EXPORT(calculator);

static lv_obj_t* cont_btn_grp;
static lv_obj_t* textarea_clac;

static const lv_coord_t btn_height = 35;
static const lv_coord_t btn_width = 60;
#define BTN_COLOR_BLUE LV_COLOR_MAKE(0x65, 0xBA, 0xF7)
#define BTN_COLOR_YELLOW LV_COLOR_MAKE(0xFB, 0xB0, 0x3B)
#define BTN_COLOR_WHITE LV_COLOR_WHITE

struct btn_node_s {
    const char* text;
    const lv_color_t color;
};

static void btn_grp_event_handler(lv_obj_t* obj, lv_event_t event)
{
    const char* text = lv_obj_get_style_value_str(obj, LV_BTN_PART_MAIN);

    if (event == LV_EVENT_CLICKED || event == LV_EVENT_LONG_PRESSED_REPEAT) {
        if (strcmp(text, "C") == 0) {
            lv_textarea_set_text(textarea_clac, "");
        } else if (strcmp(text, "Del") == 0) {
            lv_textarea_del_char(textarea_clac);
        } else {
            lv_textarea_add_text(textarea_clac, text);
        }
    }
}

static void btn_grp_create(lv_obj_t* par)
{
    struct btn_node_s btn_grp[] = {
        { "C", BTN_COLOR_YELLOW },
        { "*", BTN_COLOR_BLUE },
        { "/", BTN_COLOR_BLUE },
        { "Del", BTN_COLOR_YELLOW },

        { "7", BTN_COLOR_WHITE },
        { "8", BTN_COLOR_WHITE },
        { "9", BTN_COLOR_WHITE },
        { "-", BTN_COLOR_BLUE },

        { "4", BTN_COLOR_WHITE },
        { "5", BTN_COLOR_WHITE },
        { "6", BTN_COLOR_WHITE },
        { "+", BTN_COLOR_BLUE },

        { "1", BTN_COLOR_WHITE },
        { "2", BTN_COLOR_WHITE },
        { "3", BTN_COLOR_WHITE },
        { "%", BTN_COLOR_BLUE },

        { "Ans", BTN_COLOR_BLUE },
        { "0", BTN_COLOR_WHITE },
        { ".", BTN_COLOR_WHITE },
        { "=", BTN_COLOR_YELLOW },
    };

    static lv_style_t style;
    lv_style_init(&style);
    lv_style_set_radius(&style, LV_STATE_DEFAULT, 5);
    lv_style_set_border_color(&style, LV_STATE_DEFAULT, LV_COLOR_BLACK);
    lv_style_set_border_width(&style, LV_STATE_DEFAULT, 2);
    lv_style_set_value_align(&style, LV_STATE_DEFAULT, LV_ALIGN_CENTER);
    lv_style_set_value_color(&style, LV_STATE_DEFAULT, LV_COLOR_BLACK);
    lv_style_set_value_font(&style, LV_STATE_DEFAULT, &font_microsoft_yahei_28);

    for (int i = 0; i < ARRAY_SIZE(btn_grp); i++) {
        lv_color_t color = btn_grp[i].color;
        const char* text = btn_grp[i].text;

        lv_obj_t* btn = lv_btn_create(par, NULL);
        lv_obj_set_size(btn, btn_width, btn_height);
        lv_obj_set_x(btn, (i % 4) * btn_width);
        lv_obj_set_y(btn, (i / 4) * btn_height);
        lv_obj_set_event_cb(btn, btn_grp_event_handler);

        lv_obj_add_style(btn, LV_BTN_PART_MAIN, &style);
        lv_obj_set_style_local_bg_color(btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, color);
        lv_obj_set_style_local_bg_color(btn, LV_BTN_PART_MAIN, LV_STATE_PRESSED, LV_COLOR_GRAY);
        lv_obj_set_style_local_value_str(btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, text);
    }
}

static void cont_btn_grp_create(lv_obj_t* par)
{
    lv_obj_t* cont = lv_cont_create(par, NULL);
    lv_obj_set_width(cont, APP_WIN_WIDTH);
    lv_obj_set_height(cont, btn_height * 5);
    lv_obj_align(cont, NULL, LV_ALIGN_IN_BOTTOM_MID, 0, 0);

    lv_obj_set_style_local_border_width(cont, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, 0);

    cont_btn_grp = cont;
}

static void textarea_calc_create(lv_obj_t* par)
{
    lv_obj_t* textarea = lv_textarea_create(par, NULL);
    lv_obj_set_width(textarea, APP_WIN_WIDTH);
    lv_obj_set_height(textarea, APP_WIN_HEIGHT - lv_obj_get_height(cont_btn_grp));
    lv_obj_align(textarea, NULL, LV_ALIGN_IN_TOP_MID, 0, 0);

    lv_obj_set_style_local_text_font(textarea, LV_TEXTAREA_PART_BG, LV_STATE_DEFAULT, &font_microsoft_yahei_28);
    lv_obj_set_style_local_border_width(textarea, LV_TEXTAREA_PART_BG, LV_STATE_DEFAULT, 0);

    lv_textarea_set_text(textarea, "");

    textarea_clac = textarea;
}

static void page_play_anim(bool playback)
{
    lv_anim_timeline_t anim_timeline[] = {
        { 0, cont_btn_grp, LV_ANIM_EXEC(y), APP_WIN_HEIGHT, lv_obj_get_y(cont_btn_grp), LV_ANIM_TIME_DEFAULT, lv_anim_path_ease_out },
        { 0, textarea_clac, LV_ANIM_EXEC(opa_scale), LV_OPA_TRANSP, LV_OPA_COVER, LV_ANIM_TIME_DEFAULT, lv_anim_path_ease_in_out },
    };

    if (playback) {
        lv_textarea_set_cursor_hidden(textarea_clac, true);
    }

    uint32_t playtime = lv_anim_timeline_start(anim_timeline, ARRAY_SIZE(anim_timeline), playback);
    display_page_delay(playtime);
}

static void setup(void)
{
    lv_obj_move_foreground(app_window);

    cont_btn_grp_create(app_window);
    btn_grp_create(cont_btn_grp);
    textarea_calc_create(app_window);
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
        if (event == LV_GESTURE_DIR_BOTTOM) {
            page_manager->pop(page_manager);
        }
    }
}
