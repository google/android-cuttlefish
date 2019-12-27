/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include "host/libs/wayland/wayland_dmabuf.h"

#include <android-base/logging.h>

#include <drm_fourcc.h>

#include <linux-dmabuf-unstable-v1-server-protocol.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>

namespace wayland {
namespace {

void buffer_destroy(wl_client*, wl_resource* buffer) {
  LOG(VERBOSE) << __FUNCTION__
               << " buffer=" << buffer;

  wl_resource_destroy(buffer);
}

const struct wl_buffer_interface buffer_implementation = {
    .destroy = buffer_destroy
};

void linux_buffer_params_destroy(wl_client*, wl_resource* params) {
  LOG(VERBOSE) << __FUNCTION__
               << " params=" << params;

  wl_resource_destroy(params);
}

void params_destroy_resource_callback(struct wl_resource*) {}

void linux_buffer_params_add(wl_client*,
                             wl_resource* params,
                             int32_t fd,
                             uint32_t plane,
                             uint32_t offset,
                             uint32_t stride,
                             uint32_t modifier_hi,
                             uint32_t modifier_lo) {
  LOG(VERBOSE) << __FUNCTION__
               << " params=" << params
               << " fd=" << fd
               << " plane=" << plane
               << " offset=" << offset
               << " stride=" << stride
               << " mod_hi=" << modifier_hi
               << " mod_lo=" << modifier_lo;
}

void linux_buffer_params_create(wl_client* client,
                                wl_resource* params,
                                int32_t w,
                                int32_t h,
                                uint32_t format,
                                uint32_t flags) {
  LOG(VERBOSE) << __FUNCTION__
               << " params=" << params
               << " w=" << w
               << " h=" << h
               << " format=" << format
               << " flags=" << flags;

  wl_resource* buffer_resource =
      wl_resource_create(client, &wl_buffer_interface, 1, 0);

  wl_resource_set_implementation(buffer_resource, &buffer_implementation,
                                 nullptr, params_destroy_resource_callback);
}

void linux_buffer_params_create_immed(wl_client* client,
                                      wl_resource* params,
                                      uint32_t id,
                                      int32_t w,
                                      int32_t h,
                                      uint32_t format,
                                      uint32_t flags) {
  LOG(VERBOSE) << __FUNCTION__
               << " params=" << params
               << " id=" << id
               << " w=" << w
               << " h=" << h
               << " format=" << format
               << " flags=" << flags;

  wl_resource* buffer_resource =
      wl_resource_create(client, &wl_buffer_interface, 1, id);

  wl_resource_set_implementation(buffer_resource, &buffer_implementation,
                                 nullptr, params_destroy_resource_callback);
}

const struct zwp_linux_buffer_params_v1_interface
    zwp_linux_buffer_params_implementation = {
        .destroy = linux_buffer_params_destroy,
        .add = linux_buffer_params_add,
        .create = linux_buffer_params_create,
        .create_immed = linux_buffer_params_create_immed};

void linux_dmabuf_destroy(wl_client*, wl_resource* dmabuf) {
  LOG(VERBOSE) << __FUNCTION__
               << " dmabuf=" << dmabuf;

  wl_resource_destroy(dmabuf);
}

void linux_dmabuf_create_params(wl_client* client,
                                wl_resource* display,
                                uint32_t id) {
  LOG(VERBOSE) << __FUNCTION__
               << " display=" << display
               << " id=" << id;

  wl_resource* buffer_params_resource =
      wl_resource_create(client, &zwp_linux_buffer_params_v1_interface, 1, id);

  wl_resource_set_implementation(buffer_params_resource,
                                 &zwp_linux_buffer_params_implementation,
                                 nullptr, params_destroy_resource_callback);
}

const struct zwp_linux_dmabuf_v1_interface
    zwp_linux_dmabuf_v1_implementation = {
        .destroy = linux_dmabuf_destroy,
        .create_params = linux_dmabuf_create_params};

constexpr uint32_t kLinuxDmabufVersion = 2;

void bind_linux_dmabuf(wl_client* client,
                       void* data,
                       uint32_t version,
                       uint32_t id) {
  wl_resource* resource =
      wl_resource_create(client, &zwp_linux_dmabuf_v1_interface,
                         std::min(version, kLinuxDmabufVersion), id);

  wl_resource_set_implementation(resource, &zwp_linux_dmabuf_v1_implementation,
                                 data, nullptr);

  zwp_linux_dmabuf_v1_send_format(resource, DRM_FORMAT_ARGB8888);
}

}  // namespace

void BindDmabufInterface(wl_display* display) {
  wl_global_create(display, &zwp_linux_dmabuf_v1_interface,
                   kLinuxDmabufVersion, nullptr, bind_linux_dmabuf);
}

}  // namespace wayland