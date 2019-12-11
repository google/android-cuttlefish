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

/* This implements a GPS hardware HAL library for cuttlefish.
 * A produced shared library is placed in /system/lib/hw/gps.gce.so, and
 * loaded by hardware/libhardware/hardware.c code which is called from
 * android_location_GpsLocationProvider.cpp
 */

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <unistd.h>
#include <inttypes.h>

#include <log/log.h>
#include <cutils/sockets.h>
#include <hardware/gps.h>

#include "guest/hals/gps/gps_thread.h"

static GpsState _gps_state;

// Cleans up GpsState data structure.
static void gps_state_cleanup(GpsState* s) {
  char cmd = CMD_QUIT;

  write(s->control[0], &cmd, 1);
  if (s->thread > 0) {
    pthread_join(s->thread, NULL);
  }

  close(s->control[0]);
  close(s->control[1]);
  close(s->fd);

  s->thread = 0;
  s->control[0] = -1;
  s->control[1] = -1;
  s->fd = -1;
  s->init = 0;
}

static int gce_gps_init(GpsCallbacks* callbacks) {
  D("%s: called", __FUNCTION__);
  // Stop if the framework does not fulfill its interface contract.
  // We don't want to return an error and continue to ensure that we
  // catch framework breaks ASAP and to give a tombstone to track down the
  // offending code.
  LOG_ALWAYS_FATAL_IF(!callbacks->location_cb);
  LOG_ALWAYS_FATAL_IF(!callbacks->status_cb);
  LOG_ALWAYS_FATAL_IF(!callbacks->sv_status_cb);
  LOG_ALWAYS_FATAL_IF(!callbacks->nmea_cb);
  LOG_ALWAYS_FATAL_IF(!callbacks->set_capabilities_cb);
  LOG_ALWAYS_FATAL_IF(!callbacks->acquire_wakelock_cb);
  LOG_ALWAYS_FATAL_IF(!callbacks->release_wakelock_cb);
  LOG_ALWAYS_FATAL_IF(!callbacks->create_thread_cb);
  LOG_ALWAYS_FATAL_IF(!callbacks->request_utc_time_cb);
  if (!_gps_state.init) {
    _gps_state.init = 1;
    _gps_state.control[0] = -1;
    _gps_state.control[1] = -1;
    _gps_state.thread = 0;

    _gps_state.fd = socket_local_client(
        "gps_broadcasts", ANDROID_SOCKET_NAMESPACE_ABSTRACT, SOCK_STREAM);
    if (_gps_state.fd < 0) {
      ALOGE("no GPS emulation detected.");
      goto Fail;
    }
    D("GPS HAL will receive data from remoter via gps_broadcasts channel.");

    if (socketpair(AF_LOCAL, SOCK_STREAM, 0, _gps_state.control) < 0) {
      ALOGE("could not create thread control socket pair: %s", strerror(errno));
      goto Fail;
    }

    _gps_state.callbacks = *callbacks;
    ALOGE("Starting thread callback=%p", callbacks->location_cb);
    _gps_state.thread = callbacks->create_thread_cb(
        "gps_state_thread", gps_state_thread, &_gps_state);
    if (!_gps_state.thread) {
      ALOGE("could not create GPS thread: %s", strerror(errno));
      goto Fail;
    }
  }

  if (_gps_state.fd < 0) return -1;
  return 0;

Fail:
  gps_state_cleanup(&_gps_state);
  return -1;
}

static void gce_gps_cleanup() {
  D("%s: called", __FUNCTION__);
  if (_gps_state.init) gps_state_cleanup(&_gps_state);
}

static int gce_gps_start() {
  if (!_gps_state.init) {
    ALOGE("%s: called with uninitialized gps_state!", __FUNCTION__);
    return -1;
  }

  char cmd = CMD_START;
  int ret;
  do {
    ret = write(_gps_state.control[0], &cmd, 1);
  } while (ret < 0 && errno == EINTR);

  if (ret != 1) {
    D("%s: could not send CMD_START command: ret=%d: %s", __FUNCTION__, ret,
      strerror(errno));
    return -1;
  }

  return 0;
}

static int gce_gps_stop() {
  D("%s: called", __FUNCTION__);
  if (!_gps_state.init) {
    ALOGE("%s: called with uninitialized gps_state!", __FUNCTION__);
    return -1;
  }

  char cmd = CMD_STOP;
  int ret;

  do {
    ret = write(_gps_state.control[0], &cmd, 1);
  } while (ret < 0 && errno == EINTR);

  if (ret != 1) {
    ALOGE("%s: could not send CMD_STOP command: ret=%d: %s", __FUNCTION__, ret,
          strerror(errno));
    return -1;
  }
  return 0;
}

static int gce_gps_inject_time(GpsUtcTime /*time*/, int64_t /*time_ref*/,
                               int /*uncertainty*/) {
  D("%s: called", __FUNCTION__);
  if (!_gps_state.init) {
    ALOGE("%s: called with uninitialized gps_state!", __FUNCTION__);
    return -1;
  }

  return 0;
}

static int gce_gps_inject_location(double /*latitude*/, double /*longitude*/,
                                   float /*accuracy*/) {
  D("%s: called", __FUNCTION__);
  if (!_gps_state.init) {
    ALOGE("%s: called with uninitialized gps_state!", __FUNCTION__);
    return -1;
  }

  return 0;
}

static void gce_gps_delete_aiding_data(GpsAidingData /*flags*/) {
  D("%s: called", __FUNCTION__);
  if (!_gps_state.init) {
    ALOGE("%s: called with uninitialized gps_state!", __FUNCTION__);
    return;
  }
}

static int gce_gps_set_position_mode(GpsPositionMode mode,
                                     GpsPositionRecurrence recurrence,
                                     uint32_t min_interval,
                                     uint32_t preferred_accuracy,
                                     uint32_t preferred_time) {
  D("%s: called", __FUNCTION__);
  if (!_gps_state.init) {
    ALOGE("%s: called with uninitialized gps_state!", __FUNCTION__);
    return -1;
  }
  ALOGE("%s(mode=%d, recurrence=%d, min_interval=%" PRIu32
        ", "
        "preferred_accuracy=%" PRIu32 ", preferred_time=%" PRIu32
        ") unimplemented",
        __FUNCTION__, mode, recurrence, min_interval, preferred_accuracy,
        preferred_time);
  return 0;
}

static const void* gce_gps_get_extension(const char* name) {
  D("%s: called", __FUNCTION__);
  // It is normal for this to be called before init.
  ALOGE("%s(%s): called but not implemented.", __FUNCTION__,
        name ? name : "NULL");
  return NULL;
}

static const GpsInterface gceGpsInterface = {
    sizeof(GpsInterface),
    gce_gps_init,
    gce_gps_start,
    gce_gps_stop,
    gce_gps_cleanup,
    gce_gps_inject_time,
    gce_gps_inject_location,
    gce_gps_delete_aiding_data,
    gce_gps_set_position_mode,
    gce_gps_get_extension,
};

const GpsInterface* gps_get_gps_interface(struct gps_device_t* /*dev*/) {
  return &gceGpsInterface;
}

static int open_gps(const struct hw_module_t* module, char const* /*name*/,
                    struct hw_device_t** device) {
  struct gps_device_t* dev =
      (struct gps_device_t*)malloc(sizeof(struct gps_device_t));
  LOG_FATAL_IF(!dev, "%s: malloc returned NULL.", __FUNCTION__);
  memset(dev, 0, sizeof(*dev));

  dev->common.tag = HARDWARE_DEVICE_TAG;
  dev->common.version = 0;
  dev->common.module = (struct hw_module_t*)module;
  dev->get_gps_interface = gps_get_gps_interface;

  *device = (struct hw_device_t*)dev;
  return 0;
}

static struct hw_module_methods_t gps_module_methods = {
    .open = open_gps};

struct hw_module_t HAL_MODULE_INFO_SYM = {
    .tag = HARDWARE_MODULE_TAG,
    .version_major = 1,
    .version_minor = 0,
    .id = GPS_HARDWARE_MODULE_ID,
    .name = "GCE GPS Module",
    .author = "The Android Open Source Project",
    .methods = & gps_module_methods,
};
