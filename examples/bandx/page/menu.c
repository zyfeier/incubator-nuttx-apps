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
#include "page.h"

static lv_obj_t *screen = NULL;

LV_IMG_DECLARE(img_src_heart_rate);
LV_IMG_DECLARE(img_src_settings);
LV_IMG_DECLARE(img_src_sleep);
LV_IMG_DECLARE(img_src_sport);
LV_IMG_DECLARE(img_src_stop_watch);
LV_IMG_DECLARE(img_src_music);
LV_IMG_DECLARE(img_src_flashlight);
LV_IMG_DECLARE(img_src_icon_shadow_up);
LV_IMG_DECLARE(img_src_icon_shadow_down);

static lv_obj_t *page_app_icon;
static lv_group_t *group_app_icon;
static lv_task_t *task_auto_show;

typedef lv_obj_t *(*page_create_func_t)(void);

typedef struct
{
    const lv_img_dsc_t *img_src;
    const char *text;
    const lv_color_t bg_color;
    page_create_func_t page_create_func;
    lv_obj_t *obj;
} app_icon_t;

#define APP_DEF(name, text, color)                               \
    {                                                            \
        &img_src_##name, text, color, page_##name##_create, NULL \
    }
#define APP_ICON_SIZE 100
#define APP_ICON_ANIM_TIME 200

static app_icon_t app_icon_grp[] =
    {
        APP_DEF(heart_rate, "Heart", _LV_COLOR_MAKE(0xFF, 0x54, 0x2A)),
        APP_DEF(music, "Music", _LV_COLOR_MAKE(0xEE, 0x4C, 0x84)),
        APP_DEF(stop_watch, "StWatch", _LV_COLOR_MAKE(0x3C, 0xFF, 0xA7)),
        APP_DEF(flashlight, "Light", _LV_COLOR_MAKE(0xFF, 0xB4, 0x28)),
        APP_DEF(sleep, "Sleep", _LV_COLOR_MAKE(0x00, 0x1B, 0xC8)),
        //APP_DEF(sport,       "Sport",    _LV_COLOR_MAKE(0x9B, 0x5D, 0xFF)),
        APP_DEF(settings, "Settings", _LV_COLOR_MAKE(0x00, 0x89, 0xFF)),
};

static void app_icon_click_anim(lv_obj_t *img, bool ispress)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, img);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_img_set_zoom);
    lv_anim_set_values(
        &a,
        lv_img_get_zoom(img),
        ispress ? (int)(LV_IMG_ZOOM_NONE * 0.8f) : LV_IMG_ZOOM_NONE);
    lv_anim_set_time(&a, APP_ICON_ANIM_TIME);

    static lv_anim_path_t path_ease_in_out;
    lv_anim_path_init(&path_ease_in_out);
    lv_anim_path_set_cb(&path_ease_in_out, lv_anim_path_ease_in_out);

    static lv_anim_path_t path_overshoot;
    lv_anim_path_init(&path_overshoot);
    lv_anim_path_set_cb(&path_overshoot, lv_anim_path_overshoot);

    lv_anim_set_path(&a, ispress ? &path_ease_in_out : &path_overshoot);

    lv_anim_start(&a);
}

static void app_icon_event_handler(lv_obj_t *obj, lv_event_t event)
{
    lv_obj_t *cont = lv_obj_get_child(obj, NULL);
    lv_obj_t *img = lv_obj_get_child(cont, NULL);
    app_icon_t *app_icon = (app_icon_t *)lv_obj_get_user_data(obj);

    if (event == LV_EVENT_PRESSED)
    {
        app_icon_click_anim(img, true);
    }
    else if (event == LV_EVENT_RELEASED || event == LV_EVENT_PRESS_LOST)
    {
        app_icon_click_anim(img, false);
    }

    if (event == LV_EVENT_CLICKED)
    {
        if (page_get_autoshow_enable())
        {
            lv_task_del(task_auto_show);
        }

        page_create_func_t page_create_func = app_icon->page_create_func;

        if (page_create_func != NULL)
        {
            lv_obj_t *scr = page_create_func();
            lv_scr_load_anim(
                scr,
                LV_SCR_LOAD_ANIM_MOVE_LEFT,
                LV_ANIM_TIME_DEFAULT,
                APP_ICON_ANIM_TIME,
                false);
        }
    }
}

static void app_icon_create(lv_obj_t *par, app_icon_t *app_icon, int len)
{
    for (int i = 0; i < len; i++)
    {
        lv_obj_t *obj_vir = lv_cont_create(par, NULL);
        lv_obj_set_style_local_bg_color(
            obj_vir,
            LV_OBJ_PART_MAIN,
            LV_STATE_DEFAULT,
            LV_COLOR_BLACK);
        lv_obj_set_style_local_border_width(obj_vir, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 0);
        lv_obj_set_style_local_margin_ver(obj_vir, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 10);
        lv_obj_set_size(
            obj_vir,
            APP_ICON_SIZE + 20,
            APP_ICON_SIZE + lv_font_get_line_height(&font_erasbd_23));
        lv_obj_set_drag_parent(obj_vir, true);
        lv_obj_set_event_cb(obj_vir, app_icon_event_handler);
        lv_obj_set_user_data(obj_vir, &(app_icon[i]));

        lv_obj_t *label = lv_label_create(obj_vir, NULL);
        lv_obj_set_style_local_text_font(
            label,
            LV_LABEL_PART_MAIN,
            LV_STATE_DEFAULT,
            &font_erasbd_23);
        lv_label_set_text(label, app_icon[i].text);
        lv_obj_align(label, NULL, LV_ALIGN_IN_BOTTOM_MID, 0, 0);

        lv_obj_t *obj = lv_obj_create(obj_vir, NULL);
        lv_obj_set_parent_event(obj, true);
        lv_obj_align(obj, NULL, LV_ALIGN_IN_TOP_MID, 0, 0);
        lv_obj_set_auto_realign(obj, true);
        lv_obj_set_drag_parent(obj, true);
        lv_obj_set_size(obj, APP_ICON_SIZE, APP_ICON_SIZE);
        lv_obj_set_click_anim_default(obj);
        lv_obj_set_style_local_bg_color(
            obj,
            LV_OBJ_PART_MAIN,
            LV_STATE_DEFAULT,
            app_icon[i].bg_color);
        lv_obj_set_style_local_border_width(obj, LV_OBJ_PART_MAIN, LV_STATE_FOCUSED, 5);
        lv_obj_set_style_local_border_color(
            obj,
            LV_OBJ_PART_MAIN,
            LV_STATE_FOCUSED,
            LV_COLOR_WHITE);

        lv_obj_t *img = lv_img_create(obj, NULL);
        lv_img_set_src(img, app_icon[i].img_src);
        lv_obj_align(img, NULL, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_auto_realign(img, true);

        app_icon[i].obj = obj;
        lv_group_add_obj(group_app_icon, obj);
    }
}

static void img_app_shadow_create(lv_obj_t *par)
{
    lv_obj_t *img1 = lv_img_create(par, NULL);
    lv_img_set_src(img1, &img_src_icon_shadow_up);
    lv_obj_align(img1, NULL, LV_ALIGN_IN_TOP_MID, 0, 0);

    lv_obj_t *img2 = lv_img_create(par, NULL);
    lv_img_set_src(img2, &img_src_icon_shadow_down);
    lv_obj_align(img2, NULL, LV_ALIGN_IN_BOTTOM_MID, 0, 0);
}

static lv_obj_t *page_app_icon_create(lv_obj_t *par)
{
    lv_obj_t *page = lv_page_create(par, NULL);
    lv_obj_set_size(page, lv_obj_get_width(par), lv_obj_get_height(par));
    lv_obj_set_style_default(page);
    lv_page_set_edge_flash(page, true);
    lv_page_set_scrl_layout(page, LV_LAYOUT_COLUMN_MID);
    lv_page_set_scrlbar_mode(page, LV_SCRLBAR_MODE_OFF);

    return page;
}

static void page_event_handler(lv_obj_t *obj, lv_event_t event)
{
    if (event == LV_EVENT_GESTURE)
    {
        lv_gesture_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
        if (dir == LV_GESTURE_DIR_RIGHT)
        {
            lv_event_send(obj, LV_EVENT_LEAVE, NULL);
        }
    }
    else if (event == LV_EVENT_LEAVE)
    {
        if (page_get_autoshow_enable())
        {
            lv_task_del(task_auto_show);
        }

        lv_obj_t *scr = page_dialplate_create();
        lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_MOVE_RIGHT, LV_ANIM_TIME_DEFAULT, 0, false);
    }
    else if (event == LV_EVENT_DELETE)
    {
    }
}

static void auto_show_update(lv_task_t *task)
{
    static bool is_start_show_app_icon = true;
    extern bool page_last_screen_is_dialplate;

    enum state_type
    {
        IDLE,
        FOCUS,
        CLICK,
        NEXT,
        _END
    };
    static uint8_t state = IDLE;

    lv_obj_t *obj = lv_group_get_focused(group_app_icon);
    lv_obj_t *obj_vir = lv_obj_get_parent(obj);
    app_icon_t *app_icon = (app_icon_t *)lv_obj_get_user_data(obj_vir);

    if (page_last_screen_is_dialplate)
    {
        is_start_show_app_icon = true;
        page_last_screen_is_dialplate = false;
    }

    if (is_start_show_app_icon)
    {
        switch (state)
        {
        case IDLE:
            break;
        case FOCUS:
            lv_page_focus(page_app_icon, lv_obj_get_parent(obj), LV_ANIM_ON);
            break;
        case CLICK:
            lv_event_send(obj, LV_EVENT_CLICKED, NULL);
            break;
        case NEXT:
            lv_group_focus_next(group_app_icon);
            if (strcmp(app_icon->text, "Settings") == 0)
            {
                is_start_show_app_icon = false;
            }
            break;
        case _END:
            break;
        default:
            break;
        }
        state++;
        state %= _END;
    }
    else
    {
        lv_event_send(screen, LV_EVENT_LEAVE, NULL);
    }
}

lv_obj_t *page_menu_create(void)
{
    if (page_get_autoshow_enable())
    {
        task_auto_show = lv_task_create(auto_show_update, 500, LV_TASK_PRIO_MID, NULL);
    }

    if (screen != NULL)
    {
        return screen;
    }

    group_app_icon = lv_group_create();
    lv_obj_t *scr = page_screen_create();
    screen = scr;

    lv_obj_set_event_cb(scr, page_event_handler);

    page_app_icon = page_app_icon_create(scr);
    app_icon_create(page_app_icon, app_icon_grp, ARRAY_SIZE(app_icon_grp));

    return scr;
}
