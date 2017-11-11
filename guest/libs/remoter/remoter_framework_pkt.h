/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include <stdlib.h>

#include <cutils/sockets.h>
#include <cutils/log.h>

#include "common/libs/fs/shared_fd.h"

// #define DEBUG_CONNECTIONS

/*
 * Packet structures for commands sent to the remoter from the GCE HAL.
 * This is a private protocol between the HAL and the remoter.
 */

static const size_t kSensorNameMaxLen = 64;
// Don't use PATH_MAX here because it would increate the size of every
// packet that we send to the remoter.
static const size_t kScreenRecordFilePathMaxLen = 128;
static const size_t kUnixSocketPathMaxLen = 128;

struct remoter_request_packet {
  /* Length of the packet in bytes. */
  uint32_t length;

  /* Operation to perform. */
  uint8_t operation;

  /* Set to '1' if a response packet is desired */
  uint8_t send_response;

  /* Operation arguments. */
  union {
    /* Arguments for the frame buffer 'post' operation. */
    struct {
      /* Y offset in the double-buffer where this frame starts. */
      uint32_t y_offset;
    } fb_post_params;
    /* Arguments for the frame buffer 'update rect' operation. */
    struct {
      uint32_t left;
      uint32_t top;
      uint32_t width;
      uint32_t height;
    } fb_update_rect_params;
    struct {
      uint32_t type;
      bool enabled;
      int64_t delay_ns;
      int handle;
    } sensor_state_params;
    struct {
      char filepath[kScreenRecordFilePathMaxLen];
    } screenrecord_params;
    struct {
      char unix_socket[kUnixSocketPathMaxLen];
    } hal_ready_params;
  } params;
} __attribute__((packed));

enum {
  kRemoterHALReady = 1,
  kRemoterSensorState
};

/*
 * If 'send_response' is set in a request then the remoter will respond
 * with the following structure.
 */
struct remoter_response_packet {
  uint32_t length;
  uint8_t status;
  union {
    struct {
      /* Number of 'struct sensor_list_element_packet's to follow */
      uint8_t num_sensors;
    } sensor_list_data;
  } data;
} __attribute__((packed));

struct sensor_list_element_packet {
  int handle;
  int type;
  char name[kSensorNameMaxLen];
  char vendor[kSensorNameMaxLen];
  int version;
  float max_range;
  float resolution;
  float power;
} __attribute__((packed));

enum {
  kResponseStatusOk = 1,
  kResponseStatusFailed
};

static inline void remoter_request_packet_init(
    struct remoter_request_packet* pkt, uint8_t operation,
    uint8_t send_response) {
  memset(pkt, 0, sizeof(*pkt));
  pkt->length = sizeof(*pkt);
  pkt->operation = operation;
  pkt->send_response = send_response;
}

static inline void remoter_response_packet_init(
    struct remoter_response_packet* pkt, uint8_t status) {
  memset(pkt, 0, sizeof(*pkt));
  pkt->length = sizeof(*pkt);
  pkt->status = status;
}

void remoter_connect(cvd::SharedFD* dest);
int remoter_connect();

static inline int remoter_read_request(
    const cvd::SharedFD& socket,
    struct remoter_request_packet* request) {
  int len;
  int remaining_data;
  /* Packets start with a 4 byte length (which includes the length). */

  if ((len = socket->Read(request, sizeof(request->length))) < 0) {
    ALOGE("%s: Failed to read remoter request (%s)",
          __FUNCTION__, socket->StrError());
    return -1;
  } else if (len == 0) {
    return 0;
  } else if (len != sizeof(request->length)) {
    ALOGE("%s: Failed to read remoter request (Short read)", __FUNCTION__);
    return -1;
  }

  /* Extra paranoia. */
  if (request->length != sizeof(*request)) {
    ALOGE("%s: Malformed remoter request", __FUNCTION__);
    return -1;
  }
  remaining_data = request->length - sizeof(request->length);
  uint8_t* cursor = ((uint8_t*)request) + sizeof(request->length);
  if ((len = socket->Read(cursor, remaining_data)) < 0) {
    ALOGE("%s: Failed to read remoter request (%s)",
          __FUNCTION__, socket->StrError());
    return -1;
  } else if (len == 0) {
    return 0;
  } else if (len != (int) remaining_data) {
    ALOGE("%s: Failed to read remoter request (Short read)", __FUNCTION__);
    return -1;
  }
  return 1;
}

static inline int remoter_read_response(
    int socket, struct remoter_response_packet* response) {
  int len;
  int remaining_data;
  /* Packets start with a 4 byte length (which includes the length). */

#ifdef DEBUG_CONNECTIONS
  ALOGI("remoter_read_response(): socket %d, length length = %d", socket,
        sizeof(response->length));
#endif
  if ((len = TEMP_FAILURE_RETRY(
      read(socket, response, sizeof(response->length)))) < 0) {
    ALOGE("%s: Failed to read remoter response (%s)",
          __FUNCTION__, strerror(errno));
    return -1;
  } else if (len == 0) {
    return 0;
  } else if (len != sizeof(response->length)) {
    ALOGE("%s: Failed to read remoter response (Short read)", __FUNCTION__);
    return -1;
  }

  /* Extra paranoia. */
  if (response->length != sizeof(*response)) {
    ALOGE("%s: Malformed remoter response", __FUNCTION__);
    return -1;
  }
  remaining_data = response->length - sizeof(response->length);
  uint8_t* cursor = ((uint8_t*)response) + sizeof(response->length);
#ifdef DEBUG_CONNECTIONS
  ALOGI("remoter_read_request(): socket %d, data length = %d",
        socket, remaining_data);
#endif
  if ((len = TEMP_FAILURE_RETRY(read(socket, cursor, remaining_data))) < 0) {
    ALOGE("%s: Failed to read remoter response (%s)",
          __FUNCTION__, strerror(errno));
    return -1;
  } else if (len == 0) {
    return 0;
  } else if (len != (int) remaining_data) {
    ALOGE("%s: Failed to read remoter response (Short read)", __FUNCTION__);
    return -1;
  }
  return 1;
}

static inline int remoter_send_request(
    int socket, struct remoter_request_packet* request) {
#ifdef DEBUG_CONNECTIONS
  ALOGI(
      "remoter_send_request(): socket %d, length %u", socket, sizeof(*request));
#endif
  int len = TEMP_FAILURE_RETRY(write(socket, request, sizeof(*request)));
  if (len <= 0) {
    ALOGE("Failed to write request to remoter (%s)", strerror(errno));
    return -1;
  } else if (len != sizeof(*request)) {
    ALOGE("Failed to write request to remoter (short write)");
    return -1;
  }
  return 0;
}

static inline int remoter_send_response(
    const cvd::SharedFD& socket,
    struct remoter_response_packet* response) {
  int len = socket->Write(response, sizeof(*response));
  if (len <=0) {
    ALOGE("%s: Failed to send response to remoter (%s)",
          __FUNCTION__, strerror(errno));
    return -1;
  }
  return 0;
}

static inline int remoter_do_single_request_with_socket(
    int socket, struct remoter_request_packet* request,
    struct remoter_response_packet* response) {

  if (request->send_response && !response) {
    ALOGE("%s: Request specifies a response but no response ptr set",
          __FUNCTION__);
    return -1;
  } else if (!request->send_response && response) {
    ALOGE("%s: Request specifies no response but has response ptr set",
          __FUNCTION__);
    return -1;
  }

  if (remoter_send_request(socket, request) < 0) {
    return -1;
  }

  if (response && (remoter_read_response(socket, response) <= 0)) {
    return -1;
  }
  return 0;
}

static inline int remoter_do_single_request(
    struct remoter_request_packet* request,
    struct remoter_response_packet* response) {
  int socket;
  if ((socket = remoter_connect()) < 0) {
    return -1;
  }

  if (remoter_do_single_request_with_socket(socket, request, response) < 0) {
    close(socket);
    return -1;
  }
  close(socket);
  return 0;
}

