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
#include "display_private.h"
#include <unistd.h>

#define PAGE_IMPORT(name)                                                             \
    do {                                                                              \
        extern void page_register_##name(struct page_manager_s* pm, uint8_t page_id); \
        page_register_##name(&page_manager, page_id_##name);                          \
    } while (0)

static struct page_manager_s page_manager;

static void display_page_gestute_event_callback(lv_obj_t* obj, lv_event_t event)
{
    if (event == LV_EVENT_GESTURE) {
        lv_gesture_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
        page_manager.event_transmit(&page_manager, obj, dir);
    }
}

void display_page_init(void)
{
    static struct page_manager_node_s page_node_grp[page_id_max];
    static uint16_t page_id_stack[16];
    page_manager_init(
        &page_manager,
        page_node_grp,
        ARRAY_SIZE(page_node_grp),
        page_id_stack,
        ARRAY_SIZE(page_id_stack));

    PAGE_IMPORT(main_menu);
    PAGE_IMPORT(dialplate);
    PAGE_IMPORT(calculator);
    PAGE_IMPORT(stopwatch);
    PAGE_IMPORT(heart_rate);
    PAGE_IMPORT(settings);
    PAGE_IMPORT(timeset);
    page_manager.push(&page_manager, page_id_dialplate); //Enter first page

    lv_obj_set_event_cb(lv_scr_act(), display_page_gestute_event_callback);
}

void display_update(void)
{
    page_manager.task_handler(&page_manager);
    lv_task_handler();
}

void display_page_delay(uint32_t ms)
{
    uint32_t last_time = lv_tick_get();

    while (lv_tick_elaps(last_time) <= ms) {
        lv_task_handler();
        usleep(1000);
    }
}
