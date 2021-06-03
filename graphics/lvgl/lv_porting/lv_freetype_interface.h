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

#include "lvgl/lvgl.h"
#include "ft2build.h"
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_CACHE_H
#include FT_SIZES_H

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* The cache management macro. 0 for close, other for open */

#define LV_USE_FT_CACHE_MANAGER  1

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* each freetype face can open many lvgl font, this type control face
 * information for use.
 */

typedef struct
{
  uint16_t cnt;       /* Using counter */
  char     *name;     /* name pointer */
} lv_face_info_t;

typedef struct
{
  uint16_t num;       /* Maximum number that can be opened */
  uint16_t cnt;       /* The number of opened */
  lv_ll_t  face_ll;   /* face list */
} lv_faces_control_t;

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

typedef struct
{
    FT_Face     face;   /* freetype face handle */
    FT_Size     size;   /* freetype size handle */
    lv_font_t   *font;  /* lvgl font handle */
    uint16_t    style;  /* font style */
    uint16_t    weight; /* font size */
} lv_font_fmt_freetype_dsc_t;

typedef lv_font_fmt_freetype_dsc_t lv_font_fmt_ft_dsc_t;

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

bool lv_freetype_init(FT_UInt max_faces,
                      FT_UInt max_sizes,
                      FT_ULong max_bytes);

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

#endif /* __LV_FREETYPE_INTERFACE_H */
