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

#define TRACE_TAG ADB

#include "sysdeps.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <thread>

#include <android-base/errors.h>
#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/stringprintf.h>

#include "adb.h"
#include "adb_auth.h"
#include "adb_client.h"
#include "adb_listeners.h"
#include "adb_mdns.h"
#include "adb_utils.h"
#include "adb_wifi.h"
#include "client/mdns_utils.h"
#include "client/transport_client.h"
#include "client/usb.h"
#include "client/usb_libusb_hotplug.h"
#include "commandline.h"
#include "sysdeps/chrono.h"
#include "transport.h"

const char** __adb_argv;
const char** __adb_envp;

static void setup_daemon_logging() {
    const std::string log_file_path(GetLogFilePath());
    int fd = unix_open(log_file_path, O_WRONLY | O_CREAT | O_APPEND, 0640);
    if (fd == -1) {
        PLOG(FATAL) << "cannot open " << log_file_path;
    }
    if (dup2(fd, STDOUT_FILENO) == -1) {
        PLOG(FATAL) << "cannot redirect stdout";
    }
    if (dup2(fd, STDERR_FILENO) == -1) {
        PLOG(FATAL) << "cannot redirect stderr";
    }
    unix_close(fd);

    fprintf(stderr, "--- adb starting (pid %d) ---\n", getpid());
    LOG(INFO) << adb_version();
}

void adb_server_cleanup() {
    // Upon exit, we want to clean up in the following order:
    //   1. close_smartsockets, so that we don't get any new clients
    //   2. kick_all_transports, to avoid writing only part of a packet to a transport.
    //   3. usb_cleanup, to tear down the USB stack.
    close_smartsockets();
    kick_all_transports();
    usb_cleanup();
}

static void intentionally_leak() {
    void* p = ::operator new(1);
    // The analyzer is upset about this leaking. NOLINTNEXTLINE
    LOG(INFO) << "leaking pointer " << p;
}

// one_device: if null, server owns all devices, else server owns only
//     device where atransport::MatchesTarget(one_device) is true.
int adb_server_main(int is_daemon, const std::string& socket_spec, const char* one_device,
                    int ack_reply_fd) {
#if defined(_WIN32)
    // adb start-server starts us up with stdout and stderr hooked up to
    // anonymous pipes. When the C Runtime sees this, it makes stderr and
    // stdout buffered, but to improve the chance that error output is seen,
    // unbuffer stdout and stderr just like if we were run at the console.
    // This also keeps stderr unbuffered when it is redirected to adb.log.
    if (is_daemon) {
        if (setvbuf(stdout, nullptr, _IONBF, 0) == -1) {
            PLOG(FATAL) << "cannot make stdout unbuffered";
        }
        if (setvbuf(stderr, nullptr, _IONBF, 0) == -1) {
            PLOG(FATAL) << "cannot make stderr unbuffered";
        }
    }

    // TODO: On Ctrl-C, consider trying to kill a starting up adb server (if we're in
    // launch_server) by calling GenerateConsoleCtrlEvent().

    // On Windows, SIGBREAK is when Ctrl-Break is pressed or the console window is closed. It should
    // act like Ctrl-C.
    signal(SIGBREAK, [](int) { raise(SIGINT); });
#endif
    signal(SIGINT, [](int) { fdevent_run_on_looper([]() { exit(0); }); });

    if (one_device) {
        transport_set_one_device(one_device);
    }

    const char* reject_kill_server = getenv("ADB_REJECT_KILL_SERVER");
    if (reject_kill_server && strcmp(reject_kill_server, "1") == 0) {
        adb_set_reject_kill_server(true);
    }

    const char* leak = getenv("ADB_LEAK");
    if (leak && strcmp(leak, "1") == 0) {
        intentionally_leak();
    }

    if (is_daemon) {
        close_stdin();
        setup_daemon_logging();
    }

    atexit(adb_server_cleanup);

    init_reconnect_handler();

    if (mdns::is_enabled()) {
        init_mdns_transport_discovery();
    }

    if (!getenv("ADB_USB") || strcmp(getenv("ADB_USB"), "0") != 0) {
        if (is_libusb_enabled()) {
            libusb::usb_init();
        } else {
            usb_init();
        }
    } else {
        adb_notify_device_scan_complete();
    }

    if (!getenv("ADB_EMU") || strcmp(getenv("ADB_EMU"), "0") != 0) {
        init_emulator_scanner(
                android::base::StringPrintf("tcp:%d", DEFAULT_ADB_LOCAL_TRANSPORT_PORT));
    }

    std::string error;

    auto start = std::chrono::steady_clock::now();

    // If we told a previous adb server to quit because of version mismatch, we can get to this
    // point before it's finished exiting. Retry for a while to give it some time. Don't actually
    // accept any connections until adb_wait_for_device_initialization finishes below.
    while (install_listener(socket_spec, kSmartSocketConnectTo, nullptr, INSTALL_LISTENER_DISABLED,
                            nullptr, &error) != INSTALL_STATUS_OK) {
        if (std::chrono::steady_clock::now() - start > 0.5s) {
            LOG(FATAL) << "could not install *smartsocket* listener: " << error;
        }

        std::this_thread::sleep_for(100ms);
    }

    adb_auth_init();

    if (is_daemon) {
#if !defined(_WIN32)
        // Start a new session for the daemon. Do this here instead of after the fork so
        // that a ctrl-c between the "starting server" and "done starting server" messages
        // gets a chance to terminate the server.
        // setsid will fail with EPERM if it's already been a lead process of new session.
        // Ignore such error.
        if (setsid() == -1 && errno != EPERM) {
            PLOG(FATAL) << "setsid() failed";
        }
#endif
    }

    // Wait for the USB scan to complete before notifying the parent that we're up.
    // We need to perform this in a thread, because we would otherwise block the event loop.
    std::thread notify_thread([ack_reply_fd]() {
        adb_wait_for_device_initialization();

        if (ack_reply_fd >= 0) {
            // Any error output written to stderr now goes to adb.log. We could
            // keep around a copy of the stderr fd and use that to write any errors
            // encountered by the following code, but that is probably overkill.
#if defined(_WIN32)
            const HANDLE ack_reply_handle = cast_int_to_handle(ack_reply_fd);
            const CHAR ack[] = "OK\n";
            const DWORD bytes_to_write = arraysize(ack) - 1;
            DWORD written = 0;
            if (!WriteFile(ack_reply_handle, ack, bytes_to_write, &written, NULL)) {
                LOG(FATAL) << "cannot write ACK to handle " << ack_reply_handle
                           << android::base::SystemErrorCodeToString(GetLastError());
            }
            if (written != bytes_to_write) {
                LOG(FATAL) << "cannot write " << bytes_to_write << " bytes of ACK: only wrote "
                           << written << " bytes";
            }
            CloseHandle(ack_reply_handle);
#else
            // TODO(danalbert): Can't use SendOkay because we're sending "OK\n", not
            // "OKAY".
            if (!android::base::WriteStringToFd("OK\n", ack_reply_fd)) {
                PLOG(FATAL) << "error writing ACK to fd " << ack_reply_fd;
            }
            unix_close(ack_reply_fd);
#endif
        }
        // We don't accept() client connections until this point: this way, clients
        // can't see wonky state early in startup even if they're connecting directly
        // to the server instead of going through the adb program.
        fdevent_run_on_looper([] { enable_server_sockets(); });
    });
    notify_thread.detach();

#if defined(__linux__)
    // Write our location to .android/adb.$PORT, so that older clients can exec us.
    std::string path;
    if (!android::base::Readlink("/proc/self/exe", &path)) {
        PLOG(ERROR) << "failed to readlink /proc/self/exe";
    }

    std::optional<std::string> server_executable_path = adb_get_server_executable_path();
    if (server_executable_path) {
      if (!android::base::WriteStringToFile(path, *server_executable_path)) {
          PLOG(ERROR) << "failed to write server path to " << path;
      }
    }
#endif

    D("Event loop starting");
    fdevent_loop();
    return 0;
}

int main(int argc, char* argv[], char* envp[]) {
    __adb_argv = const_cast<const char**>(argv);
    __adb_envp = const_cast<const char**>(envp);
    adb_trace_init(argv);
    return adb_commandline(argc - 1, const_cast<const char**>(argv + 1));
}
