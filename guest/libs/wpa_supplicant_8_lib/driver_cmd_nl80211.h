#pragma once
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
#include <guest/libs/platform_support/api_level_fixes.h>

#include <memory.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"
#include "linux_ioctl.h"
#include "wpa_supplicant_i.h"

#define VSOC_WPA_SUPPLICANT_DEBUG 0

#if VSOC_WPA_SUPPLICANT_DEBUG
#  define D(...) ALOGD(__VA_ARGS__)
#else
#  define D(...) ((void)0)
#endif


typedef struct android_wifi_priv_cmd {
  char* buf;
  int used_len;
  int total_len;
} android_wifi_priv_cmd;

#if VSOC_PLATFORM_SDK_BEFORE(K)

#include "driver.h"

struct i802_bss {
  struct wpa_driver_nl80211_data* drv;
  struct i802_bss* next;
  int ifindex;
  char ifname[IFNAMSIZ + 1];
  char brname[IFNAMSIZ];

  unsigned int beacon_set:1;
  unsigned int added_if_into_bridge:1;
  unsigned int added_bridge:1;
  unsigned int in_deinit:1;

  u8 addr[ETH_ALEN];

  int freq;

  void* ctx;
  struct nl_handle* nl_preq;
  struct nl_handle* nl_mgmt;
  struct nl_cb* nl_cb;

  struct nl80211_wiphy_data *wiphy_data;
  struct dl_list wiphy_list;
};

struct nl80211_global {
  struct dl_list interfaces;
  int if_add_ifindex;
  struct netlink_data *netlink;
  struct nl_cb* nl_cb;
  struct nl_handle* nl;
  int nl80211_id;
  int ioctl_sock;  // socket for ioctl() use

  struct nl_handle* nl_event;
};

struct wpa_driver_nl80211_data {
  struct nl80211_global* global;
  struct dl_list list;
  struct dl_list wiphy_list;
  char phyname[32];
  void* ctx;
  int ifindex;
  int if_removed;
  int if_disabled;
  int ignore_if_down_event;
  struct rfkill_data* rfkill;
  struct wpa_driver_capa capa;
  u8* extended_capa;
  u8* extended_capa_mask;
  unsigned int extended_capa_len;
  int has_capability;
  // More internal data follows.
};

#endif  // VSOC_PLATFORM_SDK_AFTER(J)
