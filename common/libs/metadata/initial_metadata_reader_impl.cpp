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
#include "common/libs/metadata/initial_metadata_reader_impl.h"

#include <errno.h>
#include <stdio.h>
#include <iostream>
#include <fstream>
#include <map>
#include <string>

#include <glog/logging.h>
#include <json/json.h>

#include "common/libs/metadata/gce_metadata_attributes.h"
#include "common/libs/metadata/gce_resource_location.h"

namespace avd {

InitialMetadataReaderImpl::InitialMetadataReaderImpl()
    : is_initialized_(false) {}

static std::string ValueToString(Json::Value value) {
  if (value.isString()) {
    return value.asString();
  } else {
    Json::FastWriter writer;
    return writer.write(value);
  }
}

void StoreValues(Json::Value source, MetadataReaderValueMap* dest) {
  if (!source.isObject()) {
    return;
  }
  Json::Value::Members members = source.getMemberNames();
  for (Json::Value::Members::const_iterator it = members.begin();
       it != members.end(); ++it) {
    (*dest)[*it] = ValueToString(source[*it]);
  }
}

bool InitialMetadataReaderImpl::Init(const char* config_path) {
  std::ifstream ifs(config_path);

  if (!ifs.good()) {
    LOG(ERROR) << "Couldn't open initial metadata file.";
    return false;
  }
  // Skip over the headers.
  std::string response_line;
  while (getline(ifs, response_line, '\n')) {
    if (response_line == "\r") {
      // End of headers.
      break;
    }
  }
  // Now parse the JSON payload.
  Json::Reader reader;
  Json::Value root;
  is_initialized_ = reader.parse(ifs, root);
  // Now we need to convert the values to strings. We do this because we need
  // stable pointers to return, and libjsoncpp deallocates strings when the
  // corresponding Value goes out of scope.
  if (is_initialized_) {
    Json::Value empty(Json::objectValue);
    Json::Value source = root.get("project", empty).get("attributes", empty);
    StoreValues(source, &values_);
    source = root.get("instance", empty).get("attributes", empty);
    StoreValues(source, &values_);
    instance_hostname_ = ValueToString(
        root.get("instance", empty).get("hostname", Json::stringValue));
  }
  display_.Parse(GetValueForKey(
      GceMetadataAttributes::kDisplayConfigurationKey));
  return is_initialized_;
}

const char* InitialMetadataReaderImpl::GetValueForKey(const char* key) const {
  MetadataReaderValueMap::const_iterator it = values_.find(key);
  if (it == values_.end()) {
    return NULL;
  }
  return it->second.c_str();
}

InitialMetadataReader* InitialMetadataReader::getInstance() {
  static InitialMetadataReaderImpl* instance;
  if (!instance) {
    instance = new InitialMetadataReaderImpl();
    instance->Init(GceResourceLocation::kInitialMetadataPath);
  }
  return instance;
}

}
