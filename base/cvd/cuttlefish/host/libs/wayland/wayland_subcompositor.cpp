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

#include "host/libs/wayland/wayland_subcompositor.h"

#include <android-base/logging.h>

#include <wayland-server-core.h>
#include <wayland-server-protocol.h>

namespace wayland {
namespace {

void subsurface_destroy(wl_client*, wl_resource* subsurface) {
  LOG(VERBOSE) << " subsurface=" << subsurface;

  wl_resource_destroy(subsurface);
}

void subsurface_set_position(wl_client*,
                             wl_resource* subsurface,
                             int32_t x,
                             int32_t y) {
  LOG(VERBOSE) << __FUNCTION__
               << " subsurface=" << subsurface
               << " x=" << x
               << " y=" << y;
}

void subsurface_place_above(wl_client*,
                            wl_resource* subsurface,
                            wl_resource* surface) {
  LOG(VERBOSE) << __FUNCTION__
               << " subsurface=" << subsurface
               << " surface=" << surface;
}

void subsurface_place_below(wl_client*,
                            wl_resource* subsurface,
                            wl_resource* surface) {
  LOG(VERBOSE) << __FUNCTION__
               << " subsurface=" << subsurface
               << " surface=" << surface;
}

void subsurface_set_sync(wl_client*, wl_resource* subsurface) {
  LOG(VERBOSE) << __FUNCTION__
               << " subsurface=" << subsurface;
}

void subsurface_set_desync(wl_client*, wl_resource* subsurface) {
  LOG(VERBOSE) << __FUNCTION__
               << " subsurface=" << subsurface;
}

void subsurface_destroy_resource_callback(struct wl_resource*) {}

const struct wl_subsurface_interface subsurface_implementation = {
    .destroy = subsurface_destroy,
    .set_position = subsurface_set_position,
    .place_above = subsurface_place_above,
    .place_below = subsurface_place_below,
    .set_sync = subsurface_set_sync,
    .set_desync = subsurface_set_desync,
};

void subcompositor_destroy(wl_client*, wl_resource* subcompositor) {
  LOG(VERBOSE) << __FUNCTION__
               << " subcompositor=" << subcompositor;

  wl_resource_destroy(subcompositor);
}

void subcompositor_get_subsurface(wl_client* client,
                                  wl_resource* display,
                                  uint32_t id,
                                  wl_resource* surface,
                                  wl_resource* parent_surface) {
  LOG(VERBOSE) << __FUNCTION__
               << " display=" << display
               << " surface=" << surface
               << " parent_surface=" << parent_surface;

  wl_resource* subsurface_resource =
      wl_resource_create(client, &wl_subsurface_interface, 1, id);

  wl_resource_set_implementation(subsurface_resource,
                                 &subsurface_implementation, nullptr,
                                 subsurface_destroy_resource_callback);
}

const struct wl_subcompositor_interface subcompositor_implementation = {
    .destroy = subcompositor_destroy,
    .get_subsurface = subcompositor_get_subsurface,
};

void subcompositor_destroy_resource_callback(struct wl_resource*) {}

void bind_subcompositor(wl_client* client,
                        void* data,
                        uint32_t version,
                        uint32_t id) {
  wl_resource* resource =
      wl_resource_create(client, &wl_subcompositor_interface, version, id);

  wl_resource_set_implementation(resource, &subcompositor_implementation, data,
                                 subcompositor_destroy_resource_callback);
}

}  // namespace

void BindSubcompositorInterface(wl_display* display) {
  wl_global_create(display, &wl_subcompositor_interface, 1, nullptr,
                   bind_subcompositor);
}

}  // namespace wayland