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
#include "particle_sensor.h"
#include <stdlib.h>

static int last_beats = 800;

float particle_sensor_get_beats(void)
{
    int breats = ((uint16_t)rand() % 600 + 600);
    if (breats > last_beats) {
        last_beats++;
    } else if (breats < last_beats) {
        last_beats--;
    }

    return last_beats / 10.0f;
}
