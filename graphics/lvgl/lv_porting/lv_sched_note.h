/****************************************************************************
 * apps/graphics/lvgl/lv_porting/lv_sched_note.h
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

#ifndef __LV_SCHED_NOTE_H__
#define __LV_SCHED_NOTE_H__

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/sched_note.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#if defined(CONFIG_LV_USE_SCHED_NOTE)
#define LV_NOTE_PRINTF(format, ...)  sched_note_printf(format, ##__VA_ARGS__)
#define LV_NOTE_BEGIN()              sched_note_begin(NOTE_TAG_ALWAYS)
#define LV_NOTE_END()                sched_note_end(NOTE_TAG_ALWAYS)
#define LV_NOTE_BEGIN_STR(str)       sched_note_beginex(NOTE_TAG_ALWAYS, str)
#define LV_NOTE_END_STR(str)         sched_note_endex(NOTE_TAG_ALWAYS, str)
#define LV_NOTE_BEGIN_LOCAL(str)          \
    do {                                  \
        const char *note_temp_str = str;  \
        (void)note_temp_str;              \
        LV_NOTE_BEGIN_STR(note_temp_str)
#define LV_NOTE_END_LOCAL()               \
        LV_NOTE_END_STR(note_temp_str);   \
    } while(0)
#else
#define LV_NOTE_PRINTF(format, ...)
#define LV_NOTE_BEGIN()
#define LV_NOTE_END()
#define LV_NOTE_BEGIN_STR(str)
#define LV_NOTE_END_STR(str)
#define LV_NOTE_BEGIN_LOCAL(str)
#define LV_NOTE_END_LOCAL()
#endif /* CONFIG_LV_USE_SCHED_NOTE */

#endif /* __LV_SCHED_NOTE_H__ */
