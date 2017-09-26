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
/* This implements a lights hardware library for the Android emulator.
 * the following code should be built as a shared library that will be
 * placed into /system/lib/hw/lights.goldfish.so
 *
 * It will be loaded by the code in hardware/libhardware/hardware.c
 * which is itself called from
 * ./frameworks/base/services/jni/com_android_server_HardwareService.cpp
 */

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "GceLights"

#include <cutils/log.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <hardware/lights.h>

static int set_light(
    struct light_device_t* dev, struct light_state_t const* state) {
  ALOGI("%s: dev %p state %p", __FUNCTION__, dev, state);
  if (state) {
    ALOGI("%s: state value %d\n", __FUNCTION__, state->color);
  }
  return 0;
}

static int close_lights(struct light_device_t* dev) {
  free( dev );
  return 0;
}

static int open_lights(
    const struct hw_module_t* module, char const *name,
    struct hw_device_t **device)  {
  struct light_device_t *dev = malloc( sizeof(struct light_device_t) );
  if (dev == NULL) {
    return -EINVAL;
  }
  memset( dev, 0, sizeof(*dev) );

  dev->common.tag = HARDWARE_DEVICE_TAG;
  dev->common.version = 0;
  dev->common.module = (struct hw_module_t*)module;
  dev->common.close = (int (*)(struct hw_device_t*))close_lights;
  dev->set_light = set_light;
  *device = (struct hw_device_t*)dev;
  return 0;
}

static struct hw_module_methods_t lights_module_methods = {
    .open =  open_lights,
};

struct hw_module_t HAL_MODULE_INFO_SYM = {
    .tag = HARDWARE_MODULE_TAG,
    .version_major = 1,
    .version_minor = 0,
    .id = LIGHTS_HARDWARE_MODULE_ID,
    .name = "Android GCE lights Module",
    .author = "Google",
    .methods = &lights_module_methods,
};
