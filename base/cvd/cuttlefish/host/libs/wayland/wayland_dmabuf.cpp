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

#include "cuttlefish/host/libs/wayland/wayland_dmabuf.h"

#include <errno.h>
#include <linux/dma-buf.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <memory>
#include <utility>

#include "absl/log/log.h"
#include "drm/drm_fourcc.h"
#include "linux-dmabuf-unstable-v1-server-protocol.h"
#include "wayland-server-core.h"
#include "wayland-server-protocol.h"

#include "cuttlefish/host/libs/wayland/wayland_utils.h"

namespace wayland {

DmabufBuffer::~DmabufBuffer() {
  if (mapped_ptr != nullptr && mapped_ptr != MAP_FAILED && mapped_size > 0) {
    munmap(mapped_ptr, mapped_size);
    mapped_ptr = nullptr;
    mapped_size = 0;
  }
}

namespace {

struct DmabufParams {
  android::base::unique_fd fd;
  uint32_t offset = 0;
  uint32_t stride = 0;
  uint64_t modifier = 0;
  bool has_plane0 = false;
};

void buffer_destroy(wl_client*, wl_resource* buffer) {
  VLOG(1) << __FUNCTION__ << " buffer=" << buffer;

  wl_resource_destroy(buffer);
}

const struct wl_buffer_interface buffer_implementation = {
    .destroy = buffer_destroy,
};

void linux_buffer_params_destroy(wl_client*, wl_resource* params) {
  VLOG(1) << __FUNCTION__ << " params=" << params;

  wl_resource_destroy(params);
}

void linux_buffer_params_add(wl_client*, wl_resource* params, int32_t fd,
                             uint32_t plane, uint32_t offset, uint32_t stride,
                             uint32_t modifier_hi, uint32_t modifier_lo) {
  VLOG(1) << __FUNCTION__ << " params=" << params << " fd=" << fd
          << " plane=" << plane << " offset=" << offset << " stride=" << stride
          << " mod_hi=" << modifier_hi << " mod_lo=" << modifier_lo;

  android::base::unique_fd plane_fd(fd);
  DmabufParams* dmabuf_params = GetUserData<DmabufParams>(params);
  if (plane == 0) {
    dmabuf_params->fd = std::move(plane_fd);
    dmabuf_params->offset = offset;
    dmabuf_params->stride = stride;
    dmabuf_params->modifier =
        (static_cast<uint64_t>(modifier_hi) << 32) | modifier_lo;
    dmabuf_params->has_plane0 = true;
  }
}

std::unique_ptr<DmabufBuffer> CreateDmabufBuffer(DmabufParams* dmabuf_params,
                                                 int32_t w, int32_t h,
                                                 uint32_t format,
                                                 uint32_t flags) {
  if (!dmabuf_params || !dmabuf_params->has_plane0 || !dmabuf_params->fd.ok() ||
      w <= 0 || h <= 0 || dmabuf_params->stride == 0) {
    LOG(ERROR) << "Invalid dmabuf params: w=" << w << " h=" << h
               << " stride=" << (dmabuf_params ? dmabuf_params->stride : 0);
    return nullptr;
  }

  size_t min_size =
      static_cast<size_t>(dmabuf_params->offset) +
      static_cast<size_t>(dmabuf_params->stride) * static_cast<size_t>(h);
  off_t fd_size = lseek(dmabuf_params->fd.get(), 0, SEEK_END);
  size_t map_size = (fd_size > 0 && static_cast<size_t>(fd_size) >= min_size)
                        ? static_cast<size_t>(fd_size)
                        : min_size;

  void* mapped = mmap(nullptr, map_size, PROT_READ, MAP_SHARED,
                      dmabuf_params->fd.get(), 0);
  if (mapped == MAP_FAILED) {
    PLOG(ERROR) << "Failed to mmap dmabuf fd=" << dmabuf_params->fd.get()
                << " size=" << map_size;
    return nullptr;
  }

  auto buffer = std::make_unique<DmabufBuffer>();
  buffer->fd = std::move(dmabuf_params->fd);
  buffer->width = w;
  buffer->height = h;
  buffer->format = format;
  buffer->flags = flags;
  buffer->offset = dmabuf_params->offset;
  buffer->stride = dmabuf_params->stride;
  buffer->modifier = dmabuf_params->modifier;
  buffer->mapped_ptr = static_cast<uint8_t*>(mapped);
  buffer->mapped_size = map_size;
  return buffer;
}

void linux_buffer_params_create(wl_client* client, wl_resource* params,
                                int32_t w, int32_t h, uint32_t format,
                                uint32_t flags) {
  VLOG(1) << __FUNCTION__ << " params=" << params << " w=" << w << " h=" << h
          << " format=" << format << " flags=" << flags;

  DmabufParams* dmabuf_params = GetUserData<DmabufParams>(params);
  auto dmabuf_buffer = CreateDmabufBuffer(dmabuf_params, w, h, format, flags);
  if (!dmabuf_buffer) {
    zwp_linux_buffer_params_v1_send_failed(params);
    return;
  }

  wl_resource* buffer_resource =
      wl_resource_create(client, &wl_buffer_interface, 1, 0);
  if (!buffer_resource) {
    wl_resource_post_no_memory(params);
    return;
  }

  wl_resource_set_implementation(buffer_resource, &buffer_implementation,
                                 dmabuf_buffer.release(),
                                 DestroyUserData<DmabufBuffer>);
  zwp_linux_buffer_params_v1_send_created(params, buffer_resource);
}

void linux_buffer_params_create_immed(wl_client* client, wl_resource* params,
                                      uint32_t id, int32_t w, int32_t h,
                                      uint32_t format, uint32_t flags) {
  VLOG(1) << __FUNCTION__ << " params=" << params << " id=" << id << " w=" << w
          << " h=" << h << " format=" << format << " flags=" << flags;

  DmabufParams* dmabuf_params = GetUserData<DmabufParams>(params);
  auto dmabuf_buffer = CreateDmabufBuffer(dmabuf_params, w, h, format, flags);
  if (!dmabuf_buffer) {
    wl_resource_post_error(params,
                           ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_WL_BUFFER,
                           "Failed to import dmabuf");
    return;
  }

  wl_resource* buffer_resource =
      wl_resource_create(client, &wl_buffer_interface, 1, id);
  if (!buffer_resource) {
    wl_resource_post_no_memory(params);
    return;
  }

  wl_resource_set_implementation(buffer_resource, &buffer_implementation,
                                 dmabuf_buffer.release(),
                                 DestroyUserData<DmabufBuffer>);
}

const struct zwp_linux_buffer_params_v1_interface
    zwp_linux_buffer_params_implementation = {
        .destroy = linux_buffer_params_destroy,
        .add = linux_buffer_params_add,
        .create = linux_buffer_params_create,
        .create_immed = linux_buffer_params_create_immed,
};

void linux_dmabuf_destroy(wl_client*, wl_resource* dmabuf) {
  VLOG(1) << __FUNCTION__ << " dmabuf=" << dmabuf;

  wl_resource_destroy(dmabuf);
}

void linux_dmabuf_create_params(wl_client* client, wl_resource* display,
                                uint32_t id) {
  VLOG(1) << __FUNCTION__ << " display=" << display << " id=" << id;

  DmabufParams* dmabuf_params = new DmabufParams();
  wl_resource* buffer_params_resource =
      wl_resource_create(client, &zwp_linux_buffer_params_v1_interface, 1, id);

  wl_resource_set_implementation(buffer_params_resource,
                                 &zwp_linux_buffer_params_implementation,
                                 dmabuf_params, DestroyUserData<DmabufParams>);
}

const struct zwp_linux_dmabuf_v1_interface zwp_linux_dmabuf_v1_implementation =
    {
        .destroy = linux_dmabuf_destroy,
        .create_params = linux_dmabuf_create_params,
};

constexpr uint32_t kLinuxDmabufVersion = 2;

void bind_linux_dmabuf(wl_client* client, void* data, uint32_t version,
                       uint32_t id) {
  wl_resource* resource =
      wl_resource_create(client, &zwp_linux_dmabuf_v1_interface,
                         std::min(version, kLinuxDmabufVersion), id);

  wl_resource_set_implementation(resource, &zwp_linux_dmabuf_v1_implementation,
                                 data, nullptr);

  zwp_linux_dmabuf_v1_send_format(resource, DRM_FORMAT_ARGB8888);
  zwp_linux_dmabuf_v1_send_format(resource, DRM_FORMAT_XRGB8888);
  zwp_linux_dmabuf_v1_send_format(resource, DRM_FORMAT_ABGR8888);
  zwp_linux_dmabuf_v1_send_format(resource, DRM_FORMAT_XBGR8888);
}

}  // namespace

DmabufBuffer* GetDmabufBuffer(wl_resource* buffer) {
  if (buffer == nullptr) {
    return nullptr;
  }
  if (!wl_resource_instance_of(buffer, &wl_buffer_interface,
                               &buffer_implementation)) {
    return nullptr;
  }
  return GetUserData<DmabufBuffer>(buffer);
}

void DmabufBufferBeginAccess(DmabufBuffer* buffer) {
  if (!buffer || !buffer->fd.ok()) {
    return;
  }
  struct dma_buf_sync sync = {
      .flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_READ,
  };
  TEMP_FAILURE_RETRY(ioctl(buffer->fd.get(), DMA_BUF_IOCTL_SYNC, &sync));
}

void DmabufBufferEndAccess(DmabufBuffer* buffer) {
  if (!buffer || !buffer->fd.ok()) {
    return;
  }
  struct dma_buf_sync sync = {
      .flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_READ,
  };
  TEMP_FAILURE_RETRY(ioctl(buffer->fd.get(), DMA_BUF_IOCTL_SYNC, &sync));
}

void BindDmabufInterface(wl_display* display) {
  wl_global_create(display, &zwp_linux_dmabuf_v1_interface, kLinuxDmabufVersion,
                   nullptr, bind_linux_dmabuf);
}

}  // namespace wayland
