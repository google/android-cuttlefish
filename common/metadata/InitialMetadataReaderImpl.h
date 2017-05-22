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
#ifndef LIBS_GCEMETADATA_INITIALMETADATAREADERIMPL_H_
#define LIBS_GCEMETADATA_INITIALMETADATAREADERIMPL_H_

#include <map>
#include <string>

#include <DisplayProperties.h>
#include <InitialMetadataReader.h>

namespace avd {

typedef std::map<std::string, std::string> MetadataReaderValueMap;

class InitialMetadataReaderImpl : public InitialMetadataReader {
 public:
  InitialMetadataReaderImpl();

  const DisplayProperties& GetDisplay() const {
    return display_;
  }

  const char* GetValueForKey(const char* key) const;
  const char* GetInstanceHostname() const { return instance_hostname_.c_str(); }
  bool Init(const char* path);

 protected:
  bool is_initialized_;
  MetadataReaderValueMap values_;
  std::string instance_hostname_;
  DisplayProperties display_;
  DISALLOW_COPY_AND_ASSIGN(InitialMetadataReaderImpl);
};

}  // namespace avd

#endif  // LIBS_GCEMETADATA_INITIALMETADATAREADERIMPL_H_
