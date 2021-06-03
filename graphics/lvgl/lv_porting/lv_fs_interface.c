/****************************************************************************
 * apps/graphics/lvgl/lv_porting/lv_fs_interface.c
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

#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <unistd.h>
#include <stdio.h>
#include "lv_fs_interface.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define LV_FS_LETTER '/'

/****************************************************************************
 * Private Type Declarations
 ****************************************************************************/

/* Create a type to store the required data about your file. */

typedef int file_t;

/* Similarly to `file_t` create a type for directory reading too */

typedef DIR *dir_t;

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static void *fs_open(lv_fs_drv_t *drv, const char *path, lv_fs_mode_t mode);
static lv_fs_res_t fs_close(lv_fs_drv_t *drv, void *file_p);
static lv_fs_res_t fs_read(lv_fs_drv_t *drv, void *file_p,
                           void *buf, uint32_t btr, uint32_t *br);
static lv_fs_res_t fs_write(lv_fs_drv_t *drv, void *file_p,
                            const void *buf, uint32_t btw, uint32_t *bw);
static lv_fs_res_t fs_seek(lv_fs_drv_t *drv, void *file_p,
                           uint32_t pos, lv_fs_whence_t whence);
static lv_fs_res_t fs_tell(lv_fs_drv_t *drv, void *file_p,
                           uint32_t *pos_p);
static void *fs_dir_open(lv_fs_drv_t *drv, const char *path);
static lv_fs_res_t fs_dir_read(lv_fs_drv_t *drv, void *dir_p, char *fn);
static lv_fs_res_t fs_dir_close(lv_fs_drv_t *drv, void *dir_p);

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: fs_open
 *
 * Description:
 *   Open a file.
 *
 * Input Parameters:
 *   drv    - pointer to a driver where this function belongs.
 *   path   - path to the file beginning with the driver letter.
 *            (e.g. /folder/file.txt)
 *   mode   - read: FS_MODE_RD, write: FS_MODE_WR,
 *            both: FS_MODE_RD | FS_MODE_WR
 *
 * Returned Value:
 *   pointer to a file_t variable.
 *
 ****************************************************************************/

static void *fs_open(lv_fs_drv_t *drv, const char *path, lv_fs_mode_t mode)
{
  int oflag = 0;
  if (mode == LV_FS_MODE_WR)
    {
      oflag = O_WRONLY | O_CREAT;
    }
  else if (mode == LV_FS_MODE_RD)
    {
      oflag = O_RDONLY;
    }
  else if (mode == (LV_FS_MODE_WR | LV_FS_MODE_RD))
    {
      oflag = O_RDWR | O_CREAT;
    }

  file_t f = open(--path, oflag);
  if (f < 0)
    {
      return NULL;
    }

  file_t *fp = lv_mem_alloc(sizeof(file_t));
  if (fp == NULL)
    {
      return NULL;
    }

  *fp = f;

  return fp;
}

/****************************************************************************
 * Name: fs_close
 *
 * Description:
 *   Close an opened file.
 *
 * Input Parameters:
 *   drv    - pointer to a driver where this function belongs.
 *   file_p - pointer to a file_t variable.
 *
 * Returned Value:
 *   LV_FS_RES_OK: no error, the file is read
 *   any error from lv_fs_res_t enum.
 *
 ****************************************************************************/

static lv_fs_res_t fs_close(lv_fs_drv_t *drv, void *file_p)
{
  /* Just avoid the confusing casings */

  file_t *fp = file_p;

  int retval = close(*fp);
  lv_mem_free(file_p);
  return retval < 0 ? LV_FS_RES_FS_ERR : LV_FS_RES_OK;
}

/****************************************************************************
 * Name: fs_read
 *
 * Description:
 *   Read data from an opened file.
 *
 * Input Parameters:
 *   drv    - pointer to a driver where this function belongs.
 *   file_p - pointer to a file_t variable.
 *   buf    - pointer to a memory block where to store the read data.
 *   btr    - number of Bytes To Read.
 *   br     - the real number of read bytes (Byte Read).
 *
 * Returned Value:
 *   LV_FS_RES_OK: no error, the file is read
 *   any error from lv_fs_res_t enum.
 *
 ****************************************************************************/

static lv_fs_res_t fs_read(lv_fs_drv_t *drv, void *file_p,
                           void *buf, uint32_t btr, uint32_t *br)
{
  /* Just avoid the confusing casings */

  file_t *fp = file_p;

  *br = read(*fp, buf, btr);

  return (int32_t)*br < 0 ? LV_FS_RES_FS_ERR : LV_FS_RES_OK;
}

/****************************************************************************
 * Name: fs_write
 *
 * Description:
 *   Write into a file.
 *
 * Input Parameters:
 *   drv    - pointer to a driver where this function belongs.
 *   file_p - pointer to a file_t variable.
 *   buf    - pointer to a buffer with the bytes to write.
 *   btw    - Bytes To Write.
 *   bw     - the number of real written bytes (Bytes Written).
 *            NULL if unused.
 *
 * Returned Value:
 *   LV_FS_RES_OK or any error from lv_fs_res_t enum.
 *
 ****************************************************************************/

static lv_fs_res_t fs_write(lv_fs_drv_t *drv, void *file_p,
                            const void *buf, uint32_t btw, uint32_t *bw)
{
  /* Just avoid the confusing casings */

  file_t *fp = file_p;

  *bw = write(*fp, buf, btw);

  return (int32_t)*bw < 0 ? LV_FS_RES_FS_ERR : LV_FS_RES_OK;
}

/****************************************************************************
 * Name: fs_seek
 *
 * Description:
 *   Set the read write pointer. Also expand the file size if necessary.
 *
 * Input Parameters:
 *   drv    - pointer to a driver where this function belongs.
 *   file_p - pointer to a file_t variable.
 *   pos    - the new position of read write pointer.
 *   whence - seek modes.
 *
 * Returned Value:
 *   LV_FS_RES_OK: no error, the file is read
 *   any error from lv_fs_res_t enum.
 *
 ****************************************************************************/

static lv_fs_res_t fs_seek(lv_fs_drv_t *drv, void *file_p,
                           uint32_t pos, lv_fs_whence_t whence)
{
  /* Just avoid the confusing casings */

  file_t *fp = file_p;

  off_t offset = lseek(*fp, pos, (int)whence);

  return offset < 0 ? LV_FS_RES_FS_ERR : LV_FS_RES_OK;
}

/****************************************************************************
 * Name: fs_tell
 *
 * Description:
 *   Give the position of the read write pointer.
 *
 * Input Parameters:
 *   drv    - pointer to a driver where this function belongs.
 *   file_p - pointer to a file_t variable.
 *   pos_p  - pointer to to store the result.
 *
 * Returned Value:
 *   LV_FS_RES_OK: no error, the file is read
 *   any error from lv_fs_res_t enum.
 *
 ****************************************************************************/

static lv_fs_res_t fs_tell(lv_fs_drv_t *drv, void *file_p,
                           uint32_t *pos_p)
{
  /* Just avoid the confusing casings */

  file_t *fp = file_p;

  *pos_p = lseek(*fp, 0, SEEK_CUR);

  return (int32_t)*pos_p < 0 ? LV_FS_RES_FS_ERR : LV_FS_RES_OK;
}

/****************************************************************************
 * Name: fs_dir_open
 *
 * Description:
 *   Initialize a 'dir_t' variable for directory reading.
 *
 * Input Parameters:
 *   drv     - pointer to a driver where this function belongs.
 *   path    - path to a directory.
 *
 * Returned Value:
 *   pointer to a 'dir_t' variable.
 *
 ****************************************************************************/

static void *fs_dir_open(lv_fs_drv_t *drv, const char *path)
{
  return opendir(--path);
}

/****************************************************************************
 * Name: fs_dir_read
 *
 * Description:
 *   Read the next filename form a directory.
 *   The name of the directories will begin with '/'.
 *
 * Input Parameters:
 *   drv    - pointer to a driver where this function belongs.
 *   dir_p  - pointer to an initialized 'fs_read_dir_t' variable.
 *   fn     - pointer to a buffer to store the filename.
 *
 * Returned Value:
 *   LV_FS_RES_OK or any error from lv_fs_res_t enum.
 *
 ****************************************************************************/

static lv_fs_res_t fs_dir_read(lv_fs_drv_t *drv, void *dir_p, char *fn)
{
  /* Just avoid the confusing casings */

  dir_t *dp = dir_p;

  do
    {
      struct dirent *entry = readdir(*dp);

      if (entry)
        {
          if (entry->d_type == DT_DIR)
            {
              sprintf(fn, "/%s", entry->d_name);
            }
          else
            {
              strcpy(fn, entry->d_name);
            }
        }
      else
        {
          strcpy(fn, "");
        }
    }
  while (strcmp(fn, "/.") == 0 || strcmp(fn, "/..") == 0);

  return LV_FS_RES_OK;
}

/****************************************************************************
 * Name: fs_dir_read
 *
 * Description:
 *   Close the directory reading.
 *
 * Input Parameters:
 *   drv    - pointer to a driver where this function belongs.
 *   dir_p  - pointer to an initialized 'fs_read_dir_t' variable.
 *
 * Returned Value:
 *   LV_FS_RES_OK or any error from lv_fs_res_t enum.
 *
 ****************************************************************************/

static lv_fs_res_t fs_dir_close(lv_fs_drv_t *drv, void *dir_p)
{
  dir_t *dp = dir_p;

  return closedir(*dp) < 0 ? LV_FS_RES_FS_ERR : LV_FS_RES_OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: lv_fs_interface_init
 *
 * Description:
 *   Register a driver for the File system interface.
 *
 ****************************************************************************/

void lv_fs_interface_init(void)
{
  lv_fs_drv_t *fs_drv = (lv_fs_drv_t *)lv_mem_alloc(sizeof(lv_fs_drv_t));

  if (fs_drv == NULL)
    {
      LV_LOG_ERROR("fs_drv malloc failed");
      return;
    }

  lv_fs_drv_init(fs_drv);

  /* Set up fields... */

  fs_drv->letter = LV_FS_LETTER;
  fs_drv->open_cb = fs_open;
  fs_drv->close_cb = fs_close;
  fs_drv->read_cb = fs_read;
  fs_drv->write_cb = fs_write;
  fs_drv->seek_cb = fs_seek;
  fs_drv->tell_cb = fs_tell;

  fs_drv->dir_close_cb = fs_dir_close;
  fs_drv->dir_open_cb = fs_dir_open;
  fs_drv->dir_read_cb = fs_dir_read;

  lv_fs_drv_register(fs_drv);
  LV_LOG_INFO("fs_drv register success");
}
