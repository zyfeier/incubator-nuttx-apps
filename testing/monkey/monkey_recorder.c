/****************************************************************************
 * apps/testing/monkey/monkey_recorder.c
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

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "monkey_assert.h"
#include "monkey_log.h"
#include "monkey_port.h"
#include "monkey_recorder.h"
#include "monkey_utils.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MONKEY_REC_TOUCH_FMT "T%" PRIu32 ",X%d,Y%d,PR%d\n"
#define MONKEY_REC_BTN_FMT   "T%" PRIu32 ",BTN%" PRIu32 "\n"
#define MONKEY_REC_FILE_EXT  ".csv"
#define MONKEY_REC_FILE_HEAD "rec_"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: monkey_recorder_gen_file_path
 ****************************************************************************/

static void monkey_recorder_gen_file_path(FAR char *path_buf,
                                      size_t buf_size,
                                      FAR const char *dir_path,
                                      enum monkey_dev_type_e type)
{
  char localtime_str_buf[64];

  monkey_get_localtime_str(localtime_str_buf, sizeof(localtime_str_buf));

  snprintf(path_buf, buf_size,
           "%s/" MONKEY_REC_FILE_HEAD "%s_%s" MONKEY_REC_FILE_EXT,
           dir_path,
           monkey_dev_type2name(type),
           localtime_str_buf);
}

/****************************************************************************
 * Name: monkey_readline
 ****************************************************************************/

static int monkey_readline(int fd, FAR char *ptr, int maxlen)
{
  int n;
  int rc;
  char c;
  for (n = 1; n < maxlen; n++)
    {
      if ((rc = read(fd, &c, 1)) == 1)
        {
          *ptr++ = c;
          if (c == '\n')
            {
              break;
            }
        }
      else if (rc == 0)
        {
          if (n == 1)
            {
              /* EOF no data read */

              return 0;
            }
          else
            {
              /* EOF, some data read */

              break;
            }
        }
      else
        {
          /* error */

          return -1;
        }
    }

  *ptr = 0;
  return n;
}

/****************************************************************************
 * Name: monkey_recorder_write_timestamp
 ****************************************************************************/

static void monkey_recorder_write_timestamp(
                                  FAR struct monkey_recorder_s *recorder)
{
  union monkey_dev_state_u state;
  memset(&state, 0, sizeof(state));
  monkey_recorder_write(recorder, &state);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: monkey_recorder_create
 ****************************************************************************/

FAR struct monkey_recorder_s *monkey_recorder_create(FAR const char *path,
                                            enum monkey_dev_type_e type,
                                            enum monkey_recorder_mode_e mode)
{
  char file_path[PATH_MAX];
  FAR const char *path_ptr;
  int oflag;
  int fd;
  FAR struct monkey_recorder_s *recorder;

  MONKEY_ASSERT_NULL(path);

  recorder = malloc(sizeof(struct monkey_recorder_s));
  MONKEY_ASSERT_NULL(recorder);
  memset(recorder, 0, sizeof(struct monkey_recorder_s));

  if (mode == MONKEY_RECORDER_MODE_RECORD)
    {
      if (!monkey_dir_check(path))
        {
          goto failed;
        }

      monkey_recorder_gen_file_path(file_path, sizeof(file_path),
                                    path, type);
      path_ptr = file_path;
      oflag = O_WRONLY | O_CREAT;
    }
  else
    {
      path_ptr = path;
      oflag = O_RDONLY;
    }

  fd = open(path_ptr, oflag);
  if (fd < 0)
    {
      MONKEY_LOG_ERROR("open %s failed: %d", path_ptr, errno);
      goto failed;
    }

  recorder->type = type;
  recorder->fd = fd;
  recorder->mode = mode;

  MONKEY_LOG_NOTICE("open %s success, fd = %d, type = %s, mode = %d",
                    path_ptr, fd, monkey_dev_type2name(type), mode);

  if (mode == MONKEY_RECORDER_MODE_RECORD)
    {
      monkey_recorder_write_timestamp(recorder);
    }

  return recorder;

failed:
  if (fd > 0)
    {
      close(fd);
    }

  if (recorder)
    {
      free(recorder);
    }

  return NULL;
}

/****************************************************************************
 * Name: monkey_recorder_delete
 ****************************************************************************/

void monkey_recorder_delete(FAR struct monkey_recorder_s *recorder)
{
  MONKEY_ASSERT_NULL(recorder);
  if (recorder->fd > 0)
    {
      if (recorder->mode == MONKEY_RECORDER_MODE_RECORD)
        {
          monkey_recorder_write_timestamp(recorder);
        }

      MONKEY_LOG_NOTICE("close fd: %d", recorder->fd);
      close(recorder->fd);
      recorder->fd = -1;
    }

  free(recorder);
}

/****************************************************************************
 * Name: monkey_recorder_write
 ****************************************************************************/

enum monkey_recorder_res_e monkey_recorder_write(
                                  FAR struct monkey_recorder_s *recorder,
                                  FAR const union monkey_dev_state_u *state)
{
  MONKEY_ASSERT_NULL(recorder);
  MONKEY_ASSERT(recorder->mode == MONKEY_RECORDER_MODE_RECORD);
  char buffer[64];
  int len = -1;
  int ret;

  switch (MONKEY_GET_DEV_TYPE(recorder->type))
  {
    case MONKEY_DEV_TYPE_TOUCH:
      len = snprintf(buffer, sizeof(buffer), MONKEY_REC_TOUCH_FMT,
                     monkey_tick_get(),
                     state->touch.x,
                     state->touch.y,
                     state->touch.is_pressed);
      break;
    case MONKEY_DEV_TYPE_BUTTON:
      len = snprintf(buffer, sizeof(buffer), MONKEY_REC_BTN_FMT,
                     monkey_tick_get(),
                     state->button.value);
      break;
    default:
      return MONKEY_RECORDER_RES_DEV_TYPE_ERROR;
  }

  if (len <= 0)
    {
      return MONKEY_RECORDER_RES_PARSER_ERROR;
    }

  ret = write(recorder->fd, buffer, len);
  if (ret < 0)
    {
      return MONKEY_RECORDER_RES_WRITE_ERROR;
    }

  return MONKEY_RECORDER_RES_OK;
}

/****************************************************************************
 * Name: monkey_recorder_read
 ****************************************************************************/

enum monkey_recorder_res_e monkey_recorder_read(
                                      FAR struct monkey_recorder_s *recorder,
                                      FAR union monkey_dev_state_u *state,
                                      FAR uint32_t *time_stamp)
{
  char buffer[64];
  int converted;
  int ret;

  MONKEY_ASSERT_NULL(recorder);
  MONKEY_ASSERT(recorder->mode == MONKEY_RECORDER_MODE_PLAYBACK);

  ret = monkey_readline(recorder->fd, buffer, sizeof(buffer));

  if (ret < 0)
    {
      return MONKEY_RECORDER_RES_READ_ERROR;
    }

  if (ret == 0)
    {
      return MONKEY_RECORDER_RES_END_OF_FILE;
    }

  switch (MONKEY_GET_DEV_TYPE(recorder->type))
    {
    case MONKEY_DEV_TYPE_TOUCH:
      converted = sscanf(buffer,
                         MONKEY_REC_TOUCH_FMT,
                         time_stamp,
                         &state->touch.x,
                         &state->touch.y,
                         &state->touch.is_pressed);
      if (converted != 4)
        {
          return MONKEY_RECORDER_RES_PARSER_ERROR;
        }
      break;
    case MONKEY_DEV_TYPE_BUTTON:
      converted = sscanf(buffer,
                        MONKEY_REC_BTN_FMT,
                        time_stamp,
                        &state->button.value);
      if (converted != 2)
        {
          return MONKEY_RECORDER_RES_PARSER_ERROR;
        }
      break;
    default:
      return MONKEY_RECORDER_RES_DEV_TYPE_ERROR;
    }

  return MONKEY_RECORDER_RES_OK;
}

/****************************************************************************
 * Name: monkey_recorder_reset
 ****************************************************************************/

enum monkey_recorder_res_e monkey_recorder_reset(
                                      FAR struct monkey_recorder_s *recorder)
{
  MONKEY_ASSERT_NULL(recorder);
  lseek(recorder->fd, 0, SEEK_SET);
  return MONKEY_RECORDER_RES_OK;
}
