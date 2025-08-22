/*
**
** Copyright 2020, The Android Open Source Project
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** decode base64, returns the encoded output */
int base64_decode(const char *base64, unsigned char *bindata);
/** encode base64, returns the number of bytes decoded */
char *base64_encode(const unsigned char *bindata, char *base64, int binlength);
#ifdef __cplusplus
}
#endif
