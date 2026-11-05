/*
 * Copyright (C) 2026 The Android Open Source Project
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

/* <sys/cdefs.h> is a glibc-internal header that apps shouldn't use, but
 * unfortunately android_system_logging does. to enable compilation with
 * musl, redefine only the macros used here.
 */
#ifdef __GLIBC__
# include_next <sys/cdefs.h>
#else
# ifdef __cplusplus
#  define __BEGIN_DECLS extern "C" {
#  define __END_DECLS   }
# else
#  define __BEGIN_DECLS
#  define __END_DECLS
# endif
 /* This is really from unistd.h, but overriding that header in
  * android_system_logging has consequences for downstream, so do it here
  */
# define TEMP_FAILURE_RETRY(expression)           \
  (__extension__                                  \
    ({ long int __result;                         \
       do __result = (long int) (expression);     \
       while (__result == -1L && errno == EINTR); \
       __result; }))
#endif
