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
#ifndef CUTTLEFISH_COMMON_COMMON_LIBS_METADATA_INITIAL_METADATA_READER_H_
#define CUTTLEFISH_COMMON_COMMON_LIBS_METADATA_INITIAL_METADATA_READER_H_

namespace avd {

class DisplayProperties;

// See the comments in MetadataService.h for the rules that apply to
// project and instance values.
class InitialMetadataReader {
 public:
  // Describes the configuration of a screen. This is here because it is shared
  // with gce_init, which can't handle some of the dependencies in the full
  // framebuffer configuration.
  static InitialMetadataReader* getInstance();
  virtual const DisplayProperties& GetDisplay() const = 0;
  virtual const char* GetInstanceHostname() const = 0;
  virtual const char* GetValueForKey(const char* key) const = 0;

 protected:
  InitialMetadataReader() {}
  virtual ~InitialMetadataReader() {}
  InitialMetadataReader(const InitialMetadataReader&);
  InitialMetadataReader& operator= (const InitialMetadataReader&);
};

}

#endif  // CUTTLEFISH_COMMON_COMMON_LIBS_METADATA_INITIAL_METADATA_READER_H_
