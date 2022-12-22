/****************************************************************************
 * apps/graphics/lvgl/lv_porting/lv_uv_interface.c
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

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <uv.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <lvgl/lvgl.h>
#include "lv_uv_interface.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Private Type Declarations
 ****************************************************************************/

struct lv_uv_obj_s
{
  int fd;
  uv_timer_t ui_timer;
  uv_poll_t disp_poll;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct lv_uv_obj_s g_uv_obj;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: ui_timer_cb
 ****************************************************************************/

static void ui_timer_cb(FAR uv_timer_t *handle)
{
  uint32_t sleep_ms;

  sleep_ms = lv_timer_handler();

  /* Prevent busy loops. */

  if (sleep_ms == 0)
    {
      sleep_ms = 1;
    }

  LV_LOG_TRACE("sleep_ms = %" LV_PRIu32, sleep_ms);
  uv_timer_start(handle, ui_timer_cb, sleep_ms, 0);
}

/****************************************************************************
 * Name: ui_disp_poll_cb
 ****************************************************************************/

static void ui_disp_poll_cb(FAR uv_poll_t *handle, int status, int events)
{
  LV_LOG_TRACE("disp refr");
  _lv_disp_refr_timer(NULL);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: lv_uv_start
 *
 * Description:
 *   Add the UI event loop to the uv_loop.
 *
 * Input Parameters:
 *   loop - Pointer to uv_loop.
 *
 ****************************************************************************/

void lv_uv_start(FAR void *loop)
{
  int fd;
  FAR uv_loop_t *ui_loop = loop;
  FAR lv_disp_t *disp;

  LV_LOG_INFO("dev: " CONFIG_LV_UV_POLL_DEVICEPATH "opening...");
  fd = open(CONFIG_LV_UV_POLL_DEVICEPATH, O_WRONLY);

  if (fd < 0)
    {
      LV_LOG_ERROR(CONFIG_LV_UV_POLL_DEVICEPATH " open failed: %d", errno);
      return;
    }

  disp = lv_disp_get_default();
  LV_ASSERT_NULL(disp);

  if (!disp->refr_timer)
    {
      LV_LOG_ERROR("disp->refr_timer is NULL");
      close(fd);
      return;
    }

  /* Remove default refr timer. */

  lv_timer_del(disp->refr_timer);
  disp->refr_timer = NULL;

  memset(&g_uv_obj, 0, sizeof(g_uv_obj));
  g_uv_obj.fd = fd;

  LV_LOG_INFO("init uv_timer...");
  uv_timer_init(ui_loop, &g_uv_obj.ui_timer);
  uv_timer_start(&g_uv_obj.ui_timer, ui_timer_cb, 1, 1);

  LV_LOG_INFO("init uv_poll...");
  uv_poll_init(ui_loop, &g_uv_obj.disp_poll, fd);
  uv_poll_start(&g_uv_obj.disp_poll, UV_WRITABLE, ui_disp_poll_cb);

  LV_LOG_INFO("lvgl loop start OK");
}

/****************************************************************************
 * Name: lv_uv_close
 ****************************************************************************/

void lv_uv_close(void)
{
  if (g_uv_obj.fd <= 0)
    {
      LV_LOG_WARN("lvgl loop has been closed");
      return;
    }

  uv_close((FAR uv_handle_t *)&g_uv_obj.ui_timer, NULL);
  uv_close((FAR uv_handle_t *)&g_uv_obj.disp_poll, NULL);
  close(g_uv_obj.fd);
  g_uv_obj.fd = -1;
  LV_LOG_INFO("lvgl loop close OK");
}
