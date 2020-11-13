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

PAGE_EXPORT(main_menu);

static lv_obj_t* cont_apps;

LV_IMG_DECLARE(img_src_bluetooth);
LV_IMG_DECLARE(img_src_calculator);
LV_IMG_DECLARE(img_src_fileexplorer);
LV_IMG_DECLARE(img_src_game);
LV_IMG_DECLARE(img_src_heart_rate);
LV_IMG_DECLARE(img_src_music);
LV_IMG_DECLARE(img_src_settings);
LV_IMG_DECLARE(img_src_sleep);
LV_IMG_DECLARE(img_src_sport);
LV_IMG_DECLARE(img_src_stopwatch);
LV_IMG_DECLARE(img_src_app_shadow);

struct app_icon_s {
    const void* img_src;
    const char* name;
    const uint8_t page_id;
    const lv_color_t bg_color;
    lv_obj_t* cont;
};

#define APP_DEF(name, color)                                \
    {                                                       \
        &img_src_##name, #name, page_id_##name, color, NULL \
    }
#define APP_ICON_SIZE 80

static struct app_icon_s app_icon_grp[] = {
    APP_DEF(bluetooth, LV_COLOR_MAKE(0, 40, 255)),
    APP_DEF(calculator, LV_COLOR_MAKE(248, 119, 0)),
    APP_DEF(fileexplorer, LV_COLOR_MAKE(255, 184, 78)),
    APP_DEF(game, LV_COLOR_MAKE(15, 255, 186)),
    APP_DEF(heart_rate, LV_COLOR_MAKE(255, 84, 42)),
    APP_DEF(music, LV_COLOR_MAKE(238, 76, 132)),
    APP_DEF(settings, LV_COLOR_MAKE(0, 137, 255)),
    APP_DEF(sleep, LV_COLOR_MAKE(0, 27, 200)),
    APP_DEF(sport, LV_COLOR_MAKE(155, 93, 255)),
    APP_DEF(stopwatch, LV_COLOR_MAKE(60, 255, 167)),
};

static void app_click_anim(lv_obj_t* cont, lv_obj_t* img, bool ispress)
{
    LV_OBJ_ADD_ANIM(cont, width, ispress ? 70 : 80, 100);
    LV_OBJ_ADD_ANIM(cont, height, ispress ? 70 : 80, 100);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, img);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_img_set_zoom);
    lv_anim_set_values(&a, lv_img_get_zoom(img), ispress ? 190 : LV_IMG_ZOOM_NONE);
    lv_anim_set_time(&a, 100);

    lv_anim_path_t path;
    lv_anim_path_init(&path);
    lv_anim_path_set_cb(&path, lv_anim_path_ease_in_out);
    lv_anim_set_path(&a, &path);

    lv_anim_start(&a);
}

static uint8_t app_icon_get_page_id(lv_obj_t* obj)
{
    uint8_t page_id = page_id_none;
    for (int i = 0; i < ARRAY_SIZE(app_icon_grp); i++) {
        if (obj == app_icon_grp[i].cont) {
            page_id = app_icon_grp[i].page_id;
            break;
        }
    }
    return page_id;
}

static void app_icon_event_handler(lv_obj_t* obj, lv_event_t event)
{
    lv_obj_t* cont = lv_obj_get_child(obj, NULL);
    lv_obj_t* img = lv_obj_get_child(cont, NULL);
    if (event == LV_EVENT_PRESSED) {
        app_click_anim(cont, img, true);
    } else if (event == LV_EVENT_RELEASED || event == LV_EVENT_PRESS_LOST) {
        app_click_anim(cont, img, false);
    }

    if (event == LV_EVENT_CLICKED) {
        uint8_t page_id = app_icon_get_page_id(cont);

        if (page_id == page_id_none)
            return;

        if (page_id == page_id_calculator) {
            page_manager->push(page_manager, page_id_calculator);
        } else if (page_id == page_id_stopwatch) {
            page_manager->push(page_manager, page_id_stopwatch);
        } else if (page_id == page_id_heart_rate) {
            page_manager->push(page_manager, page_id_heart_rate);
        } else if (page_id == page_id_settings) {
            page_manager->push(page_manager, page_id_settings);
        }
    }
}

static void app_icon_create(lv_obj_t* par)
{
    static lv_style_t style;
    lv_style_init(&style);
    lv_style_set_radius(&style, LV_STATE_DEFAULT, 10);
    lv_style_set_border_width(&style, LV_STATE_DEFAULT, 0);
    lv_style_set_bg_color(&style, LV_STATE_PRESSED, LV_COLOR_GRAY);

    static lv_anim_path_t path;
    lv_anim_path_init(&path);
    lv_anim_path_set_cb(&path, lv_anim_path_ease_in_out);
    lv_style_set_transition_path(&style, LV_STATE_PRESSED, &path);
    lv_style_set_transition_time(&style, LV_STATE_PRESSED, 100);
    lv_style_set_transition_prop_1(&style, LV_STATE_PRESSED, LV_STYLE_BG_COLOR);

    for (int i = 0; i < ARRAY_SIZE(app_icon_grp); i++) {
        lv_obj_t* cont_vir = lv_cont_create(par, NULL);
        lv_obj_set_style_local_border_width(cont_vir, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, 0);
        lv_obj_set_size(cont_vir, APP_ICON_SIZE, APP_ICON_SIZE);
        lv_obj_set_drag_parent(cont_vir, true);
        lv_obj_set_event_cb(cont_vir, app_icon_event_handler);

        lv_coord_t interval_pixel_0 = (lv_obj_get_width(par) - lv_obj_get_width(cont_vir) * 2) / 3;
        lv_coord_t interval_pixel_1 = interval_pixel_0 + lv_obj_get_width(cont_vir);
        lv_coord_t interval_pixel_2 = lv_obj_get_width(cont_vir) / 2 + interval_pixel_0 / 2;
        lv_obj_align(
            cont_vir,
            NULL,
            LV_ALIGN_IN_TOP_MID,
            ((i % 2) == 0) ? -interval_pixel_2 : interval_pixel_2,
            interval_pixel_0 + (i / 2) * interval_pixel_1);

        lv_obj_t* cont = lv_cont_create(cont_vir, NULL);
        lv_obj_set_parent_event(cont, true);
        lv_obj_align(cont, NULL, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_auto_realign(cont, true);
        lv_obj_set_drag_parent(cont, true);
        lv_obj_add_style(cont, LV_CONT_PART_MAIN, &style);
        lv_obj_set_size(cont, APP_ICON_SIZE, APP_ICON_SIZE);
        lv_obj_set_style_local_bg_color(cont, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, app_icon_grp[i].bg_color);

        lv_obj_t* img = lv_img_create(cont, NULL);
        lv_img_set_src(img, app_icon_grp[i].img_src);
        lv_obj_align(img, NULL, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_auto_realign(img, true);

        app_icon_grp[i].cont = cont;
    }
}

static void img_app_shadow_create(lv_obj_t* par)
{
    lv_obj_t* img1 = lv_img_create(par, NULL);
    lv_img_set_src(img1, &img_src_app_shadow);
    lv_obj_align(img1, NULL, LV_ALIGN_IN_TOP_MID, 0, 0);

    lv_obj_t* img2 = lv_img_create(par, img1);
    lv_img_set_angle(img2, 1800);
    lv_obj_align(img2, NULL, LV_ALIGN_IN_BOTTOM_MID, 0, 0);
}

static void cont_apps_release_anim(lv_coord_t manual_target)
{
    lv_coord_t y_pos = lv_obj_get_y(cont_apps);
    lv_coord_t y_min = -(lv_obj_get_height(cont_apps) - APP_WIN_HEIGHT);
    lv_coord_t y_target = 0;

    if (!manual_target) {
        if (y_pos > 0) {
            y_target = 0;
        } else if (y_pos < y_min) {
            y_target = y_min;
        } else {
            return;
        }
    } else {
        y_target = manual_target;
    }

    LV_OBJ_ADD_ANIM(cont_apps, y, y_target, LV_ANIM_TIME_DEFAULT);
}

static void cont_apps_event_handler(lv_obj_t* obj, lv_event_t event)
{
    static bool is_draging = false;

    if (event == LV_EVENT_DRAG_BEGIN) {
        is_draging = true;
    } else if (event == LV_EVENT_DRAG_END) {
        is_draging = false;
    }

    if ((!is_draging && event == LV_EVENT_RELEASED)
        || event == LV_EVENT_PRESS_LOST
        || event == LV_EVENT_DRAG_END) {
        if (lv_anim_get(cont_apps, (lv_anim_exec_xcb_t)lv_obj_set_y) == NULL) {
            cont_apps_release_anim(0);
        }
    }
}

static void cont_apps_create(lv_obj_t* par)
{
    lv_obj_t* cont = lv_cont_create(par, NULL);

    lv_obj_set_width(cont, APP_WIN_WIDTH);

    lv_coord_t interval_pixel_h = (lv_obj_get_width(par) - APP_ICON_SIZE * 2) / 3;
    lv_obj_set_height(
        cont,
        (interval_pixel_h + APP_ICON_SIZE)
                * (ARRAY_SIZE(app_icon_grp) / 2)
            + interval_pixel_h);
    lv_obj_align(cont, NULL, LV_ALIGN_IN_TOP_MID, 0, 0);
    lv_obj_set_style_local_border_width(cont, LV_PAGE_PART_BG, LV_STATE_DEFAULT, 0);
    lv_obj_set_event_cb(cont, cont_apps_event_handler);
    lv_obj_set_drag(cont, true);
    lv_obj_set_drag_throw(cont, true);
    lv_obj_set_drag_dir(cont, LV_DRAG_DIR_VER);

    cont_apps = cont;
}

static void page_play_anim(bool open)
{
    static lv_coord_t contAppsPosY = 0;
    if (open) {
        lv_obj_set_y(cont_apps, APP_WIN_HEIGHT);

        lv_coord_t y_min = (-lv_obj_get_height(cont_apps) + APP_WIN_HEIGHT);
        if (contAppsPosY < y_min) {
            contAppsPosY = y_min;
        } else if (contAppsPosY > 0) {
            contAppsPosY = 0;
        }

        cont_apps_release_anim(contAppsPosY);
    } else {
        contAppsPosY = lv_obj_get_y(cont_apps);
        cont_apps_release_anim(APP_WIN_HEIGHT);
    }
}

static void setup(void)
{
    lv_obj_move_foreground(app_window);

    cont_apps_create(app_window);
    img_app_shadow_create(app_window);
    app_icon_create(cont_apps);
    page_play_anim(true);
}

static void quit(void)
{
    page_play_anim(false);
    display_page_delay(LV_ANIM_TIME_DEFAULT);
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
