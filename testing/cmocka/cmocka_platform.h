/*
 * Copyright 2008 Google Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef CMOCKA_PLATFORM_H_
#define CMOCKA_PLATFORM_H_

#include <syslog.h>

#define cmocka_vprint_message(f,a) vsyslog(LOG_INFO,f,a)
#define cmocka_vprint_error(f,a) vsyslog(LOG_ERR,f,a)

#endif /* CMOCKA_PLATFORM_H_ */
