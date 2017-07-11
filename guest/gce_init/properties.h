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
#ifndef GUEST_GCE_INIT_PROPERTIES_H_
#define GUEST_GCE_INIT_PROPERTIES_H_

#include <map>
#include <string>

namespace avd {

// Converts a line of text (typically read from property file) to
// |out_key| and |out_value| pair.
// Modifies content of the |line| string
//
// Returns true, if line was successfully parsed.
// Key and Value can be NULL if line is empty or a comment.
bool PropertyLineToKeyValuePair(char* line, char** out_key, char** out_value);

// Load property file |name| and process its contents.
// Store result in |properties| map.
// Returns true, if property file was successfully loaded.
bool LoadPropertyFile(const char* name,
                      std::map<std::string, std::string>* properties);

}  // namespace avd

#endif  // GUEST_GCE_INIT_PROPERTIES_H_
