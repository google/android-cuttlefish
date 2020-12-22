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

#include "host/libs/wayland/wayland_compositor.h"

#include <android-base/logging.h>

#include <wayland-server-core.h>
#include <wayland-server-protocol.h>

#include "host/libs/wayland/wayland_utils.h"

namespace wayland {
namespace {

void region_destroy(wl_client*, wl_resource* region_resource) {
  LOG(VERBOSE) << __FUNCTION__
               << " region=" << region_resource;

  wl_resource_destroy(region_resource);
}

void region_add(wl_client*,
                wl_resource* region_resource,
                int32_t x,
                int32_t y,
                int32_t w,
                int32_t h) {
  LOG(VERBOSE) << __FUNCTION__
               << " region=" << region_resource
               << " x=" << x
               << " y=" << y
               << " w=" << w
               << " h=" << h;

  Surface::Region* region = GetUserData<Surface::Region>(region_resource);
  region->x = x;
  region->y = y;
  region->w = w;
  region->h = h;
}

void region_subtract(wl_client*,
                     wl_resource* region_resource,
                     int32_t x,
                     int32_t y,
                     int32_t w,
                     int32_t h) {
  LOG(VERBOSE) << __FUNCTION__
               << " region=" << region_resource
               << " x=" << x
               << " y=" << y
               << " w=" << w
               << " h=" << h;
}

const struct wl_region_interface region_implementation = {
    .destroy = region_destroy,
    .add = region_add,
    .subtract = region_subtract,
};

void surface_destroy(wl_client*, wl_resource* surface) {
  LOG(VERBOSE) << __FUNCTION__
               << " surface=" << surface;
}

void surface_attach(wl_client*,
                    wl_resource* surface,
                    wl_resource* buffer,
                    int32_t x,
                    int32_t y) {
  LOG(VERBOSE) << __FUNCTION__
               << " surface=" << surface
               << " buffer=" << buffer
               << " x=" << x
               << " y=" << y;

  GetUserData<Surface>(surface)->Attach(buffer);
}

void surface_damage(wl_client*,
                    wl_resource* surface_resource,
                    int32_t x,
                    int32_t y,
                    int32_t w,
                    int32_t h) {
  LOG(VERBOSE) << __FUNCTION__
               << " surface=" << surface_resource
               << " x=" << x
               << " y=" << y
               << " w=" << w
               << " h=" << h;
}

void surface_frame(wl_client*, wl_resource* surface, uint32_t) {
  LOG(VERBOSE) << " surface=" << surface;
}

void surface_set_opaque_region(wl_client*,
                               wl_resource* surface_resource,
                               wl_resource* region_resource) {
  LOG(VERBOSE) << __FUNCTION__
               << " surface=" << surface_resource
               << " region=" << region_resource;

  Surface* surface = GetUserData<Surface>(surface_resource);
  Surface::Region* region = GetUserData<Surface::Region>(region_resource);

  surface->SetRegion(*region);
}

void surface_set_input_region(wl_client*,
                              wl_resource* surface_resource,
                              wl_resource* region_resource) {
  LOG(VERBOSE) << __FUNCTION__
               << " surface=" << surface_resource
               << " region=" << region_resource;
}

void surface_commit(wl_client*, wl_resource* surface_resource) {
  LOG(VERBOSE) << __FUNCTION__
               << " surface=" << surface_resource;

  GetUserData<Surface>(surface_resource)->Commit();
}

void surface_set_buffer_transform(wl_client*,
                                  wl_resource* surface_resource,
                                  int32_t transform) {
  LOG(VERBOSE) << __FUNCTION__
               << " surface=" << surface_resource
               << " transform=" << transform;
}

void surface_set_buffer_scale(wl_client*,
                              wl_resource* surface_resource,
                              int32_t scale) {
  LOG(VERBOSE) << __FUNCTION__
               << " surface=" << surface_resource
               << " scale=" << scale;
}

void surface_damage_buffer(wl_client*,
                           wl_resource* surface_resource,
                           int32_t x,
                           int32_t y,
                           int32_t w,
                           int32_t h) {
  LOG(VERBOSE) << __FUNCTION__
               << " surface=" << surface_resource
               << " x=" << x
               << " y=" << y
               << " w=" << w
               << " h=" << h;
}

const struct wl_surface_interface surface_implementation = {
  .destroy = surface_destroy,
  .attach = surface_attach,
  .damage = surface_damage,
  .frame = surface_frame,
  .set_opaque_region = surface_set_opaque_region,
  .set_input_region = surface_set_input_region,
  .commit = surface_commit,
  .set_buffer_transform = surface_set_buffer_transform,
  .set_buffer_scale = surface_set_buffer_scale,
  .damage_buffer = surface_damage_buffer,
};

void surface_destroy_resource_callback(struct wl_resource*) {}

void compositor_create_surface(wl_client* client,
                               wl_resource* compositor,
                               uint32_t id) {
  LOG(VERBOSE) << __FUNCTION__
               << " compositor=" << compositor
               << " id=" << id;

  Surface* surface = GetUserData<Surface>(compositor);

  wl_resource* surface_resource = wl_resource_create(
      client, &wl_surface_interface, wl_resource_get_version(compositor), id);

  wl_resource_set_implementation(surface_resource, &surface_implementation,
                                 surface, surface_destroy_resource_callback);
}

void compositor_create_region(wl_client* client,
                              wl_resource* compositor,
                              uint32_t id) {
  LOG(VERBOSE) << __FUNCTION__
               << " compositor=" << compositor
               << " id=" << id;

  std::unique_ptr<Surface::Region> region(new Surface::Region());

  wl_resource* region_resource = wl_resource_create(
      client, &wl_region_interface, 1, id);

  wl_resource_set_implementation(region_resource, &region_implementation,
                                 region.release(),
                                 DestroyUserData<Surface::Region>);
}

const struct wl_compositor_interface compositor_implementation = {
    .create_surface = compositor_create_surface,
    .create_region = compositor_create_region,
};

constexpr const uint32_t kCompositorVersion = 3;

void compositor_destroy_resource_callback(struct wl_resource*) {}

void bind_compositor(wl_client* client,
                     void* data,
                     uint32_t version,
                     uint32_t id) {
  wl_resource* resource =
      wl_resource_create(client, &wl_compositor_interface,
                         std::min(version, kCompositorVersion), id);

  wl_resource_set_implementation(resource, &compositor_implementation, data,
                                 compositor_destroy_resource_callback);
}

}  // namespace

void BindCompositorInterface(wl_display* display, Surface* surface) {
  wl_global_create(display, &wl_compositor_interface, kCompositorVersion,
                   surface, bind_compositor);
}

}  // namespace wayland
