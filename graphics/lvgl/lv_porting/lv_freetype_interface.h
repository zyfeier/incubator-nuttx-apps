/****************************************************************************
 * apps/graphics/lvgl/lv_porting/lv_freetype_interface.h
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

#ifndef __LV_FREETYPE_INTERFACE_H
#define __LV_FREETYPE_INTERFACE_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include "lvgl/lvgl.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#if defined(CONFIG_LV_USE_FREETYPE_INTERFACE)

/* The cache management macro. 0 for close, other for open */

#ifndef LV_USE_FT_CACHE_MANAGER
#ifdef CONFIG_LV_USE_FT_CACHE_MANAGER
#define LV_USE_FT_CACHE_MANAGER  1
#else
#define LV_USE_FT_CACHE_MANAGER  0
#endif
#endif

/****************************************************************************
 * Public Types
 ****************************************************************************/

typedef enum
{
    FT_FONT_STYLE_NORMAL = 0,       /* normal style */
    FT_FONT_STYLE_ITALIC = 1 << 0,  /* italic */
    FT_FONT_STYLE_BOLD   = 1 << 1   /* bold */
} LV_FT_FONT_STYLE;

typedef struct
{
    const char *name;   /* The name of the font file */
    lv_font_t  *font;   /* point to lvgl font */
    uint16_t   weight;  /* font size */
    uint16_t   style;   /* font style */
} lv_ft_info_t;

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef __cplusplus
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

/****************************************************************************
 * Name: lv_freetype_init
 *
 * Description:
 *   init freetype library.
 *
 * Input Parameters:
 *   max_faces - Maximum number of opened FT_Face objects managed by this
 *               cache instance. Use 0 for defaults.
 *   max_sizes - Maximum number of opened FT_Size objects managed by this
 *               cache instance. Use 0 for defaults.
 *   max_bytes - Maximum number of bytes to use for cached data nodes.
 *               Use 0 for defaults. Note that this value does not account
 *               for managed FT_Face and FT_Size objects.
 *
 * Returned Value:
 *   true on success, otherwise false.
 *
 ****************************************************************************/

bool lv_freetype_init(uint16_t max_faces,
                      uint16_t max_sizes,
                      uint32_t max_bytes);

/****************************************************************************
 * Name: lv_freetype_destroy
 *
 * Description:
 *   destroy freetype library.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void lv_freetype_destroy(void);

/****************************************************************************
 * Name: lv_ft_font_init
 *
 * Description:
 *   Creates a font with info parameter specified.
 *
 * Input Parameters:
 *   info - See lv_ft_info_t for details.
 *          when success, lv_ft_info_t->font point to the font you created.
 *
 * Returned Value:
 *   true on success, otherwise false.
 *
 ****************************************************************************/

bool lv_ft_font_init(lv_ft_info_t *info);

/****************************************************************************
 * Name: lv_ft_font_destroy
 *
 * Description:
 *   Destroy a font that has been created.
 *
 * Input Parameters:
 *   font - pointer to font.
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void lv_ft_font_destroy(lv_font_t *font);

#undef EXTERN
#ifdef __cplusplus
}
#endif

#endif /* CONFIG_LV_USE_FREETYPE_INTERFACE */

#endif /* __LV_FREETYPE_INTERFACE_H */
