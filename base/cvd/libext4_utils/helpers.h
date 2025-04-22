/*
 * Copyright (C) 2020 The Android Open Source Project
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
#pragma once

#include <stdio.h>
#include <string.h>

#define warn(fmt, args...)                                           \
    do {                                                             \
        fprintf(stderr, "warning: %s: " fmt "\n", __func__, ##args); \
    } while (0)
#define error(fmt, args...)                                        \
    do {                                                           \
        fprintf(stderr, "error: %s: " fmt "\n", __func__, ##args); \
        if (!force) longjmp(setjmp_env, EXIT_FAILURE);             \
    } while (0)
#define error_errno(s, args...) error(s ": %s", ##args, strerror(errno))
#define critical_error(fmt, args...)                                        \
    do {                                                                    \
        fprintf(stderr, "critical error: %s: " fmt "\n", __func__, ##args); \
        longjmp(setjmp_env, EXIT_FAILURE);                                  \
    } while (0)
#define critical_error_errno(s, args...) critical_error(s ": %s", ##args, strerror(errno))
