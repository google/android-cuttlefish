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
#include <remoter_framework_pkt.h>

void remoter_connect(avd::SharedFD* dest) {
  *dest = avd::SharedFD::SocketLocalClient(
      "remoter", ANDROID_SOCKET_NAMESPACE_RESERVED, SOCK_STREAM);
  if ((*dest)->IsOpen()) {
    ALOGE("Failed to connect to remoter (%s)", (*dest)->StrError());
  } else {
#ifdef DEBUG_CONNECTIONS
    ALOGI("Connected to remoter (socket %d)", socket);
#endif
  }
}

int remoter_connect() {
  int socket = socket_local_client(
      "remoter", ANDROID_SOCKET_NAMESPACE_RESERVED, SOCK_STREAM);
  if (socket < 0) {
    ALOGE("Failed to connect to remoter (%s)", strerror(errno));
    return -1;
  } else {
#ifdef DEBUG_CONNECTIONS
    ALOGI("Connected to remoter (socket %d)", socket);
#endif
  }
  return socket;
}

