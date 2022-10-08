/****************************************************************************
 * apps/gpsutils/minmea/vendor/bream.h
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

#ifndef __APPS_GPSUTILS_MINMEA_VENDOR_BREAM_H
#define __APPS_GPSUTILS_MINMEA_VENDOR_BREAM_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <minmea/minmea.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BREAM_MAX_CHANNEL         50

/****************************************************************************
 * Public Types
 ****************************************************************************/

enum bream_sentence_id
{
    BREAM_INVALID = -1,
    BREAM_UNKNOWN = 0,
    BREAM_UNSUPPORT,
    BREAM_SENTENCE_ASC_MEAS,
    BREAM_SENTENCE_PVT_DOP,
    BREAM_SENTENCE_PVT_PVT,
};

begin_packed_struct struct bream_ras_meas
{
    double   prmes;
    double   cpmes;
    float    domes;
    uint8_t  gnssid;
    uint8_t  svid;
    uint8_t  sigid;
    uint8_t  freqid;
    uint16_t locktime;
    uint8_t  cn0;
    uint8_t  prstdev;
    uint8_t  cpstdev;
    uint8_t  dostdev;
    uint8_t  trkstat;
    uint8_t  extra;
} end_packed_struct;

begin_packed_struct struct bream_sentence_asc_meas
{
    double rcvtow;
    uint16_t week;
    int8_t   leaps;
    uint8_t  nummeas;
    uint8_t  recstat;
    uint8_t  version;
    uint8_t  reserved1[2];
    struct bream_ras_meas meas[BREAM_MAX_CHANNEL];
} end_packed_struct;

begin_packed_struct struct bream_sentence_pvt_dop
{
    uint32_t itow;
    uint16_t gdop;
    uint16_t pdop;
    uint16_t tdop;
    uint16_t vdop;
    uint16_t hdop;
    uint16_t ndop;
    uint16_t edop;
} end_packed_struct;

begin_packed_struct struct bream_sentence_pvt_pvt
{
    uint32_t itow;
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t min;
    uint8_t sec;
    uint8_t valid;
    uint32_t tacc;
    int32_t nano;
    uint8_t fixtype;
    uint8_t flags;
    uint8_t flags2;
    uint8_t numsv;
    int32_t lon;
    int32_t lat;
    int32_t height;
    int32_t hmsl;
    uint32_t hacc;
    uint32_t vacc;
    int32_t veln;
    int32_t vele;
    int32_t veld;
    int32_t gspeed;
    int32_t headmot;
    uint32_t sacc;
    uint32_t headacc;
    uint16_t pdop;
    int8_t leaps;
    uint8_t reserved1[5];
    int32_t headveh;
    int16_t magdec;
    uint16_t magacc;
} end_packed_struct;

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* Determine sentence identifier. */

enum bream_sentence_id bream_sentence_id(FAR const void *sentence,
                                         FAR size_t *len);

/* Parse a specific type of sentence. */

void bream_parse_asc_meas(FAR struct bream_sentence_asc_meas *frame,
                          FAR const void *sentence);
void bream_parse_pvt_dop(FAR struct bream_sentence_pvt_dop *frame,
                         FAR const void *sentence);
void bream_parse_pvt_pvt(FAR struct bream_sentence_pvt_pvt *frame,
                         FAR const void *sentence);

#endif /* __APPS_GPSUTILS_MINMEA_VENDOR_BREAM_H */
