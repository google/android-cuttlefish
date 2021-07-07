/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include <virtio-gpu-metadata-v1.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>

#include "host/libs/wayland/wayland_surface.h"
#include "host/libs/wayland/wayland_utils.h"

namespace wayland {
namespace {

void virtio_gpu_surface_metadata_set_scanout_id(
    struct wl_client*, struct wl_resource* surface_metadata_resource,
    uint32_t scanout_id) {
  GetUserData<Surface>(surface_metadata_resource)
      ->SetVirtioGpuScanoutId(scanout_id);
}

const struct wp_virtio_gpu_surface_metadata_v1_interface
    virtio_gpu_surface_metadata_implementation = {
        .set_scanout_id = virtio_gpu_surface_metadata_set_scanout_id};

void destroy_virtio_gpu_surface_metadata_resource_callback(
    struct wl_resource*) {
  // This is only expected to occur upon surface destruction so no need to
  // update the scanout id in `Surface`.
}

void virtio_gpu_metadata_get_surface_metadata(
    struct wl_client* client, struct wl_resource* /*metadata_impl_resource*/,
    uint32_t id, struct wl_resource* surface_resource) {
  Surface* surface = GetUserData<Surface>(surface_resource);

  wl_resource* virtio_gpu_metadata_surface_resource = wl_resource_create(
      client, &wp_virtio_gpu_surface_metadata_v1_interface, 1, id);

  wl_resource_set_implementation(
      virtio_gpu_metadata_surface_resource,
      &virtio_gpu_surface_metadata_implementation, surface,
      destroy_virtio_gpu_surface_metadata_resource_callback);
}

const struct wp_virtio_gpu_metadata_v1_interface
    virtio_gpu_metadata_implementation = {
        .get_surface_metadata = virtio_gpu_metadata_get_surface_metadata,
};

void destroy_virtio_gpu_metadata_resource_callback(struct wl_resource*) {}

void bind_virtio_gpu_metadata(wl_client* client, void* data,
                              uint32_t /*version*/, uint32_t id) {
  wl_resource* resource =
      wl_resource_create(client, &wp_virtio_gpu_metadata_v1_interface, 1, id);

  wl_resource_set_implementation(resource, &virtio_gpu_metadata_implementation,
                                 data,
                                 destroy_virtio_gpu_metadata_resource_callback);
}

}  // namespace

void BindVirtioGpuMetadataInterface(wl_display* display, Surfaces* surfaces) {
  wl_global_create(display, &wp_virtio_gpu_metadata_v1_interface, 1, surfaces,
                   bind_virtio_gpu_metadata);
}

}  // namespace wayland