/*
 * Copyright (C) 2015 The Android Open Source Project
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

#ifndef __ADB_SOCKET_H
#define __ADB_SOCKET_H

#include <stddef.h>

#include <deque>
#include <memory>
#include <optional>
#include <string>

#include "adb_unique_fd.h"
#include "fdevent/fdevent.h"
#include "types.h"

class atransport;

/* An asocket represents one half of a connection between a local and
   remote entity.  A local asocket is bound to a file descriptor.  A
   remote asocket is bound to the protocol engine (transport).

   Example of a Local Socket (LS) with undetermined peer:

                                  LOCAL SOCKET (THIS)                         TRANSPORT
                   ┌────────────────────────────────────────────────┐           ┌──┐
     ┌──┐ write(3) │  ┌─────┐                                   enqueue()       │  │
     │  │◄─────────┼──┤Queue├─────────────◄──────────────◄──────────┼─────────(A_WRTE)◄──
     │fd│          │  └─────┘                                       │           │  │
     │  ├──────────►─────────────────┐                              │        ─  │  │
     └──┘ read(3)  └─────────────────┼──────────────────────────────┘           │  │
                                     │                                          │  │
                   ┌─────────────────▼─────────────────▲────────────┐           │  │
                   │                 │                              │           │  │
                   │                 │                              │           │  │
                   │                 └─────────────────────►──────────────────(A_WRTE)───►
                   │                enqueue()                       │           │  │
                   └────────────────────────────────────────────────┘           └──┘
                                  REMOTE SOCKET (PEER)

    Note that sockets can be peered regardless of their kind. A Remote Socket (RS) can be peered
   with a Local Socket (LS) or a Local Service Socket (LSS).
 */
struct asocket {
    /* the unique identifier for this asocket
     */
    unsigned id = 0;

    // Start Local socket fields
    // TODO: move all the local socket fields together

    /* flag: set when the socket's peer has closed
     * but packets are still queued for delivery
     */
    bool closing = false;

    // flag: set when the socket failed to write, so the socket will not wait to
    // write packets and close directly.
    bool has_write_error = false;

    /* flag: quit adbd when both ends close the
     * local service socket
     */
    int exit_on_close = 0;

    // End Local socket fields

    // the asocket we are connected to
    asocket* peer = nullptr;

    /* enqueue is called by our peer when it has data
     * for us.  It should return 0 if we can accept more
     * data or 1 if not.  If we return 1, we must call
     * peer->ready() when we once again are ready to
     * receive data.
     */
    int (*enqueue)(asocket* s, apacket::payload_type data) = nullptr;

    /* ready is called by the peer when it is ready for
     * us to send data via enqueue again
     */
    void (*ready)(asocket* s) = nullptr;

    /* shutdown is called by the peer before it goes away.
     * the socket should not do any further calls on its peer.
     * Always followed by a call to close. Optional, i.e. can be NULL.
     */
    void (*shutdown)(asocket* s) = nullptr;

    /* close is called by the peer when it has gone away.
     * we are not allowed to make any further calls on the
     * peer once our close method is called.
     */
    void (*close)(asocket* s) = nullptr;

    /* A socket is bound to atransport */
    atransport* transport = nullptr;

    size_t get_max_payload() const;

    // TODO: Make asocket an actual class and use inheritance instead of having an ever-growing
    //       struct with random use-specific fields stuffed into it.

    // Start Local socket fields
    fdevent* fde = nullptr;
    int fd = -1;

    // Queue of data that we've received from our peer, and are waiting to write into fd.
    IOVector packet_queue;
    // End Local socket fields

    // The number of bytes that have been acknowledged by the other end if delayed_ack is available.
    // This value can go negative: if we have a MAX_PAYLOAD's worth of bytes available to send,
    // we'll send out a full packet.
    std::optional<int64_t> available_send_bytes;

    // Start Smart socket fields
    // A temporary buffer used to hold a partially-read service string for smartsockets.
    std::string smart_socket_data;
    // End Smart socket fields
};

asocket *find_local_socket(unsigned local_id, unsigned remote_id);
void install_local_socket(asocket *s);
void remove_socket(asocket *s);
void close_all_sockets(atransport *t);

void local_socket_ack(asocket* s, std::optional<int32_t> acked_bytes);

asocket* create_local_socket(unique_fd fd);
asocket* create_local_service_socket(std::string_view destination, atransport* transport);

asocket *create_remote_socket(unsigned id, atransport *t);
void connect_to_remote(asocket* s, std::string_view destination);

#if ADB_HOST
void connect_to_smartsocket(asocket *s);
#endif

// Internal functions that are only made available here for testing purposes.
namespace internal {

#if ADB_HOST
bool parse_host_service(std::string_view* out_serial, std::string_view* out_command,
                        std::string_view service);
#endif

}  // namespace internal

#endif  // __ADB_SOCKET_H
