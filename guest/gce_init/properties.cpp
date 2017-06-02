/*
 * Copyright (C) 2016 The Android Open Source Project
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
#include "guest/gce_init/properties.h"

#include <ctype.h>
#include <glog/logging.h>

#include "common/libs/auto_resources/auto_resources.h"

namespace avd {

bool PropertyLineToKeyValuePair(
    char* line, char** out_key, char** out_value) {
  int length = strlen(line);
  *out_key = NULL;
  *out_value = NULL;

  char* key = line;

  // Trim whitespaces at the beginning of line.
  while ((length > 0) && isspace(*key)) {
    --length;
    ++key;
  }

  // Trim whitespaces at the end of line.
  while ((length > 0) && isspace(key[length - 1])) {
    --length;
    key[length] = '\0';
  }

  // Empty line (^\s*$\n) => no key / value pair.
  if (!length) return true;

  // Start of comment => no key / value pair.
  if (*key == '#') return true;

  // Separate key and value. Separator is the '=' sign.
  char* value = key;
  while ((*value != '=') && (length > 0)) {
    ++value;
    --length;
  }

  // Malformed line: value with no attribute ("=value").
  if (key == value) return false;

  // Malformed line: attribute with no value ("attribute").
  if (!length) return false;

  *(value++) = '\0';  // Separate key and value with \0.
  --length;

  *out_key = key;
  *out_value = value;

  return true;
}

bool LoadPropertyFile(
    const char* name, std::map<std::string, std::string>* properties) {
  AutoCloseFILE f(fopen(name, "r"));
  if (!f) return false;

  char line[1024];
  int line_number = 1;
  while (fgets(&line[0], sizeof(line), f)) {
    char* key;
    char* value;

    if (!PropertyLineToKeyValuePair(&line[0], &key, &value)) {
      LOG(ERROR) << "Failed to process file " << name
                 << ", line: " << line_number
                 << ": Invalid property declaration: " << line;
      return false;
    }

    if (key != NULL && value != NULL) {
      (*properties)[key] = value;
    }

    ++line_number;
  }
  return true;
}

}  // namespace avd
