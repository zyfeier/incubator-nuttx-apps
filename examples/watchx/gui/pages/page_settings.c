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

PAGE_EXPORT(settings);

struct list_btn_s {
    const char* text;
    const uint8_t pageID;
    lv_obj_t* btn;
};

static struct list_btn_s btn_grp[] = {
    { "Timeset", page_id_timeset },
    { "Abaabaset", page_id_none },
    { "Wuhuset", page_id_none },
    { "Lalaset", page_id_none },
    { "Yueleset", page_id_none },
    { "Rioset", page_id_none },
    { "Wotuleset", page_id_none }
};

static lv_obj_t* page_settings;

static void btn_grp_event_handler(lv_obj_t* obj, lv_event_t event)
{
    if (event == LV_EVENT_CLICKED) {
        if (obj == btn_grp[0].btn) {
            page_manager->push(page_manager, page_id_timeset);
        }
    }
}

static void page_settings_create(lv_obj_t* par)
{
    lv_obj_t* page = lv_page_create(par, NULL);
    lv_obj_set_size(page, APP_WIN_WIDTH, APP_WIN_HEIGHT);
    lv_obj_align(page, NULL, LV_ALIGN_CENTER, 0, 0);
    lv_page_set_scrollbar_mode(page, LV_SCRLBAR_MODE_AUTO);
    lv_page_set_edge_flash(page, true);
    lv_obj_set_style_local_border_width(page, LV_PAGE_PART_BG, LV_STATE_DEFAULT, 0);

    for (int i = 0; i < ARRAY_SIZE(btn_grp); i++) {
        lv_obj_t* list_btn = lv_btn_create(page, NULL);
        lv_page_glue_obj(list_btn, true);
        lv_obj_set_size(list_btn, 200, 70);

        lv_obj_set_style_local_border_width(list_btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, 0);
        lv_obj_set_style_local_bg_color(list_btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, lv_color_make(0x03, 0x5d, 0xa6));
        lv_obj_set_style_local_radius(list_btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, 8);
        lv_obj_set_event_cb(list_btn, btn_grp_event_handler);
        lv_obj_t* label = lv_label_create(list_btn, NULL);
        lv_label_set_text(label, btn_grp[i].text);
        lv_obj_set_style_local_text_decor(label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_TEXT_DECOR_NONE);

        if (i == 0) {
            lv_obj_align(list_btn, page, LV_ALIGN_IN_TOP_MID, 0, 0);
        } else {
            lv_obj_align(list_btn, btn_grp[i - 1].btn, LV_ALIGN_OUT_BOTTOM_MID, 0, 2);
        }
        btn_grp[i].btn = list_btn;
    }
    page_settings = page;
}

static void page_play_anim(bool playback)
{
#define ANIM_X_DEF(start_time, obj)                                                                                               \
    {                                                                                                                             \
        start_time, obj, LV_ANIM_EXEC(y), -lv_obj_get_height(obj), lv_obj_get_y(obj), LV_ANIM_TIME_DEFAULT, lv_anim_path_ease_out \
    }
#define ANIM_X_REV_DEF(start_time, obj) { start_time, obj, LV_ANIM_EXEC(x), APP_WIN_HEIGHT + lv_obj_get_width(obj), lv_obj_get_x(obj), LV_ANIM_TIME_DEFAULT, lv_anim_path_ease_out }

    lv_anim_timeline_t anim_timeline[] = {
        ANIM_X_REV_DEF(0, page_settings),
    };

    uint32_t playtime = lv_anim_timeline_start(anim_timeline, ARRAY_SIZE(anim_timeline), playback);
    display_page_delay(playtime);
    status_bar_set_name(page_manager->get_current_name(page_manager));
    status_bar_set_enable(false);
}

static void setup(void)
{
    lv_obj_move_foreground(app_window);
    page_settings_create(app_window);
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
