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
#include "page_manager.h"

#define IS_PAGE(id) ((id) < (pm->node_array_size))

#ifndef NULL
#define NULL 0
#endif

static bool page_manager_register_page(struct page_manager_s* pm, uint16_t page_id, page_manager_event_callback_t callback, const char* name);
static bool page_manager_clear(struct page_manager_s* pm, uint16_t page_id);
static bool page_manager_push(struct page_manager_s* pm, uint16_t page_id);
static uint16_t page_manager_pop(struct page_manager_s* pm);
static void page_manager_change_to(struct page_manager_s* pm, uint16_t page_id);
static void page_manager_event_transmit(struct page_manager_s* pm, void* obj, uint8_t event);
static void page_manager_stack_clear(struct page_manager_s* pm);
static const char* page_manager_get_current_name(struct page_manager_s* pm);
static void page_manager_task_handler(struct page_manager_s* pm);

void page_manager_init(
    struct page_manager_s* pm,
    struct page_manager_node_s* node_array_p,
    uint16_t node_array_len,
    uint16_t* stack_p,
    uint16_t stack_depth)
{
    pm->node_array = node_array_p;
    pm->node_array_size = node_array_len;
    pm->stack = stack_p;
    pm->stack_size = stack_depth;

    pm->now_page = 0;
    pm->last_page = 0;
    pm->next_page = 0;

    pm->new_page = 0;
    pm->old_page = 0;
    pm->is_busy = false;

    pm->register_page = page_manager_register_page;
    pm->clear = page_manager_clear;
    pm->push = page_manager_push;
    pm->pop = page_manager_pop;
    pm->change_to = page_manager_change_to;
    pm->event_transmit = page_manager_event_transmit;
    pm->stack_clear = page_manager_stack_clear;
    pm->get_current_name = page_manager_get_current_name;
    pm->task_handler = page_manager_task_handler;

    /* Clear array */
    for (uint16_t id = 0; id < node_array_len; id++) {
        page_manager_clear(pm, id);
    }
    page_manager_stack_clear(pm);
}

static bool page_manager_register_page(
    struct page_manager_s* pm,
    uint16_t page_id,
    page_manager_event_callback_t callback,
    const char* name)
{
    if (!IS_PAGE(page_id))
        return false;

    pm->node_array[page_id].event_callback = callback;
    pm->node_array[page_id].name = name;
    return true;
}

static bool page_manager_clear(struct page_manager_s* pm, uint16_t page_id)
{
    if (!IS_PAGE(page_id))
        return false;

    pm->node_array[page_id].event_callback = NULL;
    pm->node_array[page_id].name = NULL;
    return true;
}

static bool page_manager_push(struct page_manager_s* pm, uint16_t page_id)
{
    if (!IS_PAGE(page_id))
        return false;

    /* Check if the page is busy */
    if (pm->is_busy)
        return false;

    /* Prevent stack overflow */
    if (pm->stack_top >= pm->stack_size - 1)
        return false;

    /* Prevent duplicate pages from being pushed onto the stack */
    if (page_id == pm->stack[pm->stack_top])
        return false;

    /* Move the top pointer up */
    pm->stack_top++;

    /* Page push */
    pm->stack[pm->stack_top] = page_id;

    /* Page jump */
    page_manager_change_to(pm, pm->stack[pm->stack_top]);

    return true;
}

static uint16_t page_manager_pop(struct page_manager_s* pm)
{
    /* Check if the page is busy */
    if (pm->is_busy)
        return 0;

    /* Prevent stack overflow */
    if (pm->stack_top == 0)
        return 0;

    /* Clear current page */
    pm->stack[pm->stack_top] = 0;

    /* Pop the stack, move the top pointer down */
    pm->stack_top--;

    uint16_t page_id = pm->stack[pm->stack_top];

    /* Page jump */
    page_manager_change_to(pm, page_id);

    return page_id;
}

static void page_manager_change_to(struct page_manager_s* pm, uint16_t page_id)
{
    if (!IS_PAGE(page_id))
        return;

    /* Check if the page is busy */
    if (!pm->is_busy) {
        /*New page ID*/
        pm->next_page = pm->new_page = page_id;

        /*Mark as busy*/
        pm->is_busy = true;
    }
}

static void page_manager_event_transmit(struct page_manager_s* pm, void* obj, uint8_t event)
{
    /* Pass the event to the current page */
    if (pm->node_array[pm->now_page].event_callback != NULL) {
        pm->node_array[pm->now_page].event_callback(obj, event);
    }
}

static void page_manager_stack_clear(struct page_manager_s* pm)
{
    /* Check if the page is busy */
    if (pm->is_busy)
        return;

    /* Clear the data in the stack */
    for (uint8_t i = 0; i < pm->stack_size; i++) {
        pm->stack[i] = 0;
    }
    /* Stack top pointer reset */
    pm->stack_top = 0;
}

static const char* page_manager_get_current_name(struct page_manager_s* pm)
{
    return pm->node_array[pm->now_page].name;
}

static void page_manager_task_handler(struct page_manager_s* pm)
{
    /* Page switching event */
    if (pm->new_page != pm->old_page) {
        /* Mark as busy */
        pm->is_busy = true;

        /* Trigger the old page exit event */
        if (pm->node_array[pm->old_page].event_callback != NULL) {
            pm->node_array[pm->old_page].event_callback(pm, page_manager_event_quit);
        }

        /* Mark old pages */
        pm->last_page = pm->old_page;

        /* Mark the new page as the current page */
        pm->now_page = pm->new_page;

        /* Trigger a new page initialization event */
        if (pm->node_array[pm->new_page].event_callback != NULL) {
            pm->node_array[pm->new_page].event_callback(pm, page_manager_event_setup);
        }

        /* The new page is initialized and marked as the old page */
        pm->old_page = pm->new_page;

        /* The marked page is not busy and in a loop */
        pm->is_busy = false;
    }

    /* Page loop event */
    if (pm->node_array[pm->now_page].event_callback != NULL) {
        pm->node_array[pm->now_page].event_callback(pm, page_manager_event_loop);
    }
}
