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

#include <nuttx/config.h>
#include <sched.h>
#include <pthread.h>
#include <string.h>
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <cmocka.h>
#include <nuttx/lib/builtin.h>

int main(int argc, char* argv[]) {
    const char prefix[] = CONFIG_TESTING_CMOCKA_PROGNAME"_";
    const struct builtin_s* builtin;
    int len = strlen(prefix);
    struct sched_param param;
    pthread_attr_t attr;
    pthread_t pid;
    char *comp = NULL;
    char *skipcase = NULL;
    int ret;
    int i;
    int j;
    int m_len;

    if (strlen(argv[0]) < len - 1 ||
        strncmp(argv[0], prefix, len - 1)) {
        return 0;
    }

    if (argc > 1 && strncmp(prefix, argv[1], len) == 0) {
        comp = &argv[1][len];
    }

    for (i = 0; (builtin = builtin_for_index(i)) != NULL; i++) {
        if (builtin->main == NULL ||
            strlen(builtin->name) < len ||
            strncmp(builtin->name, prefix, len)) {
            continue;
        }

        if (comp &&
            strncmp(builtin->name + len, comp, strlen(comp))) {
            continue;
        }

        for (j = 1; j < argc; j++) {
          m_len = strlen(builtin->name)-len;
          if (strncmp(builtin->name + len, argv[j], m_len) == 0 &&
              strlen(argv[j]) > m_len) {
            cmocka_set_skip_filter(NULL);
            skipcase = &argv[j][m_len + 1];
            cmocka_set_skip_filter(skipcase);
          }
        }

        pthread_attr_init(&attr);
        pthread_attr_getschedparam(&attr, &param);
        param.sched_priority = builtin->priority;
        pthread_attr_setschedparam(&attr, &param);
        pthread_attr_setstacksize(&attr, builtin->stacksize);
        ret = pthread_create(&pid, &attr,
                            (pthread_startroutine_t)builtin->main, NULL);
        pthread_attr_destroy(&attr);

        if (ret != 0) {
            break;
        }

        pthread_join(pid, NULL);
    }
    cmocka_set_skip_filter(NULL);
    return 0;
}
