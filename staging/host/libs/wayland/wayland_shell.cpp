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

#include "host/libs/wayland/wayland_shell.h"

#include <android-base/logging.h>

#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <xdg-shell-unstable-v6-server-protocol.h>

namespace wayland {
namespace {


void zxdg_positioner_v6_destroy(wl_client*, wl_resource* positioner) {
  LOG(VERBOSE) << __FUNCTION__
               << " positioner=" << positioner;

  wl_resource_destroy(positioner);
}

void zxdg_positioner_v6_set_size(wl_client*,
                                 wl_resource* positioner,
                                 int32_t w,
                                 int32_t h) {
  LOG(VERBOSE) << __FUNCTION__
               << " positioner=" << positioner
               << " w=" << w
               << " h=" << h;
}

void zxdg_positioner_v6_set_anchor_rect(wl_client*,
                                        wl_resource* positioner,
                                        int32_t x,
                                        int32_t y,
                                        int32_t w,
                                        int32_t h) {
  LOG(VERBOSE) << __FUNCTION__
               << " positioner=" << positioner
               << " x=" << x
               << " y=" << y
               << " w=" << w
               << " h=" << h;
}

void zxdg_positioner_v6_set_anchor(wl_client*,
                                   wl_resource* positioner,
                                   uint32_t anchor) {
  LOG(VERBOSE) << __FUNCTION__
               << " positioner=" << positioner
               << " anchor=" << anchor;
}

void zxdg_positioner_v6_set_gravity(wl_client*,
                                    wl_resource* positioner,
                                    uint32_t gravity) {
  LOG(VERBOSE) << __FUNCTION__
               << " positioner=" << positioner
               << " gravity=" << gravity;
}

void zxdg_positioner_v6_set_constraint_adjustment(wl_client*,
                                                  wl_resource* positioner,
                                                  uint32_t adjustment) {
  LOG(VERBOSE) << __FUNCTION__
               << " positioner=" << positioner
               << " adjustment=" << adjustment;
}

void zxdg_positioner_v6_set_offset(wl_client*,
                                   wl_resource* positioner,
                                   int32_t x,
                                   int32_t y) {
  LOG(VERBOSE) << __FUNCTION__
               << " positioner=" << positioner
               << " x=" << x
               << " y=" << y;
}

const struct zxdg_positioner_v6_interface
    zxdg_positioner_v6_implementation = {
        .destroy = zxdg_positioner_v6_destroy,
        .set_size = zxdg_positioner_v6_set_size,
        .set_anchor_rect = zxdg_positioner_v6_set_anchor_rect,
        .set_anchor = zxdg_positioner_v6_set_anchor,
        .set_gravity = zxdg_positioner_v6_set_gravity,
        .set_constraint_adjustment = zxdg_positioner_v6_set_constraint_adjustment,
        .set_offset = zxdg_positioner_v6_set_offset};

void zxdg_toplevel_v6_destroy(wl_client*, wl_resource* toplevel) {
  LOG(VERBOSE) << __FUNCTION__
               << " toplevel=" << toplevel;

  wl_resource_destroy(toplevel);
}

void zxdg_toplevel_v6_set_parent(wl_client*,
                                 wl_resource* toplevel,
                                 wl_resource* parent_toplevel) {
  LOG(VERBOSE) << __FUNCTION__
               << " toplevel=" << toplevel
               << " parent_toplevel=" << parent_toplevel;
}

void zxdg_toplevel_v6_set_title(wl_client*,
                                wl_resource* toplevel,
                                const char* title) {
  LOG(VERBOSE) << __FUNCTION__
               << " toplevel=" << toplevel
               << " title=" << title;
}

void zxdg_toplevel_v6_set_app_id(wl_client*,
                                 wl_resource* toplevel,
                                 const char* app) {
  LOG(VERBOSE) << __FUNCTION__
               << " toplevel=" << toplevel
               << " app=" << app;
}

void zxdg_toplevel_v6_show_window_menu(wl_client*,
                                       wl_resource* toplevel,
                                       wl_resource* seat,
                                       uint32_t serial,
                                       int32_t x,
                                       int32_t y) {
  LOG(VERBOSE) << __FUNCTION__
               << " toplevel=" << toplevel
               << " seat=" << seat
               << " serial=" << serial
               << " x=" << x
               << " y=" << y;
}

void zxdg_toplevel_v6_move(wl_client*,
                           wl_resource* toplevel,
                           wl_resource* seat,
                           uint32_t serial) {
  LOG(VERBOSE) << __FUNCTION__
               << " toplevel=" << toplevel
               << " seat=" << seat
               << " serial=" << serial;
}

void zxdg_toplevel_v6_resize(wl_client*,
                             wl_resource* toplevel,
                             wl_resource* seat,
                             uint32_t serial,
                             uint32_t edges) {
  LOG(VERBOSE) << __FUNCTION__
               << " toplevel=" << toplevel
               << " seat=" << seat
               << " serial=" << serial
               << " edges=" << edges;
}

void zxdg_toplevel_v6_set_max_size(wl_client*,
                                   wl_resource* toplevel,
                                   int32_t w,
                                   int32_t h) {
  LOG(VERBOSE) << __FUNCTION__
               << " toplevel=" << toplevel
               << " w=" << w
               << " h=" << h;
}

void zxdg_toplevel_v6_set_min_size(wl_client*,
                                   wl_resource* toplevel,
                                   int32_t w,
                                   int32_t h) {
  LOG(VERBOSE) << __FUNCTION__
               << " toplevel=" << toplevel
               << " w=" << w
               << " h=" << h;
}

void zxdg_toplevel_v6_set_maximized(wl_client*, wl_resource* toplevel) {
  LOG(VERBOSE) << __FUNCTION__
               << " toplevel=" << toplevel;
}

void zxdg_toplevel_v6_unset_maximized(wl_client*, wl_resource* toplevel) {
  LOG(VERBOSE) << __FUNCTION__
               << " toplevel=" << toplevel;
}

void zxdg_toplevel_v6_set_fullscreen(wl_client*,
                                     wl_resource* toplevel,
                                     wl_resource*) {
  LOG(VERBOSE) << __FUNCTION__
               << " toplevel=" << toplevel;
}

void zxdg_toplevel_v6_unset_fullscreen(wl_client*, wl_resource* toplevel) {
  LOG(VERBOSE) << __FUNCTION__
               << " toplevel=" << toplevel;
}

void zxdg_toplevel_v6_set_minimized(wl_client*, wl_resource* toplevel) {
  LOG(VERBOSE) << __FUNCTION__
               << " toplevel=" << toplevel;
}

const struct zxdg_toplevel_v6_interface zxdg_toplevel_v6_implementation = {
  .destroy = zxdg_toplevel_v6_destroy,
  .set_parent = zxdg_toplevel_v6_set_parent,
  .set_title = zxdg_toplevel_v6_set_title,
  .set_app_id = zxdg_toplevel_v6_set_app_id,
  .show_window_menu = zxdg_toplevel_v6_show_window_menu,
  .move = zxdg_toplevel_v6_move,
  .resize = zxdg_toplevel_v6_resize,
  .set_max_size = zxdg_toplevel_v6_set_max_size,
  .set_min_size = zxdg_toplevel_v6_set_min_size,
  .set_maximized = zxdg_toplevel_v6_set_maximized,
  .unset_maximized = zxdg_toplevel_v6_unset_maximized,
  .set_fullscreen = zxdg_toplevel_v6_set_fullscreen,
  .unset_fullscreen = zxdg_toplevel_v6_unset_fullscreen,
  .set_minimized = zxdg_toplevel_v6_set_minimized
};

void zxdg_popup_v6_destroy(wl_client*, wl_resource* popup) {
  LOG(VERBOSE) << __FUNCTION__
               << " popup=" << popup;

  wl_resource_destroy(popup);
}

void zxdg_popup_v6_grab(wl_client*,
                        wl_resource* popup,
                        wl_resource* seat,
                        uint32_t serial) {
  LOG(VERBOSE) << __FUNCTION__
               << " popup=" << popup
               << " seat=" << seat
               << " serial=" << serial;
}

const struct zxdg_popup_v6_interface zxdg_popup_v6_implementation = {
    .destroy = zxdg_popup_v6_destroy,
    .grab = zxdg_popup_v6_grab
};

void zxdg_surface_v6_destroy(wl_client*, wl_resource* surface) {
  LOG(VERBOSE) << __FUNCTION__
               << " surface=" << surface;

  wl_resource_destroy(surface);
}

void toplevel_destroy_resource_callback(struct wl_resource*) {}

void zxdg_surface_v6_get_toplevel(wl_client* client,
                                  wl_resource* surface,
                                  uint32_t id) {
  LOG(VERBOSE) << __FUNCTION__
               << " surface=" << surface
               << " id=" << id;

  wl_resource* xdg_toplevel_resource =
      wl_resource_create(client, &zxdg_toplevel_v6_interface, 1, id);

  wl_resource_set_implementation(xdg_toplevel_resource,
                                 &zxdg_toplevel_v6_implementation, nullptr,
                                 toplevel_destroy_resource_callback);
}

void popup_destroy_resource_callback(struct wl_resource*) {}

void zxdg_surface_v6_get_popup(wl_client* client,
                              wl_resource* surface,
                              uint32_t id,
                              wl_resource* parent_surface,
                              wl_resource* positioner) {
  LOG(VERBOSE) << __FUNCTION__
               << " surface=" << surface
               << " id=" << id
               << " parent_surface=" << parent_surface
               << " positioner=" << positioner;

  wl_resource* xdg_popup_resource =
      wl_resource_create(client, &zxdg_popup_v6_interface, 1, id);

  wl_resource_set_implementation(xdg_popup_resource,
                                 &zxdg_popup_v6_implementation, nullptr,
                                 popup_destroy_resource_callback);
}

void zxdg_surface_v6_set_window_geometry(wl_client*,
                                         wl_resource* surface,
                                         int32_t x,
                                         int32_t y,
                                         int32_t w,
                                         int32_t h) {
  LOG(VERBOSE) << __FUNCTION__
               << " surface=" << surface
               << " x=" << x
               << " y=" << y
               << " w=" << w
               << " h=" << h;
}

void zxdg_surface_v6_ack_configure(wl_client*,
                                   wl_resource* surface,
                                   uint32_t serial) {
  LOG(VERBOSE) << __FUNCTION__
               << " surface=" << surface
               << " serial=" << serial;
}

const struct zxdg_surface_v6_interface zxdg_surface_v6_implementation = {
    .destroy = zxdg_surface_v6_destroy,
    .get_toplevel = zxdg_surface_v6_get_toplevel,
    .get_popup = zxdg_surface_v6_get_popup,
    .set_window_geometry = zxdg_surface_v6_set_window_geometry,
    .ack_configure = zxdg_surface_v6_ack_configure
};

void zxdg_shell_v6_destroy(wl_client*, wl_resource* shell) {
  LOG(VERBOSE) << __FUNCTION__
               << " shell=" << shell;

  wl_resource_destroy(shell);
}

void positioner_destroy_resource_callback(struct wl_resource*) {}

void zxdg_shell_v6_create_positioner(wl_client* client,
                                     wl_resource* shell,
                                     uint32_t id) {
  LOG(VERBOSE) << __FUNCTION__
               << " shell=" << shell
               << " id=" << id;

  wl_resource* positioner_resource =
      wl_resource_create(client, &zxdg_positioner_v6_interface, 1, id);

  wl_resource_set_implementation(positioner_resource,
                                 &zxdg_positioner_v6_implementation, nullptr,
                                 positioner_destroy_resource_callback);
}

void surface_destroy_resource_callback(struct wl_resource*) {}

void zxdg_shell_v6_get_xdg_surface(wl_client* client,
                                   wl_resource* shell,
                                   uint32_t id,
                                   wl_resource* surface) {
  LOG(VERBOSE) << __FUNCTION__
               << " shell=" << shell
               << " id=" << id
               << " surface=" << surface;

  wl_resource* surface_resource =
      wl_resource_create(client, &zxdg_surface_v6_interface, 1, id);

  wl_resource_set_implementation(surface_resource,
                                 &zxdg_surface_v6_implementation, nullptr,
                                 surface_destroy_resource_callback);
}

void zxdg_shell_v6_pong(wl_client*, wl_resource* shell, uint32_t serial) {
  LOG(VERBOSE) << __FUNCTION__
               << " shell=" << shell
               << " serial=" << serial;
}

const struct zxdg_shell_v6_interface zxdg_shell_v6_implementation = {
    .destroy = zxdg_shell_v6_destroy,
    .create_positioner = zxdg_shell_v6_create_positioner,
    .get_xdg_surface = zxdg_shell_v6_get_xdg_surface,
    .pong = zxdg_shell_v6_pong
};

void bind_shell(wl_client* client, void* data, uint32_t version, uint32_t id) {
  wl_resource* shell_resource =
      wl_resource_create(client, &zxdg_shell_v6_interface, version, id);

  wl_resource_set_implementation(shell_resource,
                                 &zxdg_shell_v6_implementation, data, nullptr);
}

}  // namespace

void BindShellInterface(wl_display* display) {
  wl_global_create(display, &zxdg_shell_v6_interface, 1, nullptr, bind_shell);
}

}  // namespace wayland