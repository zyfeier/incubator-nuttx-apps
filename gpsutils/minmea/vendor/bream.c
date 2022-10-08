/****************************************************************************
 * apps/gpsutils/minmea/vendor/bream.c
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

#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "bream.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BREAM_SYNC_CHAR1          0xB5
#define BREAM_SYNC_CHAR2          0x62
#define BREAM_CHKSUM_SIZE         0x02

#define BREAM_GROUP_ASC           0x02
#define BREAM_GROUP_PVT           0x01

#define BREAM_ASC_MEAS            0x15

#define BREAM_PVT_DOP             0x04
#define BREAM_PVT_PVT             0x07

/****************************************************************************
 * Private Types
 ****************************************************************************/

begin_packed_struct struct bream_header
{
  uint8_t sync1;
  uint8_t sync2;
  uint8_t group;
  uint8_t number;
  uint16_t payloadlen;
} end_packed_struct;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static ssize_t bream_verify_checksum(FAR const void *sentence, size_t len)
{
  FAR const struct bream_header *header = sentence;
  FAR const uint8_t *data = sentence;
  size_t pktlen = sizeof(*header) + header->payloadlen + BREAM_CHKSUM_SIZE;
  size_t i = offsetof(struct bream_header, group);
  uint8_t checka = 0;
  uint8_t checkb = 0;

  if (len < pktlen)
    {
      return -ENODATA;
    }

  while (i < sizeof(struct bream_header) + header->payloadlen)
    {
      checka += data[i++];
      checkb += checka;
    }

  if (data[i] == checka && data[i + 1] == checkb)
    {
      return pktlen;
    }

  return -ESTALE;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

enum bream_sentence_id bream_sentence_id(FAR const void *sentence,
                                         FAR size_t *len)
{
  FAR const struct bream_header *header = sentence;
  ssize_t pktlen;

  if (*len < sizeof(*header))
    {
      return BREAM_UNKNOWN;
    }

  if (header->sync1 != BREAM_SYNC_CHAR1 || header->sync2 != BREAM_SYNC_CHAR2)
    {
      return BREAM_INVALID;
    }

  if (header->group == BREAM_GROUP_PVT)
    {
      pktlen = bream_verify_checksum(sentence, *len);
      if (pktlen < 0)
        {
          if (pktlen == -ENODATA)
            {
              return BREAM_UNKNOWN;
            }

          if (pktlen == -ESTALE)
            {
              return BREAM_INVALID;
            }
        }

      *len = pktlen;

      switch (header->number)
        {
        case BREAM_PVT_DOP:
          {
          if (header->payloadlen == sizeof(struct bream_sentence_pvt_dop))
            {
              return BREAM_SENTENCE_PVT_DOP;
            }
          break;
          }

        case BREAM_PVT_PVT:
          {
            if (header->payloadlen == sizeof(struct bream_sentence_pvt_pvt))
              {
                return BREAM_SENTENCE_PVT_PVT;
              }
            break;
          }
        }
    }
  else if(header->group == BREAM_GROUP_ASC)
    {
      pktlen = bream_verify_checksum(sentence, *len);
      if (pktlen < 0)
        {
          if (pktlen == -ENODATA)
            {
              return BREAM_UNKNOWN;
            }

          if (pktlen == -ESTALE)
            {
              return BREAM_INVALID;
            }
        }

      *len = pktlen;

      if (header->number == BREAM_ASC_MEAS)
        {
          if (header->payloadlen == sizeof(struct bream_sentence_asc_meas))
            {
              return BREAM_ASC_MEAS;
            }
        }
    }

  return BREAM_UNSUPPORT;
}

void bream_parse_asc_meas(FAR struct bream_sentence_asc_meas *frame,
                          FAR const void *sentence)
{
  FAR const struct bream_header *header = sentence;
  memcpy(frame, header + 1, sizeof(*frame));
}

void bream_parse_pvt_dop(FAR struct bream_sentence_pvt_dop *frame,
                         FAR const void *sentence)
{
  FAR const struct bream_header *header = sentence;
  memcpy(frame, header + 1, sizeof(*frame));
}

void bream_parse_pvt_pvt(FAR struct bream_sentence_pvt_pvt *frame,
                         FAR const void *sentence)
{
  FAR const struct bream_header *header = sentence;
  memcpy(frame, header + 1, sizeof(*frame));
}
