/****************************************************************************
 * apps/testing/monkey/monkey_port.h
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

#ifndef __MONKEY_PORT_H__
#define __MONKEY_PORT_H__

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "monkey_type.h"

/****************************************************************************
 * Public Types
 ****************************************************************************/

struct monkey_port_dev_s
{
  int fd;
  enum monkey_dev_type_e type;
  union monkey_dev_state_u state;
  union monkey_dev_state_u last_state;
};

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
 * Name: monkey_port_create
 ****************************************************************************/

FAR struct monkey_port_dev_s *monkey_port_create(FAR const char *dev_path,
                                              enum monkey_dev_type_e type);

/****************************************************************************
 * Name: monkey_port_delete
 ****************************************************************************/

void monkey_port_delete(FAR struct monkey_port_dev_s *dev);

/****************************************************************************
 * Name: monkey_port_set_state
 ****************************************************************************/

void monkey_port_set_state(FAR struct monkey_port_dev_s *dev,
                           FAR const union monkey_dev_state_u *state);

/****************************************************************************
 * Name: monkey_port_get_state
 ****************************************************************************/

bool monkey_port_get_state(FAR struct monkey_port_dev_s *dev,
                           FAR union monkey_dev_state_u *state);

/****************************************************************************
 * Name: monkey_port_get_type
 ****************************************************************************/

enum monkey_dev_type_e monkey_port_get_type(
                       FAR struct monkey_port_dev_s *dev);

/****************************************************************************
 * Name: monkey_port_get_type_name
 ****************************************************************************/

FAR const char *monkey_port_get_type_name(enum monkey_dev_type_e type);

#undef EXTERN
#ifdef __cplusplus
}
#endif

#endif /* __MONKEY_PORT_H__ */
