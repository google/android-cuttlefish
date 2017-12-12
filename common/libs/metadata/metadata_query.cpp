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
#include "common/libs/fs/shared_fd.h"

#include "common/libs/glog/logging.h"
#include "common/libs/metadata/metadata_query.h"

namespace {
class MetadataQueryImpl : public MetadataQuery {
 public:
  MetadataQueryImpl() {
  }

  ~MetadataQueryImpl() {}

  bool QueryServer(AutoFreeBuffer* buffer) {
    if (!client_->IsOpen()) {
      client_ = avd::SharedFD::SocketLocalClient(
          "gce_metadata", true, SOCK_STREAM);

      if (!client_->IsOpen()) {
        LOG(ERROR) << "Couldn't connect to metadata proxy.";
        return false;
      }
    }

    int32_t length;
    client_->Read(&length, sizeof(length));

    if ((length < 0) || (length > (1 << 20))) {
      LOG(ERROR) << "Invalid metadata length: " << length;
      client_->Close();
      return false;
    }

    buffer->Resize(length);
    client_->Read(buffer->data(), length);
    buffer->Resize(length + 1);
    return true;
  }

 private:
  avd::SharedFD client_;
};
}  // namespace

MetadataQuery* MetadataQuery::New() {
  return new MetadataQueryImpl();
}

