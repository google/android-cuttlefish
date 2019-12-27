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

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <thread>

namespace wayland {

namespace internal {
struct WaylandServerState;
}  // namespace internal

using FrameCallback = std::function<void(std::uint32_t /*frame_number*/,
                                         std::uint8_t* /*frame_pixels*/)>;

// A Wayland compositing server that provides an interface for receiving frame
// updates from a connected client.
class WaylandServer {
  public:
    // Creates a Wayland compositing server. If specified, uses the given
    // socket file descriptor to connect with clients. If provided, this
    // server will close the file descriptor upon exit.
    WaylandServer(int wayland_socket_fd = -1);
    virtual ~WaylandServer();

    WaylandServer(const WaylandServer& rhs) = delete;
    WaylandServer& operator=(const WaylandServer& rhs) = delete;

    WaylandServer(WaylandServer&& rhs) = delete;
    WaylandServer& operator=(WaylandServer&& rhs) = delete;

    // Registers a callback to run on the next frame available after the given
    // frame number.
    std::future<void> OnFrameAfter(std::uint32_t frame_number,
                                   const FrameCallback& frame_callback);

  private:
    void ServerLoop(int wayland_socket_fd);

    bool server_ready_ = false;
    std::mutex server_ready_mutex_;
    std::condition_variable server_ready_cv_;

    std::thread server_thread_;
    std::unique_ptr<internal::WaylandServerState> server_state_;
};

}  // namespace wayland