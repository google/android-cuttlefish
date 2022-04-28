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
/*
 * Driver interaction with extended Linux CFG8021
 */

#include "driver_cmd_nl80211.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include <linux/if.h>
#include <linux/if_ether.h>
#include <netinet/in.h>
#include "driver_nl80211.h"

#include "android_drv.h"
#include "common.h"
#include "config.h"
#include "wpa_supplicant_i.h"

int wpa_driver_nl80211_driver_cmd(void* priv, char* cmd, char* buf,
                                  size_t buf_len) {
  struct i802_bss* bss = priv;
  struct wpa_driver_nl80211_data* drv = bss->drv;
  int ret = 0;

  D("%s: called", __FUNCTION__);
  if (os_strcasecmp(cmd, "STOP") == 0) {
    linux_set_iface_flags(drv->global->ioctl_sock, bss->ifname, 0);
    wpa_msg(drv->ctx, MSG_INFO, WPA_EVENT_DRIVER_STATE "STOPPED");
  } else if (os_strcasecmp(cmd, "START") == 0) {
    linux_set_iface_flags(drv->global->ioctl_sock, bss->ifname, 1);
    wpa_msg(drv->ctx, MSG_INFO, WPA_EVENT_DRIVER_STATE "STARTED");
  } else if (os_strcasecmp(cmd, "MACADDR") == 0) {
    u8 macaddr[ETH_ALEN] = {};

    ret = linux_get_ifhwaddr(drv->global->ioctl_sock, bss->ifname, macaddr);
    if (!ret)
      ret =
          os_snprintf(buf, buf_len, "Macaddr = " MACSTR "\n", MAC2STR(macaddr));
  } else if (os_strcasecmp(cmd, "RELOAD") == 0) {
    wpa_msg(drv->ctx, MSG_INFO, WPA_EVENT_DRIVER_STATE "HANGED");
  } else {  // Use private command
    return 0;
  }
  return ret;
}

int wpa_driver_set_p2p_noa(void* priv, u8 count, int start, int duration) {
  D("%s: called", __FUNCTION__);
  return 0;
}

int wpa_driver_get_p2p_noa(void* priv, u8* buf, size_t len) {
  D("%s: called", __FUNCTION__);
  return 0;
}

int wpa_driver_set_p2p_ps(void* priv, int legacy_ps, int opp_ps, int ctwindow) {
  D("%s: called", __FUNCTION__);
  return -1;
}

int wpa_driver_set_ap_wps_p2p_ie(void* priv, const struct wpabuf* beacon,
                                 const struct wpabuf* proberesp,
                                 const struct wpabuf* assocresp) {
  D("%s: called", __FUNCTION__);
  return 0;
}
