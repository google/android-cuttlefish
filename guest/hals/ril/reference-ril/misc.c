/* //device/system/reference-ril/misc.c
**
** Copyright 2006, The Android Open Source Project
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/
#include <sys/system_properties.h>

#include <fcntl.h>
#include "misc.h"
/** returns 1 if line starts with prefix, 0 if it does not */
int strStartsWith(const char *line, const char *prefix)
{
    for ( ; *line != '\0' && *prefix != '\0' ; line++, prefix++) {
        if (*line != *prefix) {
            return 0;
        }
    }

    return *prefix == '\0';
}

// Returns true iff running this process in an emulator VM
bool isInEmulator(void) {
  static int inQemu = -1;
  if (inQemu < 0) {
      char propValue[PROP_VALUE_MAX];
      inQemu = (__system_property_get("ro.boot.qemu", propValue) != 0);
  }
  return inQemu == 1;
}

int qemu_open_modem_port() {
    char propValue[PROP_VALUE_MAX];
    if (__system_property_get("vendor.qemu.vport.modem", propValue) <= 0) {
        return -1;
    }
    int fd = open(propValue, O_RDWR);
    return fd;
}
