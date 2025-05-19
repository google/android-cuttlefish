/*
 * Copyright (C) 2007 The Android Open Source Project
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

#define TRACE_TAG TRANSPORT

#include "sysdeps.h"
#include "transport.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include <android-base/parsenetaddress.h>
#include <android-base/stringprintf.h>
#include <android-base/thread_annotations.h>
#include <cutils/sockets.h>

#include "adb.h"
#include "adb_io.h"
#include "adb_unique_fd.h"
#include "adb_utils.h"
#include "fdevent/fdevent.h"
#include "socket_spec.h"
#include "sysdeps/chrono.h"

// Android Wear has been using port 5601 in all of its documentation/tooling,
// but we search for emulators on ports [5554, 5555 + ADB_LOCAL_TRANSPORT_MAX].
// Avoid stomping on their port by restricting the active scanning range.
// Once emulators self-(re-)register, they'll have to avoid 5601 in their own way.
static int adb_local_transport_max_port = DEFAULT_ADB_LOCAL_TRANSPORT_PORT + 16 * 2 - 1;

static void adb_local_transport_max_port_env_override() {
    const char* env_max_s = getenv("ADB_LOCAL_TRANSPORT_MAX_PORT");
    if (env_max_s != nullptr) {
        size_t env_max;
        if (ParseUint(&env_max, env_max_s, nullptr) && env_max < 65536) {
            // < DEFAULT_ADB_LOCAL_TRANSPORT_PORT harmlessly mimics ADB_EMU=0
            adb_local_transport_max_port = env_max;
            D("transport: ADB_LOCAL_TRANSPORT_MAX_PORT read as %d", adb_local_transport_max_port);
        } else {
            D("transport: ADB_LOCAL_TRANSPORT_MAX_PORT '%s' invalid or >= 65536, so ignored",
              env_max_s);
        }
    }
}

static std::mutex& emulator_transports_lock = *new std::mutex();
// We keep a map from emulator port to transport.
// TODO: weak_ptr?
static std::unordered_map<int, atransport*> emulator_transports
        [[clang::no_destroy]] GUARDED_BY(emulator_transports_lock);

bool connect_emulator(int port) {
    fdevent_check_not_looper();
    std::string dummy;
    return connect_emulator_arbitrary_ports(port - 1, port, &dummy) == 0;
}

void connect_device(const std::string& address, std::string* response) {
    fdevent_check_not_looper();
    if (address.empty()) {
        *response = "empty address";
        return;
    }

    D("connection requested to '%s'", address.c_str());
    unique_fd fd;
    int port;
    std::string serial, prefix_addr;

    // If address does not match any socket type, it should default to TCP.
    if (address.starts_with("vsock:") || address.starts_with("localfilesystem:")) {
        prefix_addr = address;
    } else {
        prefix_addr = "tcp:" + address;
    }

    socket_spec_connect(&fd, prefix_addr, &port, &serial, response);
    if (fd.get() == -1) {
        return;
    }
    auto reconnect = [prefix_addr](atransport* t) {
        std::string response;
        unique_fd fd;
        int port;
        std::string serial;
        socket_spec_connect(&fd, prefix_addr, &port, &serial, &response);
        if (fd == -1) {
            D("reconnect failed: %s", response.c_str());
            return ReconnectResult::Retry;
        }
        // This invokes the part of register_socket_transport() that needs to be
        // invoked if the atransport* has already been setup. This eventually
        // calls atransport->SetConnection() with a newly created Connection*
        // that will in turn send the CNXN packet.
        return init_socket_transport(t, std::move(fd), port, false) >= 0 ? ReconnectResult::Success
                                                                         : ReconnectResult::Retry;
    };

    int error;
    if (!register_socket_transport(std::move(fd), serial, port, false, std::move(reconnect), false,
                                   &error)) {
        if (error == EALREADY) {
            *response = android::base::StringPrintf("already connected to %s", serial.c_str());
        } else if (error == EPERM) {
            *response = android::base::StringPrintf("failed to authenticate to %s", serial.c_str());
        } else {
            *response = android::base::StringPrintf("failed to connect to %s", serial.c_str());
        }
    } else {
        *response = android::base::StringPrintf("connected to %s", serial.c_str());
    }
}

int connect_emulator_arbitrary_ports(int console_port, int adb_port, std::string* error) {
    unique_fd fd;

    if (find_emulator_transport_by_adb_port(adb_port) != nullptr ||
        find_emulator_transport_by_console_port(console_port) != nullptr) {
        return -1;
    }

    const char* host = getenv("ADBHOST");
    if (host) {
        fd.reset(network_connect(host, adb_port, SOCK_STREAM, 0, error));
    }

    if (fd < 0) {
        fd.reset(network_loopback_client(adb_port, SOCK_STREAM, error));
    }

    if (fd >= 0) {
        D("client: connected on remote on fd %d", fd.get());
        close_on_exec(fd.get());
        disable_tcp_nagle(fd.get());
        std::string serial = getEmulatorSerialString(console_port);
        if (register_socket_transport(
                    std::move(fd), std::move(serial), adb_port, true,
                    [](atransport*) { return ReconnectResult::Abort; }, false)) {
            return 0;
        }
    }
    return -1;
}

static void PollAllLocalPortsForEmulator() {
    // Try to connect to any number of running emulator instances.
    for (int port = DEFAULT_ADB_LOCAL_TRANSPORT_PORT; port <= adb_local_transport_max_port;
         port += 2) {
        connect_emulator(port);  // Note, uses port and port-1, so '=max_port' is OK.
    }
}

// Retry the disconnected local port for 60 times, and sleep 1 second between two retries.
static constexpr uint32_t LOCAL_PORT_RETRY_COUNT = 60;
static constexpr auto LOCAL_PORT_RETRY_INTERVAL = 1s;

struct RetryPort {
    int port;
    uint32_t retry_count;
};

// Retry emulators just kicked.
static std::vector<RetryPort>& retry_ports = *new std::vector<RetryPort>;
std::mutex& retry_ports_lock = *new std::mutex;
std::condition_variable& retry_ports_cond = *new std::condition_variable;

static void client_socket_thread(std::string_view) {
    adb_thread_setname("emulator_scanner");
    PollAllLocalPortsForEmulator();
    while (true) {
        std::vector<RetryPort> ports;
        // Collect retry ports.
        {
            std::unique_lock<std::mutex> lock(retry_ports_lock);
            while (retry_ports.empty()) {
                retry_ports_cond.wait(lock);
            }
            retry_ports.swap(ports);
        }
        // Sleep here instead of the end of loop, because if we immediately try to reconnect
        // the emulator just kicked, the adbd on the emulator may not have time to remove the
        // just kicked transport.
        std::this_thread::sleep_for(LOCAL_PORT_RETRY_INTERVAL);

        // Try connecting retry ports.
        std::vector<RetryPort> next_ports;
        for (auto& port : ports) {
            VLOG(TRANSPORT) << "retry port " << port.port << ", last retry_count "
                            << port.retry_count;
            if (connect_emulator(port.port)) {
                VLOG(TRANSPORT) << "retry port " << port.port << " successfully";
                continue;
            }
            if (--port.retry_count > 0) {
                next_ports.push_back(port);
            } else {
                VLOG(TRANSPORT) << "stop retrying port " << port.port;
            }
        }

        // Copy back left retry ports.
        {
            std::unique_lock<std::mutex> lock(retry_ports_lock);
            retry_ports.insert(retry_ports.end(), next_ports.begin(), next_ports.end());
        }
    }
}

void init_emulator_scanner(const std::string& addr) {
    VLOG(TRANSPORT) << "Starting emulator scanner on '" << addr << "'";
    std::thread(client_socket_thread, addr).detach();
    adb_local_transport_max_port_env_override();
}

struct EmulatorConnection : public FdConnection {
    EmulatorConnection(unique_fd fd, int local_port)
        : FdConnection(std::move(fd)), local_port_(local_port) {}

    ~EmulatorConnection() {
        VLOG(TRANSPORT) << "remote_close, local_port = " << local_port_;
        std::unique_lock<std::mutex> lock(retry_ports_lock);
        RetryPort port;
        port.port = local_port_;
        port.retry_count = LOCAL_PORT_RETRY_COUNT;
        retry_ports.push_back(port);
        retry_ports_cond.notify_one();
    }

    void Close() override {
        std::lock_guard<std::mutex> lock(emulator_transports_lock);
        emulator_transports.erase(local_port_);
        FdConnection::Close();
    }

    int local_port_;
};

/* Only call this function if you already hold emulator_transports_lock. */
static atransport* find_emulator_transport_by_adb_port_locked(int adb_port)
        REQUIRES(emulator_transports_lock) {
    auto it = emulator_transports.find(adb_port);
    if (it == emulator_transports.end()) {
        return nullptr;
    }
    return it->second;
}

atransport* find_emulator_transport_by_adb_port(int adb_port) {
    std::lock_guard<std::mutex> lock(emulator_transports_lock);
    return find_emulator_transport_by_adb_port_locked(adb_port);
}

atransport* find_emulator_transport_by_console_port(int console_port) {
    return find_transport(getEmulatorSerialString(console_port).c_str());
}

std::string getEmulatorSerialString(int console_port) {
    return android::base::StringPrintf("emulator-%d", console_port);
}

int init_socket_transport(atransport* t, unique_fd fd, int adb_port, bool is_emulator) {
    int fail = 0;

    if (is_emulator) {
        auto emulator_connection = std::make_unique<EmulatorConnection>(std::move(fd), adb_port);
        t->SetConnection(
                std::make_unique<BlockingConnectionAdapter>(std::move(emulator_connection)));
        std::lock_guard<std::mutex> lock(emulator_transports_lock);
        atransport* existing_transport = find_emulator_transport_by_adb_port_locked(adb_port);
        if (existing_transport != nullptr) {
            D("is_emulator transport for port %d already registered (%p)?", adb_port,
              existing_transport);
            fail = -1;
        } else {
            emulator_transports[adb_port] = t;
        }

        return fail;
    }

    // Regular tcp connection.
    auto fd_connection = std::make_unique<FdConnection>(std::move(fd));
    t->SetConnection(std::make_unique<BlockingConnectionAdapter>(std::move(fd_connection)));
    return fail;
}
