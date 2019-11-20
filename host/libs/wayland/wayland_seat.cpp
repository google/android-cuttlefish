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

#include "host/libs/wayland/wayland_seat.h"

#include <android-base/logging.h>

#include <wayland-server-core.h>
#include <wayland-server-protocol.h>

namespace wayland {
namespace {

void pointer_set_cursor(wl_client*,
                        wl_resource* pointer,
                        uint32_t serial,
                        wl_resource* surface,
                        int32_t x,
                        int32_t y) {
  LOG(VERBOSE) << __FUNCTION__
               << " pointer=" << pointer
               << " serial=" << serial
               << " surface=" << surface
               << " x=" << x
               << " y=" << y;
}

void pointer_release(wl_client*, wl_resource* pointer) {
  LOG(VERBOSE) << __FUNCTION__
               << " pointer=" << pointer;

  wl_resource_destroy(pointer);
}

const struct wl_pointer_interface pointer_implementation = {
  .set_cursor = pointer_set_cursor,
  .release = pointer_release
};

void keyboard_release(wl_client*, wl_resource* keyboard) {
  LOG(VERBOSE) << __FUNCTION__
               << " keyboard=" << keyboard;

  wl_resource_destroy(keyboard);
}

const struct wl_keyboard_interface keyboard_implementation = {
  .release = keyboard_release
};

void touch_release(wl_client*, wl_resource* touch) {
  LOG(VERBOSE) << __FUNCTION__
               << " touch=" << touch;

  wl_resource_destroy(touch);
}

const struct wl_touch_interface touch_implementation = {
  .release = touch_release
};

void pointer_destroy_resource_callback(struct wl_resource*) {}

void seat_get_pointer(wl_client* client, wl_resource* seat, uint32_t id) {
  LOG(VERBOSE) << __FUNCTION__
               << " seat=" << seat
               << " id=" << id;

  wl_resource* pointer_resource =
      wl_resource_create(client, &wl_pointer_interface,
                         wl_resource_get_version(seat), id);

  wl_resource_set_implementation(pointer_resource, &pointer_implementation,
                                 nullptr, pointer_destroy_resource_callback);
}

void keyboard_destroy_resource_callback(struct wl_resource*) {}

void seat_get_keyboard(wl_client* client, wl_resource* seat, uint32_t id) {
  LOG(VERBOSE) << __FUNCTION__
               << " seat=" << seat
               << " id=" << id;

  wl_resource* keyboard_resource =
      wl_resource_create(client, &wl_keyboard_interface,
                         wl_resource_get_version(seat), id);

  wl_resource_set_implementation(keyboard_resource, &keyboard_implementation,
                                 nullptr, keyboard_destroy_resource_callback);
}

void touch_destroy_resource_callback(struct wl_resource*) {}

void seat_get_touch(wl_client* client, wl_resource* seat, uint32_t id) {
  LOG(VERBOSE) << __FUNCTION__
               << " seat=" << seat
               << " id=" << id;

  wl_resource* touch_resource =
      wl_resource_create(client, &wl_touch_interface,
                         wl_resource_get_version(seat), id);

  wl_resource_set_implementation(touch_resource, &touch_implementation, nullptr,
                                 touch_destroy_resource_callback);
}

void seat_release(wl_client*, wl_resource* resource) {
  LOG(VERBOSE) << __FUNCTION__
               << " resource=" << resource;

  wl_resource_destroy(resource);
}

constexpr const uint32_t kSeatVersion = 6;

const struct wl_seat_interface seat_implementation = {
    .get_pointer = seat_get_pointer,
    .get_keyboard = seat_get_keyboard,
    .get_touch = seat_get_touch,
    .release = seat_release
};

void bind_seat(wl_client* client, void* data, uint32_t version, uint32_t id) {
  wl_resource* resource =
      wl_resource_create(client, &wl_seat_interface,
                         std::min(version, kSeatVersion), id);

  wl_resource_set_implementation(resource, &seat_implementation, data, nullptr);
}

}  // namespace

void BindSeatInterface(wl_display* display) {
  wl_global_create(display, &wl_seat_interface, kSeatVersion, nullptr,
                   bind_seat);
}

}  // namespace wayland