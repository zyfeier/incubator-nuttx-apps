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
#ifndef __PAGE_MANAGER_H
#define __PAGE_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

typedef void (*page_manager_event_callback_t)(void*, uint8_t);

struct page_manager_node_s {
    page_manager_event_callback_t event_callback;
    const char* name;
};

enum page_manager_event {
    page_manager_event_none,
    page_manager_event_setup,
    page_manager_event_quit,
    page_manager_event_loop
};

struct page_manager_s {
    uint16_t now_page;
    uint16_t last_page;
    uint16_t next_page;

    uint16_t new_page;
    uint16_t old_page;

    struct page_manager_node_s* node_array;
    uint16_t node_array_size;

    uint16_t* stack;
    uint16_t stack_size;
    uint16_t stack_top;

    bool is_busy;

    bool (*register_page)(struct page_manager_s* pm, uint16_t page_id, page_manager_event_callback_t callback, const char* name);
    bool (*clear)(struct page_manager_s* pm, uint16_t page_id);
    bool (*push)(struct page_manager_s* pm, uint16_t page_id);
    uint16_t (*pop)(struct page_manager_s* pm);
    void (*change_to)(struct page_manager_s* pm, uint16_t page_id);
    void (*event_transmit)(struct page_manager_s* pm, void* obj, uint8_t event);
    void (*stack_clear)(struct page_manager_s* pm);
    const char* (*get_current_name)(struct page_manager_s* pm);
    void (*task_handler)(struct page_manager_s* pm);
};

void page_manager_init(
    struct page_manager_s* pm,
    struct page_manager_node_s* node_array_p,
    uint16_t node_array_len,
    uint16_t* stack_p,
    uint16_t stack_depth);

#ifdef __cplusplus
}
#endif

#endif
