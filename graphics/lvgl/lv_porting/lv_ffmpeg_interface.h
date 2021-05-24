/****************************************************************************
 * graphics/lvgl/lv_porting/lv_ffmpeg_interface.h
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

#ifndef __LV_FFMPEG_INTERFACE_H__
#define __LV_FFMPEG_INTERFACE_H__

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#if defined(CONFIG_LV_USE_FFMPEG_INTERFACE)

/****************************************************************************
 * Type Definitions
 ****************************************************************************/

struct ffmpeg_context_s;

extern const lv_obj_class_t lv_ffmpeg_player_class;

typedef struct
  {
    lv_img_t img;
    lv_timer_t *timer;
    lv_img_dsc_t imgdsc;
    bool auto_restart;
    struct ffmpeg_context_s *ffmpeg_ctx;
  } lv_ffmpeg_player_t;

typedef enum
  {
    LV_FFMPEG_PLAYER_CMD_START = 1,
    LV_FFMPEG_PLAYER_CMD_STOP,
    LV_FFMPEG_PLAYER_CMD_PAUSE,
    LV_FFMPEG_PLAYER_CMD_RESUME,
    _LV_FFMPEG_PLAYER_CMD_LAST
  } lv_ffmpeg_player_cmd_t;

/****************************************************************************
 * Public Data
 ****************************************************************************/

#ifdef __cplusplus
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: lv_ffmpeg_interface_init
 *
 * Description:
 *  Register FFMPEG image decoder.
 *
 ****************************************************************************/

void lv_ffmpeg_interface_init(void);

/****************************************************************************
 * Name: lv_ffmpeg_player_create
 *
 * Description:
 *   Create ffmpeg_player object.
 *
 * Input Parameters:
 *   parent - pointer to an object, it will be the parent of the new player.
 *
 * Returned Value:
 *   pointer to the created ffmpeg_player.
 *
 ****************************************************************************/

lv_obj_t *lv_ffmpeg_player_create(lv_obj_t *parent);

/****************************************************************************
 * Name: lv_ffmpeg_player_set_src
 *
 * Description:
 *   Set the path of the file to be played.
 *
 * Input Parameters:
 *   ffmpeg_player - pointer to an ffmpeg_player.
 *   filename      - video file name.
 *
 * Returned Value:
 *   LV_RES_OK: no error; LV_RES_INV: can't get the info.
 *
 ****************************************************************************/

lv_res_t lv_ffmpeg_player_set_src(lv_obj_t *ffmpeg_player,
                                  const char *filename);

/****************************************************************************
 * Name: lv_ffmpeg_player_set_cmd
 *
 * Description:
 *   Send command control video player.
 *
 * Input Parameters:
 *   ffmpeg_player - pointer to an image.
 *   cmd           - control commands.
 *
 ****************************************************************************/

void lv_ffmpeg_player_set_cmd(lv_obj_t *ffmpeg_player,
                              lv_ffmpeg_player_cmd_t cmd);

/****************************************************************************
 * Name: lv_ffmpeg_player_set_auto_restart
 *
 * Description:
 *   Set the video to automatically replay.
 *
 * Input Parameters:
 *   ffmpeg_player - pointer to an ffmpeg_player.
 *   en            - true: enable the auto restart.
 *
 ****************************************************************************/

void lv_ffmpeg_player_set_auto_restart(lv_obj_t *ffmpeg_player, bool en);

#undef EXTERN
#ifdef __cplusplus
}
#endif

#endif /* CONFIG_LV_USE_FFMPEG_INTERFACE */

#endif /* __LV_FFMPEG_INTERFACE_H__ */
