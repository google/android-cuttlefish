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

#include <stdint.h>
#include <vector>

struct fec_handle;

// Checks if there is a valid AVB footer in the end of the image. If so, parses
// the contents of vbmeta struct from the given AVB footer. Returns 0 on
// success.
int parse_vbmeta_from_footer(fec_handle *f, std::vector<uint8_t> *vbmeta);

// Parses the AVB vbmeta for the information of hashtree and fec data.
int parse_avb_image(fec_handle *f, const std::vector<uint8_t> &vbmeta);
