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

#define TRACE_TAG ADB

#include "sysdeps.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <iostream>

#include <memory>
#include <string>
#include <thread>
#include <vector>
using namespace std::string_literals;

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/parseint.h>
#include <android-base/stringprintf.h>
#include <android-base/strings.h>

#include "client/host_services.h"

#if defined(_WIN32)
#define _POSIX
#include <signal.h>
#else
#include <sys/ioctl.h>
#include <termios.h>
#endif

#include "adb.h"
#include "adb_auth.h"
#include "adb_client.h"
#include "adb_host.pb.h"
#include "adb_install.h"
#include "adb_io.h"
#include "adb_unique_fd.h"
#include "adb_utils.h"
#include "app_processes.pb.h"
#include "bugreport.h"
#include "client/file_sync_client.h"
#include "commandline.h"
#include "incremental_server.h"
#include "services.h"
#include "shell_protocol.h"
#include "socket_spec.h"
#include "sysdeps/chrono.h"

DefaultStandardStreamsCallback DEFAULT_STANDARD_STREAMS_CALLBACK(nullptr, nullptr);

static std::string product_file(const std::string& file) {
    const char* ANDROID_PRODUCT_OUT = getenv("ANDROID_PRODUCT_OUT");
    if (ANDROID_PRODUCT_OUT == nullptr) {
        error_exit("product directory not specified; set $ANDROID_PRODUCT_OUT");
    }
    return std::string{ANDROID_PRODUCT_OUT} + OS_PATH_SEPARATOR_STR + file;
}

static constexpr int kDefaultServerPort = 5037;

static void help() {
    fprintf(stdout, "%s\n", adb_version().c_str());
    // clang-format off
    fprintf(stdout,
        "global options:\n"
        " -a                       listen on all network interfaces, not just localhost\n"
        " -d                       use USB device (error if multiple devices connected)\n"
        " -e                       use TCP/IP device (error if multiple TCP/IP devices available)\n"
        " -s SERIAL                use device with given serial (overrides $ANDROID_SERIAL)\n"
        " -t ID                    use device with given transport id\n"
        " -H                       name of adb server host [default=localhost]\n"
        " -P                       port of adb server [default=5037]\n"
        " -L SOCKET                listen on given socket for adb server"
        " [default=tcp:localhost:5037]\n"
        " --one-device SERIAL|USB  only allowed with 'start-server' or 'server nodaemon', server"
        " will only connect to one USB device, specified by a serial number or USB device"
        " address.\n"
        " --exit-on-write-error    exit if stdout is closed\n"
        "\n"
        "general commands:\n"
        " devices [-l]             list connected devices (-l for long output)\n"
        " help                     show this help message\n"
        " version                  show version num\n"
        "\n"
        "networking:\n"
        " connect HOST[:PORT]      connect to a device via TCP/IP [default port=5555]\n"
        " disconnect [HOST[:PORT]]\n"
        "     disconnect from given TCP/IP device [default port=5555], or all\n"
        " pair HOST[:PORT] [PAIRING CODE]\n"
        "     pair with a device for secure TCP/IP communication\n"
        " forward --list           list all forward socket connections\n"
        " forward [--no-rebind] LOCAL REMOTE\n"
        "     forward socket connection using:\n"
        "       tcp:<port> (<local> may be \"tcp:0\" to pick any open port)\n"
        "       localabstract:<unix domain socket name>\n"
        "       localreserved:<unix domain socket name>\n"
        "       localfilesystem:<unix domain socket name>\n"
        "       dev:<character device name>\n"
        "       dev-raw:<character device name> (open device in raw mode)\n"
        "       jdwp:<process pid> (remote only)\n"
        "       vsock:<CID>:<port> (remote only)\n"
        "       acceptfd:<fd> (listen only)\n"
        " forward --remove LOCAL   remove specific forward socket connection\n"
        " forward --remove-all     remove all forward socket connections\n"
        " reverse --list           list all reverse socket connections from device\n"
        " reverse [--no-rebind] REMOTE LOCAL\n"
        "     reverse socket connection using:\n"
        "       tcp:<port> (<remote> may be \"tcp:0\" to pick any open port)\n"
        "       localabstract:<unix domain socket name>\n"
        "       localreserved:<unix domain socket name>\n"
        "       localfilesystem:<unix domain socket name>\n"
        " reverse --remove REMOTE  remove specific reverse socket connection\n"
        " reverse --remove-all     remove all reverse socket connections from device\n"
        " mdns check               check if mdns discovery is available\n"
        " mdns services            list all discovered services\n"
        "\n"
        "file transfer:\n"
        " push [--sync] [-z ALGORITHM] [-Z] LOCAL... REMOTE\n"
        "     copy local files/directories to device\n"
        "     -n: dry run: push files to device without storing to the filesystem\n"
        "     -q: suppress progress messages\n"
        "     -Z: disable compression\n"
        "     -z: enable compression with a specified algorithm (any/none/brotli/lz4/zstd)\n"
        "     --sync: only push files that have different timestamps on the host than the device\n"
        " pull [-a] [-z ALGORITHM] [-Z] REMOTE... LOCAL\n"
        "     copy files/dirs from device\n"
        "     -a: preserve file timestamp and mode\n"
        "     -q: suppress progress messages\n"
        "     -Z: disable compression\n"
        "     -z: enable compression with a specified algorithm (any/none/brotli/lz4/zstd)\n"
        " sync [-l] [-z ALGORITHM] [-Z] [all|data|odm|oem|product|system|system_ext|vendor]\n"
        "     sync a local build from $ANDROID_PRODUCT_OUT to the device (default all)\n"
        "     -l: list files that would be copied, but don't copy them\n"
        "     -n: dry run: push files to device without storing to the filesystem\n"
        "     -q: suppress progress messages\n"
        "     -Z: disable compression\n"
        "     -z: enable compression with a specified algorithm (any/none/brotli/lz4/zstd)\n"
        "\n"
        "shell:\n"
        " shell [-e ESCAPE] [-n] [-Tt] [-x] [COMMAND...]\n"
        "     run remote shell command (interactive shell if no command given)\n"
        "     -e: choose escape character, or \"none\"; default '~'\n"
        "     -n: don't read from stdin\n"
        "     -T: disable pty allocation\n"
        "     -t: allocate a pty if on a tty (-tt: force pty allocation)\n"
        "     -x: disable remote exit codes and stdout/stderr separation\n"
        " emu COMMAND              run emulator console command\n"
        "\n"
        "app installation (see also `adb shell cmd package help`):\n"
        " install [-lrtsdg] [--instant] PACKAGE\n"
        "     push a single package to the device and install it\n"
        " install-multiple [-lrtsdpg] [--instant] PACKAGE...\n"
        "     push multiple APKs to the device for a single package and install them\n"
        " install-multi-package [-lrtsdpg] [--instant] PACKAGE...\n"
        "     push one or more packages to the device and install them atomically\n"
        "     -r: replace existing application\n"
        "     -t: allow test packages\n"
        "     -d: allow version code downgrade (debuggable packages only)\n"
        "     -p: partial application install (install-multiple only)\n"
        "     -g: grant all runtime permissions\n"
        "     --abi ABI: override platform's default ABI\n"
        "     --instant: cause the app to be installed as an ephemeral install app\n"
        "     --no-streaming: always push APK to device and invoke Package Manager as separate steps\n"
        "     --streaming: force streaming APK directly into Package Manager\n"
        "     --force-agent: force update of deployment agent when using fast deploy\n"
        "     --date-check-agent: update deployment agent when local version is newer and using fast deploy\n"
        "     --version-check-agent: update deployment agent when local version has different version code and using fast deploy\n"
#ifndef _WIN32
        "     --local-agent: locate agent files from local source build (instead of SDK location)\n"
#endif
        "     (See also `adb shell pm help` for more options.)\n"
        //TODO--installlog <filename>
        " uninstall [-k] PACKAGE\n"
        "     remove this app package from the device\n"
        "     '-k': keep the data and cache directories\n"
        "\n"
        "debugging:\n"
        " bugreport [PATH]\n"
        "     write bugreport to given PATH [default=bugreport.zip];\n"
        "     if PATH is a directory, the bug report is saved in that directory.\n"
        "     devices that don't support zipped bug reports output to stdout.\n"
        " jdwp                     list pids of processes hosting a JDWP transport\n"
        " logcat                   show device log (logcat --help for more)\n"
        "\n"
        "security:\n"
        " disable-verity           disable dm-verity checking on userdebug builds\n"
        " enable-verity            re-enable dm-verity checking on userdebug builds\n"
        " keygen FILE\n"
        "     generate adb public/private key; private key stored in FILE,\n"
        "\n"
        "scripting:\n"
        " wait-for[-TRANSPORT]-STATE...\n"
        "     wait for device to be in a given state\n"
        "     STATE: device, recovery, rescue, sideload, bootloader, or disconnect\n"
        "     TRANSPORT: usb, local, or any [default=any]\n"
        " get-state                print offline | bootloader | device\n"
        " get-serialno             print <serial-number>\n"
        " get-devpath              print <device-path>\n"
        " remount [-R]\n"
        "      remount partitions read-write. if a reboot is required, -R will\n"
        "      will automatically reboot the device.\n"
        " reboot [bootloader|recovery|sideload|sideload-auto-reboot]\n"
        "     reboot the device; defaults to booting system image but\n"
        "     supports bootloader and recovery too. sideload reboots\n"
        "     into recovery and automatically starts sideload mode,\n"
        "     sideload-auto-reboot is the same but reboots after sideloading.\n"
        " sideload OTAPACKAGE      sideload the given full OTA package\n"
        " root                     restart adbd with root permissions\n"
        " unroot                   restart adbd without root permissions\n"
        " usb                      restart adbd listening on USB\n"
        " tcpip PORT               restart adbd listening on TCP on PORT\n"
        "\n"
        "internal debugging:\n"
        " start-server             ensure that there is a server running\n"
        " kill-server              kill the server if it is running\n"
        " reconnect                kick connection from host side to force reconnect\n"
        " reconnect device         kick connection from device side to force reconnect\n"
        " reconnect offline        reset offline/unauthorized devices to force reconnect\n"
        "\n"
        "usb:\n"
        " attach                   attach a detached USB device\n"
        " detach                   detach from a USB device to allow use by other processes\n"
        ""
        "environment variables:\n"
        " $ADB_TRACE\n"
        "     comma/space separated list of debug info to log:\n"
        "     all,adb,sockets,packets,rwx,usb,sync,sysdeps,transport,jdwp,services,auth,fdevent,shell,incremental\n"
        " $ADB_VENDOR_KEYS         colon-separated list of keys (files or directories)\n"
        " $ANDROID_SERIAL          serial number to connect to (see -s)\n"
        " $ANDROID_LOG_TAGS        tags to be used by logcat (see logcat --help)\n"
        " $ADB_LOCAL_TRANSPORT_MAX_PORT max emulator scan port (default 5585, 16 emus)\n"
        " $ADB_MDNS_AUTO_CONNECT   comma-separated list of mdns services to allow auto-connect (default adb-tls-connect)\n"
        "\n"
        "Online documentation: https://android.googlesource.com/platform/packages/modules/adb/+/refs/heads/main/docs/user/adb.1.md\n"
        "\n"
    );
    // clang-format on
}

#if defined(_WIN32)

// Implemented in sysdeps_win32.cpp.
void stdin_raw_init();
void stdin_raw_restore();

#else
static termios g_saved_terminal_state;

static void stdin_raw_init() {
    if (tcgetattr(STDIN_FILENO, &g_saved_terminal_state)) return;

    termios tio;
    if (tcgetattr(STDIN_FILENO, &tio)) return;

    cfmakeraw(&tio);

    // No timeout but request at least one character per read.
    tio.c_cc[VTIME] = 0;
    tio.c_cc[VMIN] = 1;

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &tio);
}

static void stdin_raw_restore() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_saved_terminal_state);
}
#endif

int read_and_dump_protocol(borrowed_fd fd, StandardStreamsCallbackInterface* callback) {
    // OpenSSH returns 255 on unexpected disconnection.
    int exit_code = 255;
    std::unique_ptr<ShellProtocol> protocol = std::make_unique<ShellProtocol>(fd);
    if (!protocol) {
      LOG(ERROR) << "failed to allocate memory for ShellProtocol object";
      return 1;
    }
    while (protocol->Read()) {
      if (protocol->id() == ShellProtocol::kIdStdout) {
          if (!callback->OnStdoutReceived(protocol->data(), protocol->data_length())) {
              exit_code = SIGPIPE + 128;
              break;
          }
      } else if (protocol->id() == ShellProtocol::kIdStderr) {
          if (!callback->OnStderrReceived(protocol->data(), protocol->data_length())) {
              exit_code = SIGPIPE + 128;
              break;
          }
      } else if (protocol->id() == ShellProtocol::kIdExit) {
        // data() returns a char* which doesn't have defined signedness.
        // Cast to uint8_t to prevent 255 from being sign extended to INT_MIN,
        // which doesn't get truncated on Windows.
        exit_code = static_cast<uint8_t>(protocol->data()[0]);
      }
    }
    return exit_code;
}

int read_and_dump(borrowed_fd fd, bool use_shell_protocol,
                  StandardStreamsCallbackInterface* callback) {
    int exit_code = 0;
    if (fd < 0) return exit_code;

    if (use_shell_protocol) {
      exit_code = read_and_dump_protocol(fd, callback);
    } else {
      char raw_buffer[BUFSIZ];
      char* buffer_ptr = raw_buffer;
      while (true) {
        D("read_and_dump(): pre adb_read(fd=%d)", fd.get());
        int length = adb_read(fd, raw_buffer, sizeof(raw_buffer));
        D("read_and_dump(): post adb_read(fd=%d): length=%d", fd.get(), length);
        if (length <= 0) {
          break;
        }
        if (!callback->OnStdoutReceived(buffer_ptr, length)) {
            break;
        }
      }
    }

    return callback->Done(exit_code);
}

static void stdinout_raw_prologue(int inFd, int outFd, int& old_stdin_mode, int& old_stdout_mode) {
    if (inFd == STDIN_FILENO) {
        stdin_raw_init();
#ifdef _WIN32
        old_stdin_mode = _setmode(STDIN_FILENO, _O_BINARY);
        if (old_stdin_mode == -1) {
            PLOG(FATAL) << "could not set stdin to binary";
        }
#endif
    }

#ifdef _WIN32
    if (outFd == STDOUT_FILENO) {
        old_stdout_mode = _setmode(STDOUT_FILENO, _O_BINARY);
        if (old_stdout_mode == -1) {
            PLOG(FATAL) << "could not set stdout to binary";
        }
    }
#endif
}

static void stdinout_raw_epilogue(int inFd, int outFd, int old_stdin_mode, int old_stdout_mode) {
    if (inFd == STDIN_FILENO) {
        stdin_raw_restore();
#ifdef _WIN32
        if (_setmode(STDIN_FILENO, old_stdin_mode) == -1) {
            PLOG(FATAL) << "could not restore stdin mode";
        }
#endif
    }

#ifdef _WIN32
    if (outFd == STDOUT_FILENO) {
        if (_setmode(STDOUT_FILENO, old_stdout_mode) == -1) {
            PLOG(FATAL) << "could not restore stdout mode";
        }
    }
#endif
}

bool copy_to_file(int inFd, int outFd) {
    bool result = true;
    std::vector<char> buf(64 * 1024);
    int len;
    long total = 0;
    int old_stdin_mode = -1;
    int old_stdout_mode = -1;

    D("copy_to_file(%d -> %d)", inFd, outFd);

    stdinout_raw_prologue(inFd, outFd, old_stdin_mode, old_stdout_mode);

    while (true) {
        if (inFd == STDIN_FILENO) {
            len = unix_read(inFd, buf.data(), buf.size());
        } else {
            len = adb_read(inFd, buf.data(), buf.size());
        }
        if (len == 0) {
            D("copy_to_file() : read 0 bytes; exiting");
            break;
        }
        if (len < 0) {
            D("copy_to_file(): read failed: %s", strerror(errno));
            result = false;
            break;
        }
        if (outFd == STDOUT_FILENO) {
            fwrite(buf.data(), 1, len, stdout);
            fflush(stdout);
        } else {
            adb_write(outFd, buf.data(), len);
        }
        total += len;
    }

    stdinout_raw_epilogue(inFd, outFd, old_stdin_mode, old_stdout_mode);

    D("copy_to_file() finished with %s after %lu bytes", result ? "success" : "failure", total);
    return result;
}

static void send_window_size_change(int fd, std::unique_ptr<ShellProtocol>& shell) {
    // Old devices can't handle window size changes.
    if (shell == nullptr) return;

#if defined(_WIN32)
    struct winsize {
        unsigned short ws_row;
        unsigned short ws_col;
        unsigned short ws_xpixel;
        unsigned short ws_ypixel;
    };
#endif

    winsize ws;

#if defined(_WIN32)
    // If stdout is redirected to a non-console, we won't be able to get the
    // console size, but that makes sense.
    const intptr_t intptr_handle = _get_osfhandle(STDOUT_FILENO);
    if (intptr_handle == -1) return;

    const HANDLE handle = reinterpret_cast<const HANDLE>(intptr_handle);

    CONSOLE_SCREEN_BUFFER_INFO info;
    memset(&info, 0, sizeof(info));
    if (!GetConsoleScreenBufferInfo(handle, &info)) return;

    memset(&ws, 0, sizeof(ws));
    // The number of visible rows, excluding offscreen scroll-back rows which are in info.dwSize.Y.
    ws.ws_row = info.srWindow.Bottom - info.srWindow.Top + 1;
    // If the user has disabled "Wrap text output on resize", they can make the screen buffer wider
    // than the window, in which case we should use the width of the buffer.
    ws.ws_col = info.dwSize.X;
#else
    if (ioctl(fd, TIOCGWINSZ, &ws) == -1) return;
#endif

    // Send the new window size as human-readable ASCII for debugging convenience.
    size_t l = snprintf(shell->data(), shell->data_capacity(), "%dx%d,%dx%d",
                        ws.ws_row, ws.ws_col, ws.ws_xpixel, ws.ws_ypixel);
    shell->Write(ShellProtocol::kIdWindowSizeChange, l + 1);
}

// Used to pass multiple values to the stdin read thread.
struct StdinReadArgs {
    int stdin_fd, write_fd;
    bool raw_stdin;
    std::unique_ptr<ShellProtocol> protocol;
    char escape_char;
};

// Loops to read from stdin and push the data to the given FD.
// The argument should be a pointer to a StdinReadArgs object. This function
// will take ownership of the object and delete it when finished.
static void stdin_read_thread_loop(void* x) {
    std::unique_ptr<StdinReadArgs> args(reinterpret_cast<StdinReadArgs*>(x));

#if !defined(_WIN32)
    // Mask SIGTTIN in case we're in a backgrounded process.
    sigset_t sigset;
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGTTIN);
    pthread_sigmask(SIG_BLOCK, &sigset, nullptr);
#endif

#if defined(_WIN32)
    // _get_interesting_input_record_uncached() causes unix_read_interruptible()
    // to return -1 with errno == EINTR if the window size changes.
#else
    // Unblock SIGWINCH for this thread, so our read(2) below will be
    // interrupted if the window size changes.
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGWINCH);
    pthread_sigmask(SIG_UNBLOCK, &mask, nullptr);
#endif

    // Set up the initial window size.
    send_window_size_change(args->stdin_fd, args->protocol);

    char raw_buffer[BUFSIZ];
    char* buffer_ptr = raw_buffer;
    size_t buffer_size = sizeof(raw_buffer);
    if (args->protocol != nullptr) {
        buffer_ptr = args->protocol->data();
        buffer_size = args->protocol->data_capacity();
    }

    // If we need to parse escape sequences, make life easy.
    if (args->raw_stdin && args->escape_char != '\0') {
        buffer_size = 1;
    }

    enum EscapeState { kMidFlow, kStartOfLine, kInEscape };
    EscapeState state = kStartOfLine;

    while (true) {
        // Use unix_read_interruptible() rather than adb_read() for stdin.
        D("stdin_read_thread_loop(): pre unix_read_interruptible(fdi=%d,...)", args->stdin_fd);
        int r = unix_read_interruptible(args->stdin_fd, buffer_ptr,
                                        buffer_size);
        if (r == -1 && errno == EINTR) {
            send_window_size_change(args->stdin_fd, args->protocol);
            continue;
        }
        D("stdin_read_thread_loop(): post unix_read_interruptible(fdi=%d,...)", args->stdin_fd);
        if (r <= 0) {
            // Only devices using the shell protocol know to close subprocess
            // stdin. For older devices we want to just leave the connection
            // open, otherwise an unpredictable amount of return data could
            // be lost due to the FD closing before all data has been received.
            if (args->protocol) {
                args->protocol->Write(ShellProtocol::kIdCloseStdin, 0);
            }
            break;
        }
        // If we made stdin raw, check input for escape sequences. In
        // this situation signals like Ctrl+C are sent remotely rather than
        // interpreted locally so this provides an emergency out if the remote
        // process starts ignoring the signal. SSH also does this, see the
        // "escape characters" section on the ssh man page for more info.
        if (args->raw_stdin && args->escape_char != '\0') {
            char ch = buffer_ptr[0];
            if (ch == args->escape_char) {
                if (state == kStartOfLine) {
                    state = kInEscape;
                    // Swallow the escape character.
                    continue;
                } else {
                    state = kMidFlow;
                }
            } else {
                if (state == kInEscape) {
                    if (ch == '.') {
                        fprintf(stderr,"\r\n[ disconnected ]\r\n");
                        stdin_raw_restore();
                        exit(0);
                    } else {
                        // We swallowed an escape character that wasn't part of
                        // a valid escape sequence; time to cough it up.
                        buffer_ptr[0] = args->escape_char;
                        buffer_ptr[1] = ch;
                        ++r;
                    }
                }
                state = (ch == '\n' || ch == '\r') ? kStartOfLine : kMidFlow;
            }
        }
        if (args->protocol) {
            if (!args->protocol->Write(ShellProtocol::kIdStdin, r)) {
                break;
            }
        } else {
            if (!WriteFdExactly(args->write_fd, buffer_ptr, r)) {
                break;
            }
        }
    }
}

// Returns a shell service string with the indicated arguments and command.
static std::string ShellServiceString(bool use_shell_protocol,
                                      const std::string& type_arg,
                                      const std::string& command) {
    std::vector<std::string> args;
    if (use_shell_protocol) {
        args.push_back(kShellServiceArgShellProtocol);

        const char* terminal_type = getenv("TERM");
        if (terminal_type != nullptr) {
            args.push_back(std::string("TERM=") + terminal_type);
        }
    }
    if (!type_arg.empty()) {
        args.push_back(type_arg);
    }

    // Shell service string can look like: shell[,arg1,arg2,...]:[command].
    return android::base::StringPrintf("shell%s%s:%s",
                                       args.empty() ? "" : ",",
                                       android::base::Join(args, ',').c_str(),
                                       command.c_str());
}

// Connects to a shell on the device and read/writes data.
//
// Note: currently this function doesn't properly clean up resources; the
// FD connected to the adb server is never closed and the stdin read thread
// may never exit.
//
// On success returns the remote exit code if |use_shell_protocol| is true,
// 0 otherwise. On failure returns 1.
static int RemoteShell(bool use_shell_protocol, const std::string& type_arg, char escape_char,
                       bool empty_command, const std::string& service_string) {
    // Old devices can't handle a service string that's longer than MAX_PAYLOAD_V1.
    // Use |use_shell_protocol| to determine whether to allow a command longer than that.
    if (service_string.size() > MAX_PAYLOAD_V1 && !use_shell_protocol) {
        fprintf(stderr, "error: shell command too long\n");
        return 1;
    }

    // Make local stdin raw if the device allocates a PTY, which happens if:
    //   1. We are explicitly asking for a PTY shell, or
    //   2. We don't specify shell type and are starting an interactive session.
    bool raw_stdin = (type_arg == kShellServiceArgPty || (type_arg.empty() && empty_command));

    std::string error;
    int fd = adb_connect(service_string, &error);
    if (fd < 0) {
        fprintf(stderr,"error: %s\n", error.c_str());
        return 1;
    }

    StdinReadArgs* args = new StdinReadArgs;
    if (!args) {
        LOG(ERROR) << "couldn't allocate StdinReadArgs object";
        return 1;
    }
    args->stdin_fd = STDIN_FILENO;
    args->write_fd = fd;
    args->raw_stdin = raw_stdin;
    args->escape_char = escape_char;
    if (use_shell_protocol) {
        args->protocol = std::make_unique<ShellProtocol>(args->write_fd);
    }

    if (raw_stdin) stdin_raw_init();

#if !defined(_WIN32)
    // Ensure our process is notified if the local window size changes.
    // We use sigaction(2) to ensure that the SA_RESTART flag is not set,
    // because the whole reason we're sending signals is to unblock the read(2)!
    // That also means we don't need to do anything in the signal handler:
    // the side effect of delivering the signal is all we need.
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = [](int) {};
    sa.sa_flags = 0;
    sigaction(SIGWINCH, &sa, nullptr);

    // Now block SIGWINCH in this thread (the main thread) and all threads spawned
    // from it. The stdin read thread will unblock this signal to ensure that it's
    // the thread that receives the signal.
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGWINCH);
    pthread_sigmask(SIG_BLOCK, &mask, nullptr);
#endif

    // TODO: combine read_and_dump with stdin_read_thread to make life simpler?
    std::thread(stdin_read_thread_loop, args).detach();
    int exit_code = read_and_dump(fd, use_shell_protocol);

    // TODO: properly exit stdin_read_thread_loop and close |fd|.

    // TODO: we should probably install signal handlers for this.
    // TODO: can we use atexit? even on Windows?
    if (raw_stdin) stdin_raw_restore();

    return exit_code;
}

static int adb_shell(int argc, const char** argv) {
    enum PtyAllocationMode { kPtyAuto, kPtyNo, kPtyYes, kPtyDefinitely };

    // Defaults.
    char escape_char = '~';                                                 // -e
    auto&& features = adb_get_feature_set_or_die();
    bool use_shell_protocol = CanUseFeature(*features, kFeatureShell2);     // -x
    PtyAllocationMode tty = use_shell_protocol ? kPtyAuto : kPtyDefinitely; // -t/-T

    // Parse shell-specific command-line options.
    argv[0] = "adb shell"; // So getopt(3) error messages start "adb shell".
#ifdef _WIN32
    // fixes "adb shell -l" crash on Windows, b/37284906
    __argv = const_cast<char**>(argv);
#endif
    optind = 1; // argv[0] is always "shell", so set `optind` appropriately.
    int opt;
    while ((opt = getopt(argc, const_cast<char**>(argv), "+e:ntTx")) != -1) {
        switch (opt) {
            case 'e':
                if (!(strlen(optarg) == 1 || strcmp(optarg, "none") == 0)) {
                    error_exit("-e requires a single-character argument or 'none'");
                }
                escape_char = (strcmp(optarg, "none") == 0) ? 0 : optarg[0];
                break;
            case 'n':
                close_stdin();
                break;
            case 'x':
                // This option basically asks for historical behavior, so set options that
                // correspond to the historical defaults. This is slightly weird in that -Tx
                // is fine (because we'll undo the -T) but -xT isn't, but that does seem to
                // be our least worst choice...
                use_shell_protocol = false;
                tty = kPtyDefinitely;
                escape_char = '~';
                break;
            case 't':
                // Like ssh, -t arguments are cumulative so that multiple -t's
                // are needed to force a PTY.
                tty = (tty >= kPtyYes) ? kPtyDefinitely : kPtyYes;
                break;
            case 'T':
                tty = kPtyNo;
                break;
            default:
                // getopt(3) already printed an error message for us.
                return 1;
        }
    }

    bool is_interactive = (optind == argc);

    std::string shell_type_arg = kShellServiceArgPty;
    if (tty == kPtyNo) {
        shell_type_arg = kShellServiceArgRaw;
    } else if (tty == kPtyAuto) {
        // If stdin isn't a TTY, default to a raw shell; this lets
        // things like `adb shell < my_script.sh` work as expected.
        // Non-interactive shells should also not have a pty.
        if (!unix_isatty(STDIN_FILENO) || !is_interactive) {
            shell_type_arg = kShellServiceArgRaw;
        }
    } else if (tty == kPtyYes) {
        // A single -t arg isn't enough to override implicit -T.
        if (!unix_isatty(STDIN_FILENO)) {
            fprintf(stderr,
                    "Remote PTY will not be allocated because stdin is not a terminal.\n"
                    "Use multiple -t options to force remote PTY allocation.\n");
            shell_type_arg = kShellServiceArgRaw;
        }
    }

    D("shell -e 0x%x t=%d use_shell_protocol=%s shell_type_arg=%s\n",
      escape_char, tty,
      use_shell_protocol ? "true" : "false",
      (shell_type_arg == kShellServiceArgPty) ? "pty" : "raw");

    // Raw mode is only supported when talking to a new device *and* using the shell protocol.
    if (!use_shell_protocol) {
        if (shell_type_arg != kShellServiceArgPty) {
            fprintf(stderr, "error: %s only supports allocating a pty\n",
                    !CanUseFeature(*features, kFeatureShell2) ? "device" : "-x");
            return 1;
        } else {
            // If we're not using the shell protocol, the type argument must be empty.
            shell_type_arg = "";
        }
    }

    std::string command;
    if (optind < argc) {
        // We don't escape here, just like ssh(1). http://b/20564385.
        command = android::base::Join(std::vector<const char*>(argv + optind, argv + argc), ' ');
    }

    std::string service_string = ShellServiceString(use_shell_protocol, shell_type_arg, command);
    return RemoteShell(use_shell_protocol, shell_type_arg, escape_char, command.empty(),
                       service_string);
}

static int adb_abb(int argc, const char** argv) {
    auto&& features = adb_get_feature_set_or_die();
    if (!CanUseFeature(*features, kFeatureAbb)) {
        error_exit("abb is not supported by the device");
    }

    optind = 1;  // argv[0] is always "abb", so set `optind` appropriately.

    // Defaults.
    constexpr char escape_char = '~';  // -e
    constexpr bool use_shell_protocol = true;
    constexpr auto shell_type_arg = kShellServiceArgRaw;
    constexpr bool empty_command = false;

    std::vector<const char*> args(argv + optind, argv + argc);
    std::string service_string = "abb:" + android::base::Join(args, ABB_ARG_DELIMETER);

    D("abb -e 0x%x [%*.s]\n", escape_char, static_cast<int>(service_string.size()),
      service_string.data());

    return RemoteShell(use_shell_protocol, shell_type_arg, escape_char, empty_command,
                       service_string);
}

static int adb_shell_noinput(int argc, const char** argv) {
#if !defined(_WIN32)
    unique_fd fd(adb_open("/dev/null", O_RDONLY));
    CHECK_NE(STDIN_FILENO, fd.get());
    dup2(fd.get(), STDIN_FILENO);
#endif
    return adb_shell(argc, argv);
}

static int adb_sideload_legacy(const char* filename, int in_fd, int size) {
    std::string error;
    unique_fd out_fd(adb_connect(android::base::StringPrintf("sideload:%d", size), &error));
    if (out_fd < 0) {
        fprintf(stderr, "adb: pre-KitKat sideload connection failed: %s\n", error.c_str());
        return -1;
    }

    int opt = CHUNK_SIZE;
    opt = adb_setsockopt(out_fd, SOL_SOCKET, SO_SNDBUF, &opt, sizeof(opt));

    char buf[CHUNK_SIZE];
    int total = size;
    while (size > 0) {
        unsigned xfer = (size > CHUNK_SIZE) ? CHUNK_SIZE : size;
        if (!ReadFdExactly(in_fd, buf, xfer)) {
            fprintf(stderr, "adb: failed to read data from %s: %s\n", filename, strerror(errno));
            return -1;
        }
        if (!WriteFdExactly(out_fd, buf, xfer)) {
            std::string error;
            adb_status(out_fd, &error);
            fprintf(stderr, "adb: failed to write data: %s\n", error.c_str());
            return -1;
        }
        size -= xfer;
        printf("sending: '%s' %4d%%    \r", filename, (int)(100LL - ((100LL * size) / (total))));
        fflush(stdout);
    }
    printf("\n");

    if (!adb_status(out_fd, &error)) {
        fprintf(stderr, "adb: error response: %s\n", error.c_str());
        return -1;
    }

    return 0;
}

#define SIDELOAD_HOST_BLOCK_SIZE (CHUNK_SIZE)

// Connects to the sideload / rescue service on the device (served by minadbd) and sends over the
// data in an OTA package.
//
// It uses a simple protocol as follows.
//
// - The connect message includes the total number of bytes in the file and a block size chosen by
//   us.
//
// - The other side sends the desired block number as eight decimal digits (e.g. "00000023" for
//   block 23). Blocks are numbered from zero.
//
// - We send back the data of the requested block. The last block is likely to be partial; when the
//   last block is requested we only send the part of the block that exists, it's not padded up to
//   the block size.
//
// - When the other side sends "DONEDONE" or "FAILFAIL" instead of a block number, we have done all
//   the data transfer.
//
static int adb_sideload_install(const char* filename, bool rescue_mode) {
    // TODO: use a LinePrinter instead...
    struct stat sb;
    if (stat(filename, &sb) == -1) {
        fprintf(stderr, "adb: failed to stat file %s: %s\n", filename, strerror(errno));
        return -1;
    }
    unique_fd package_fd(adb_open(filename, O_RDONLY));
    if (package_fd == -1) {
        fprintf(stderr, "adb: failed to open file %s: %s\n", filename, strerror(errno));
        return -1;
    }

    std::string service = android::base::StringPrintf(
            "%s:%" PRId64 ":%d", rescue_mode ? "rescue-install" : "sideload-host",
            static_cast<int64_t>(sb.st_size), SIDELOAD_HOST_BLOCK_SIZE);
    std::string error;
    unique_fd device_fd(adb_connect(service, &error));
    if (device_fd < 0) {
        fprintf(stderr, "adb: sideload connection failed: %s\n", error.c_str());

        if (rescue_mode) {
            return -1;
        }

        // If this is a small enough package, maybe this is an older device that doesn't
        // support sideload-host. Try falling back to the older (<= K) sideload method.
        if (sb.st_size > INT_MAX) {
            return -1;
        }
        fprintf(stderr, "adb: trying pre-KitKat sideload method...\n");
        return adb_sideload_legacy(filename, package_fd.get(), static_cast<int>(sb.st_size));
    }

    int opt = SIDELOAD_HOST_BLOCK_SIZE;
    adb_setsockopt(device_fd, SOL_SOCKET, SO_SNDBUF, &opt, sizeof(opt));

    char buf[SIDELOAD_HOST_BLOCK_SIZE];

    int64_t xfer = 0;
    int last_percent = -1;
    while (true) {
        if (!ReadFdExactly(device_fd, buf, 8)) {
            fprintf(stderr, "adb: failed to read command: %s\n", strerror(errno));
            return -1;
        }
        buf[8] = '\0';

        if (strcmp(kMinadbdServicesExitSuccess, buf) == 0 ||
            strcmp(kMinadbdServicesExitFailure, buf) == 0) {
            printf("\rTotal xfer: %.2fx%*s\n",
                   static_cast<double>(xfer) / (sb.st_size ? sb.st_size : 1),
                   static_cast<int>(strlen(filename) + 10), "");
            if (strcmp(kMinadbdServicesExitFailure, buf) == 0) {
                return 1;
            }
            return 0;
        }

        int64_t block = strtoll(buf, nullptr, 10);
        int64_t offset = block * SIDELOAD_HOST_BLOCK_SIZE;
        if (offset >= static_cast<int64_t>(sb.st_size)) {
            fprintf(stderr,
                    "adb: failed to read block %" PRId64 " at offset %" PRId64 ", past end %" PRId64
                    "\n",
                    block, offset, static_cast<int64_t>(sb.st_size));
            return -1;
        }

        size_t to_write = SIDELOAD_HOST_BLOCK_SIZE;
        if ((offset + SIDELOAD_HOST_BLOCK_SIZE) > static_cast<int64_t>(sb.st_size)) {
            to_write = sb.st_size - offset;
        }

        if (adb_lseek(package_fd, offset, SEEK_SET) != offset) {
            fprintf(stderr, "adb: failed to seek to package block: %s\n", strerror(errno));
            return -1;
        }
        if (!ReadFdExactly(package_fd, buf, to_write)) {
            fprintf(stderr, "adb: failed to read package block: %s\n", strerror(errno));
            return -1;
        }

        if (!WriteFdExactly(device_fd, buf, to_write)) {
            adb_status(device_fd, &error);
            fprintf(stderr, "adb: failed to write data '%s' *\n", error.c_str());
            return -1;
        }
        xfer += to_write;

        // For normal OTA packages, we expect to transfer every byte
        // twice, plus a bit of overhead (one read during
        // verification, one read of each byte for installation, plus
        // extra access to things like the zip central directory).
        // This estimate of the completion becomes 100% when we've
        // transferred ~2.13 (=100/47) times the package size.
        int percent = static_cast<int>(xfer * 47LL / (sb.st_size ? sb.st_size : 1));
        if (percent != last_percent) {
            printf("\rserving: '%s'  (~%d%%)    ", filename, percent);
            fflush(stdout);
            last_percent = percent;
        }
    }
}

static int adb_wipe_devices() {
    auto wipe_devices_message_size = strlen(kMinadbdServicesExitSuccess);
    std::string error;
    unique_fd fd(adb_connect(
            android::base::StringPrintf("rescue-wipe:userdata:%zu", wipe_devices_message_size),
            &error));
    if (fd < 0) {
        fprintf(stderr, "adb: wipe device connection failed: %s\n", error.c_str());
        return 1;
    }

    std::string message(wipe_devices_message_size, '\0');
    if (!ReadFdExactly(fd, message.data(), wipe_devices_message_size)) {
        fprintf(stderr, "adb: failed to read wipe result: %s\n", strerror(errno));
        return 1;
    }

    if (message == kMinadbdServicesExitSuccess) {
        return 0;
    }

    if (message != kMinadbdServicesExitFailure) {
        fprintf(stderr, "adb: got unexpected message from rescue wipe %s\n", message.c_str());
    }
    return 1;
}

static bool wait_for_device(const char* service,
                            std::optional<std::chrono::milliseconds> timeout = std::nullopt) {
    std::vector<std::string> components = android::base::Split(service, "-");
    if (components.size() < 3) {
        fprintf(stderr, "adb: couldn't parse 'wait-for' command: %s\n", service);
        return false;
    }

    // If the first thing after "wait-for-" wasn't a TRANSPORT, insert whatever
    // the current transport implies.
    if (components[2] != "usb" && components[2] != "local" && components[2] != "any") {
        TransportType t;
        adb_get_transport(&t, nullptr, nullptr);
        auto it = components.begin() + 2;
        if (t == kTransportUsb) {
            components.insert(it, "usb");
        } else if (t == kTransportLocal) {
            components.insert(it, "local");
        } else {
            components.insert(it, "any");
        }
    }

    // Stitch it back together and send it over...
    std::string cmd = format_host_command(android::base::Join(components, "-").c_str());
    if (timeout) {
        std::thread([timeout]() {
            std::this_thread::sleep_for(*timeout);
            fprintf(stderr, "timeout expired while waiting for device\n");
            _exit(1);
        }).detach();
    }
    return adb_command(cmd);
}

static bool adb_root(const char* command) {
    std::string error;

    TransportId transport_id;
    unique_fd fd(adb_connect(&transport_id, android::base::StringPrintf("%s:", command), &error));
    if (fd < 0) {
        fprintf(stderr, "adb: unable to connect for %s: %s\n", command, error.c_str());
        return false;
    }

    // Figure out whether we actually did anything.
    char buf[256];
    char* cur = buf;
    ssize_t bytes_left = sizeof(buf);
    while (bytes_left > 0) {
        ssize_t bytes_read = adb_read(fd, cur, bytes_left);
        if (bytes_read == 0) {
            break;
        } else if (bytes_read < 0) {
            fprintf(stderr, "adb: error while reading for %s: %s\n", command, strerror(errno));
            return false;
        }
        cur += bytes_read;
        bytes_left -= bytes_read;
    }

    if (bytes_left == 0) {
        fprintf(stderr, "adb: unexpected output length for %s\n", command);
        return false;
    }

    fwrite(buf, 1, sizeof(buf) - bytes_left, stdout);
    fflush(stdout);
    if (cur != buf && strstr(buf, "restarting") == nullptr) {
        return true;
    }

    // Wait for the device to go away.
    TransportType previous_type;
    const char* previous_serial;
    TransportId previous_id;
    adb_get_transport(&previous_type, &previous_serial, &previous_id);

    adb_set_transport(kTransportAny, nullptr, transport_id);
    wait_for_device("wait-for-disconnect");

    // Wait for the device to come back.
    // If we were using a specific transport ID, there's nothing we can wait for.
    if (previous_id == 0) {
        adb_set_transport(previous_type, previous_serial, 0);
        wait_for_device("wait-for-device", 12000ms);
    }

    return true;
}

int send_shell_command(const std::string& command, bool disable_shell_protocol,
                       StandardStreamsCallbackInterface* callback) {
    unique_fd fd;
    bool use_shell_protocol = false;

    while (true) {
        bool attempt_connection = true;

        // Use shell protocol if it's supported and the caller doesn't explicitly
        // disable it.
        if (!disable_shell_protocol) {
            auto&& features = adb_get_feature_set(nullptr);
            if (features) {
                use_shell_protocol = CanUseFeature(*features, kFeatureShell2);
            } else {
                // Device was unreachable.
                attempt_connection = false;
            }
        }

        if (attempt_connection) {
            std::string error;
            std::string service_string = ShellServiceString(use_shell_protocol, "", command);

            fd.reset(adb_connect(service_string, &error));
            if (fd >= 0) {
                break;
            }
        }

        fprintf(stderr, "- waiting for device -\n");
        if (!wait_for_device("wait-for-device")) {
            return 1;
        }
    }

    return read_and_dump(fd.get(), use_shell_protocol, callback);
}

static int logcat(int argc, const char** argv) {
    char* log_tags = getenv("ANDROID_LOG_TAGS");
    std::string quoted = escape_arg(log_tags == nullptr ? "" : log_tags);

    std::string cmd = "export ANDROID_LOG_TAGS=" + quoted + "; exec logcat";

    if (!strcmp(argv[0], "longcat")) {
        cmd += " -v long";
    }

    --argc;
    ++argv;
    while (argc-- > 0) {
        cmd += " " + escape_arg(*argv++);
    }

    return send_shell_command(cmd);
}

static void write_zeros(int bytes, borrowed_fd fd) {
    int old_stdin_mode = -1;
    int old_stdout_mode = -1;
    std::vector<char> buf(bytes);

    D("write_zeros(%d) -> %d", bytes, fd.get());

    stdinout_raw_prologue(-1, fd.get(), old_stdin_mode, old_stdout_mode);

    if (fd == STDOUT_FILENO) {
        fwrite(buf.data(), 1, bytes, stdout);
        fflush(stdout);
    } else {
        adb_write(fd, buf.data(), bytes);
    }

    stdinout_raw_prologue(-1, fd.get(), old_stdin_mode, old_stdout_mode);

    D("write_zeros() finished");
}

static int backup(int argc, const char** argv) {
    fprintf(stdout, "WARNING: adb backup is deprecated and may be removed in a future release\n");

    const char* filename = "backup.ab";

    /* find, extract, and use any -f argument */
    for (int i = 1; i < argc; i++) {
        if (!strcmp("-f", argv[i])) {
            if (i == argc - 1) error_exit("backup -f passed with no filename");
            filename = argv[i+1];
            for (int j = i+2; j <= argc; ) {
                argv[i++] = argv[j++];
            }
            argc -= 2;
            argv[argc] = nullptr;
        }
    }

    // Bare "adb backup" or "adb backup -f filename" are not valid invocations ---
    // a list of packages is required.
    if (argc < 2) error_exit("backup either needs a list of packages or -all/-shared");

    adb_unlink(filename);
    unique_fd outFd(adb_creat(filename, 0640));
    if (outFd < 0) {
        fprintf(stderr, "adb: backup unable to create file '%s': %s\n", filename, strerror(errno));
        return EXIT_FAILURE;
    }

    std::string cmd = "backup:";
    --argc;
    ++argv;
    while (argc-- > 0) {
        cmd += " " + escape_arg(*argv++);
    }

    D("backup. filename=%s cmd=%s", filename, cmd.c_str());
    std::string error;
    unique_fd fd(adb_connect(cmd, &error));
    if (fd < 0) {
        fprintf(stderr, "adb: unable to connect for backup: %s\n", error.c_str());
        return EXIT_FAILURE;
    }

    fprintf(stdout, "Now unlock your device and confirm the backup operation...\n");
    fflush(stdout);

    copy_to_file(fd.get(), outFd.get());
    return EXIT_SUCCESS;
}

static int restore(int argc, const char** argv) {
    fprintf(stdout, "WARNING: adb restore is deprecated and may be removed in a future release\n");

    if (argc < 2) {
        error_exit("usage: adb restore FILENAME [ARG]...");
    }

    const char* filename = argv[1];
    unique_fd tarFd(adb_open(filename, O_RDONLY));
    if (tarFd < 0) {
        fprintf(stderr, "adb: unable to open file %s: %s\n", filename, strerror(errno));
        return -1;
    }

    std::string cmd = "restore:";
    argc -= 2;
    argv += 2;
    while (argc-- > 0) {
        cmd += " " + escape_arg(*argv++);
    }

    D("restore. filename=%s cmd=%s", filename, cmd.c_str());

    std::string error;
    unique_fd fd(adb_connect(cmd, &error));
    if (fd < 0) {
        fprintf(stderr, "adb: unable to connect for restore: %s\n", error.c_str());
        return -1;
    }

    fprintf(stdout, "Now unlock your device and confirm the restore operation.\n");
    fflush(stdout);

    copy_to_file(tarFd.get(), fd.get());

    // Provide an in-band EOD marker in case the archive file is malformed
    write_zeros(512 * 2, fd);

    // Wait until the other side finishes, or it'll get sent SIGHUP.
    copy_to_file(fd.get(), STDOUT_FILENO);
    return 0;
}

static CompressionType parse_compression_type(const std::string& str, bool allow_numbers) {
    if (allow_numbers) {
        if (str == "0") {
            return CompressionType::None;
        } else if (str == "1") {
            return CompressionType::Any;
        }
    }

    if (str == "any") {
        return CompressionType::Any;
    } else if (str == "none") {
        return CompressionType::None;
    }

    if (str == "brotli") {
        return CompressionType::Brotli;
    } else if (str == "lz4") {
        return CompressionType::LZ4;
    } else if (str == "zstd") {
        return CompressionType::Zstd;
    }

    error_exit("unexpected compression type %s", str.c_str());
}

static void parse_push_pull_args(const char** arg, int narg, std::vector<const char*>* srcs,
                                 const char** dst, bool* copy_attrs, bool* sync, bool* quiet,
                                 CompressionType* compression, bool* dry_run) {
    *copy_attrs = false;
    if (const char* adb_compression = getenv("ADB_COMPRESSION")) {
        *compression = parse_compression_type(adb_compression, true);
    }

    srcs->clear();
    bool ignore_flags = false;
    while (narg > 0) {
        if (ignore_flags || *arg[0] != '-') {
            srcs->push_back(*arg);
        } else {
            if (!strcmp(*arg, "-p")) {
                // Silently ignore for backwards compatibility.
            } else if (!strcmp(*arg, "-a")) {
                *copy_attrs = true;
            } else if (!strcmp(*arg, "-z")) {
                if (narg < 2) {
                    error_exit("-z requires an argument");
                }
                *compression = parse_compression_type(*++arg, false);
                --narg;
            } else if (!strcmp(*arg, "-Z")) {
                *compression = CompressionType::None;
            } else if (dry_run && !strcmp(*arg, "-n")) {
                *dry_run = true;
            } else if (!strcmp(*arg, "--sync")) {
                if (sync != nullptr) {
                    *sync = true;
                }
            } else if (!strcmp(*arg, "-q")) {
                *quiet = true;
            } else if (!strcmp(*arg, "--")) {
                ignore_flags = true;
            } else {
                error_exit("unrecognized option '%s'", *arg);
            }
        }
        ++arg;
        --narg;
    }

    if (srcs->size() > 1) {
        *dst = srcs->back();
        srcs->pop_back();
    }
}

static int adb_connect_command(const std::string& command, TransportId* transport,
                               StandardStreamsCallbackInterface* callback) {
    std::string error;
    unique_fd fd(adb_connect(transport, command, &error));
    if (fd < 0) {
        fprintf(stderr, "error: %s\n", error.c_str());
        return 1;
    }
    read_and_dump(fd, false, callback);
    return 0;
}

static int adb_connect_command(const std::string& command, TransportId* transport = nullptr) {
    return adb_connect_command(command, transport, &DEFAULT_STANDARD_STREAMS_CALLBACK);
}

// A class to convert server status binary protobuf to text protobuf.
class AdbServerStateStreamsCallback : public DefaultStandardStreamsCallback {
  public:
    AdbServerStateStreamsCallback() : DefaultStandardStreamsCallback(nullptr, nullptr) {}

    bool OnStdoutReceived(const char* buffer, size_t length) override {
        return SendTo(&output_, nullptr, buffer, length, false);
    }

    int Done(int status) {
        if (output_.size() < 4) {
            return SendTo(nullptr, stdout, output_.data(), output_.length(), false);
        }

        // Skip the 4-hex prefix
        std::string binary_proto_bytes{output_.substr(4)};

        ::adb::proto::AdbServerStatus binary_proto;
        binary_proto.ParseFromString(binary_proto_bytes);

        std::string string_proto;
        google::protobuf::TextFormat::PrintToString(binary_proto, &string_proto);

        return SendTo(nullptr, stdout, string_proto.data(), string_proto.length(), false);
    }

  private:
    std::string output_;
    DISALLOW_COPY_AND_ASSIGN(AdbServerStateStreamsCallback);
};

static int adb_connect_command_bidirectional(const std::string& command) {
    std::string error;
    unique_fd fd(adb_connect(command, &error));
    if (fd < 0) {
        fprintf(stderr, "error: %s\n", error.c_str());
        return 1;
    }

    static constexpr auto forward = [](int src, int sink, bool exit_on_end) {
        char buf[4096];
        while (true) {
            int rc = adb_read(src, buf, sizeof(buf));
            if (rc == 0) {
                if (exit_on_end) {
                    exit(0);
                } else {
                    adb_shutdown(sink, SHUT_WR);
                }
                return;
            } else if (rc < 0) {
                perror_exit("read failed");
            }
            if (!WriteFdExactly(sink, buf, rc)) {
                perror_exit("write failed");
            }
        }
    };

    std::thread read(forward, fd.get(), STDOUT_FILENO, true);
    std::thread write(forward, STDIN_FILENO, fd.get(), false);
    read.join();
    write.join();
    return 0;
}

const std::optional<FeatureSet>& adb_get_feature_set_or_die(void) {
    std::string error;
    const std::optional<FeatureSet>& features = adb_get_feature_set(&error);
    if (!features) {
        error_exit("%s", error.c_str());
    }
    return features;
}

// Helper function to handle processing of shell service commands:
// remount, disable/enable-verity. There's only one "feature",
// but they were all moved from adbd to external binaries in the
// same release.
static int process_remount_or_verity_service(const int argc, const char** argv) {
    auto&& features = adb_get_feature_set_or_die();
    if (CanUseFeature(*features, kFeatureRemountShell)) {
        std::vector<const char*> args = {"shell"};
        args.insert(args.cend(), argv, argv + argc);
        return adb_shell_noinput(args.size(), args.data());
    } else if (argc > 1) {
        auto command = android::base::StringPrintf("%s:%s", argv[0], argv[1]);
        return adb_connect_command(command);
    } else {
        return adb_connect_command(std::string(argv[0]) + ":");
    }
}

static int adb_query_command(const std::string& command) {
    std::string result;
    std::string error;
    if (!adb_query(command, &result, &error)) {
        fprintf(stderr, "error: %s\n", error.c_str());
        return 1;
    }
    printf("%s\n", result.c_str());
    return 0;
}

// Disallow stdin, stdout, and stderr.
static bool _is_valid_ack_reply_fd(const int ack_reply_fd) {
#ifdef _WIN32
    const HANDLE ack_reply_handle = cast_int_to_handle(ack_reply_fd);
    return (GetStdHandle(STD_INPUT_HANDLE) != ack_reply_handle) &&
           (GetStdHandle(STD_OUTPUT_HANDLE) != ack_reply_handle) &&
           (GetStdHandle(STD_ERROR_HANDLE) != ack_reply_handle);
#else
    return ack_reply_fd > 2;
#endif
}

static bool _is_valid_os_fd(int fd) {
    // Disallow invalid FDs and stdin/out/err as well.
    if (fd < 3) {
        return false;
    }
#ifdef _WIN32
    auto handle = (HANDLE)fd;
    DWORD info = 0;
    if (GetHandleInformation(handle, &info) == 0) {
        return false;
    }
#else
    int flags = fcntl(fd, F_GETFD);
    if (flags == -1) {
        return false;
    }
#endif
    return true;
}

bool forward_dest_is_featured(const std::string& dest, std::string* error) {
    auto features = adb_get_feature_set_or_die();

    if (android::base::StartsWith(dest, "dev-raw:")) {
        if (!CanUseFeature(*features, kFeatureDevRaw)) {
            *error = "dev-raw is not supported by the device";
            return false;
        }
    }

    return true;
}

int adb_commandline(int argc, const char** argv) {
    bool no_daemon = false;
    bool is_daemon = false;
    bool is_server = false;
    int r;
    TransportType transport_type = kTransportAny;
    int ack_reply_fd = -1;

#if !defined(_WIN32)
    // We'd rather have EPIPE than SIGPIPE.
    signal(SIGPIPE, SIG_IGN);
#endif

    const char* server_host_str = nullptr;
    const char* server_port_str = nullptr;
    const char* server_socket_str = nullptr;
    const char* one_device_str = nullptr;

    // We need to check for -d and -e before we look at $ANDROID_SERIAL.
    const char* serial = nullptr;
    TransportId transport_id = 0;

    while (argc > 0) {
        if (!strcmp(argv[0], "server")) {
            is_server = true;
        } else if (!strcmp(argv[0], "nodaemon")) {
            no_daemon = true;
        } else if (!strcmp(argv[0], "fork-server")) {
            /* this is a special flag used only when the ADB client launches the ADB Server */
            is_daemon = true;
        } else if (!strcmp(argv[0], "--reply-fd")) {
            if (argc < 2) error_exit("--reply-fd requires an argument");
            const char* reply_fd_str = argv[1];
            --argc;
            ++argv;
            ack_reply_fd = strtol(reply_fd_str, nullptr, 10);
            if (!_is_valid_ack_reply_fd(ack_reply_fd)) {
                fprintf(stderr, "adb: invalid reply fd \"%s\"\n", reply_fd_str);
                return 1;
            }
        } else if (!strcmp(argv[0], "--one-device")) {
            if (argc < 2) error_exit("--one-device requires an argument");
            one_device_str = argv[1];
            --argc;
            ++argv;
        } else if (!strncmp(argv[0], "-s", 2)) {
            if (isdigit(argv[0][2])) {
                serial = argv[0] + 2;
            } else {
                if (argc < 2 || argv[0][2] != '\0') error_exit("-s requires an argument");
                serial = argv[1];
                --argc;
                ++argv;
            }
        } else if (!strncmp(argv[0], "-t", 2)) {
            const char* id;
            if (isdigit(argv[0][2])) {
                id = argv[0] + 2;
            } else {
                if (argc < 2 || argv[0][2] != '\0') error_exit("-t requires an argument");
                id = argv[1];
                --argc;
                ++argv;
            }
            transport_id = strtoll(id, const_cast<char**>(&id), 10);
            if (*id != '\0') {
                error_exit("invalid transport id");
            }
        } else if (!strcmp(argv[0], "-d")) {
            transport_type = kTransportUsb;
        } else if (!strcmp(argv[0], "-e")) {
            transport_type = kTransportLocal;
        } else if (!strcmp(argv[0], "-a")) {
            gListenAll = true;
        } else if (!strncmp(argv[0], "-H", 2)) {
            if (argv[0][2] == '\0') {
                if (argc < 2) error_exit("-H requires an argument");
                server_host_str = argv[1];
                --argc;
                ++argv;
            } else {
                server_host_str = argv[0] + 2;
            }
        } else if (!strncmp(argv[0], "-P", 2)) {
            if (argv[0][2] == '\0') {
                if (argc < 2) error_exit("-P requires an argument");
                server_port_str = argv[1];
                --argc;
                ++argv;
            } else {
                server_port_str = argv[0] + 2;
            }
        } else if (!strcmp(argv[0], "-L")) {
            if (argc < 2) error_exit("-L requires an argument");
            server_socket_str = argv[1];
            --argc;
            ++argv;
        } else if (strcmp(argv[0], "--exit-on-write-error") == 0) {
            DEFAULT_STANDARD_STREAMS_CALLBACK.ReturnErrors(true);
        } else {
            /* out of recognized modifiers and flags */
            break;
        }
        --argc;
        ++argv;
    }

    if ((server_host_str || server_port_str) && server_socket_str) {
        error_exit("-L is incompatible with -H or -P");
    }

    // If -L, -H, or -P are specified, ignore environment variables.
    // Otherwise, prefer ADB_SERVER_SOCKET over ANDROID_ADB_SERVER_ADDRESS/PORT.
    if (!server_host_str && !server_port_str && !server_socket_str) {
        server_socket_str = getenv("ADB_SERVER_SOCKET");
    }

    if (!server_socket_str) {
        // tcp:1234 and tcp:localhost:1234 are different with -a, so don't default to localhost
        server_host_str = server_host_str ? server_host_str : getenv("ANDROID_ADB_SERVER_ADDRESS");

        int server_port = kDefaultServerPort;
        server_port_str = server_port_str ? server_port_str : getenv("ANDROID_ADB_SERVER_PORT");
        if (server_port_str && strlen(server_port_str) > 0) {
            if (!android::base::ParseInt(server_port_str, &server_port, 1, 65535)) {
                error_exit(
                        "$ANDROID_ADB_SERVER_PORT must be a positive number less than 65535: "
                        "got \"%s\"",
                        server_port_str);
            }
        }

        int rc;
        char* temp;
        if (server_host_str) {
            rc = asprintf(&temp, "tcp:%s:%d", server_host_str, server_port);
        } else {
            rc = asprintf(&temp, "tcp:%d", server_port);
        }
        if (rc < 0) {
            LOG(FATAL) << "failed to allocate server socket specification";
        }
        server_socket_str = temp;
    }
    VLOG(ADB) << "Using server socket: " << server_socket_str;

    bool server_start =
            is_daemon || is_server || (argc > 0 && strcmp(argv[0], "start-server") == 0);
    if (one_device_str && !server_start) {
        error_exit("--one-device is only allowed when starting a server.");
    }

    adb_set_one_device(one_device_str);
    adb_set_socket_spec(server_socket_str);

    // If none of -d, -e, or -s were specified, try $ANDROID_SERIAL.
    if (transport_type == kTransportAny && serial == nullptr) {
        serial = getenv("ANDROID_SERIAL");
    }

    adb_set_transport(transport_type, serial, transport_id);

    if (is_server) {
        if (no_daemon || is_daemon) {
            if (is_daemon && (ack_reply_fd == -1)) {
                fprintf(stderr, "reply fd for adb server to client communication not specified.\n");
                return 1;
            }
            r = adb_server_main(is_daemon, server_socket_str, one_device_str, ack_reply_fd);
        } else {
            r = launch_server(server_socket_str, one_device_str);
        }
        if (r) {
            fprintf(stderr,"* could not start server *\n");
        }
        return r;
    }

    if (argc == 0) {
        help();
        return 1;
    }

    /* handle wait-for-* prefix */
    if (!strncmp(argv[0], "wait-for-", strlen("wait-for-"))) {
        const char* service = argv[0];

        if (!wait_for_device(service)) {
            return 1;
        }

        // Allow a command to be run after wait-for-device,
        // e.g. 'adb wait-for-device shell'.
        if (argc == 1) {
            return 0;
        }

        /* Fall through */
        --argc;
        ++argv;
    }

    /* adb_connect() commands */
    if (!strcmp(argv[0], "devices")) {
        const char *listopt;
        if (argc < 2) {
            listopt = "";
        } else if (argc == 2 && !strcmp(argv[1], "-l")) {
            listopt = argv[1];
        } else {
            error_exit("adb devices [-l]");
        }

        std::string query = android::base::StringPrintf("host:%s%s", argv[0], listopt);
        std::string error;
        if (!adb_check_server_version(&error)) {
            error_exit("failed to check server version: %s", error.c_str());
        }
        printf("List of devices attached\n");
        return adb_query_command(query);
    } else if (!strcmp(argv[0], "transport-id")) {
        TransportId transport_id;
        std::string error;
        unique_fd fd(adb_connect(&transport_id, "host:features", &error, true));
        if (fd == -1) {
            error_exit("%s", error.c_str());
        }
        printf("%" PRIu64 "\n", transport_id);
        return 0;
    } else if (!strcmp(argv[0], "connect")) {
        if (argc != 2) error_exit("usage: adb connect HOST[:PORT]");

        std::string query = android::base::StringPrintf("host:connect:%s", argv[1]);
        return adb_query_command(query);
    } else if (!strcmp(argv[0], "disconnect")) {
        if (argc > 2) error_exit("usage: adb disconnect [HOST[:PORT]]");

        std::string query = android::base::StringPrintf("host:disconnect:%s",
                                                        (argc == 2) ? argv[1] : "");
        return adb_query_command(query);
    } else if (!strcmp(argv[0], "abb")) {
        return adb_abb(argc, argv);
    } else if (!strcmp(argv[0], "pair")) {
        if (argc < 2 || argc > 3) error_exit("usage: adb pair HOST[:PORT] [PAIRING CODE]");

        std::string password;
        if (argc == 2) {
            printf("Enter pairing code: ");
            fflush(stdout);
            if (!std::getline(std::cin, password) || password.empty()) {
                error_exit("No pairing code provided");
            }
        } else {
            password = argv[2];
        }
        std::string query =
                android::base::StringPrintf("host:pair:%s:%s", password.c_str(), argv[1]);

        return adb_query_command(query);
    } else if (!strcmp(argv[0], "emu")) {
        return adb_send_emulator_command(argc, argv, serial);
    } else if (!strcmp(argv[0], "shell")) {
        return adb_shell(argc, argv);
    } else if (!strcmp(argv[0], "exec-in") || !strcmp(argv[0], "exec-out")) {
        int exec_in = !strcmp(argv[0], "exec-in");

        if (argc < 2) error_exit("usage: adb %s command", argv[0]);

        std::string cmd = "exec:";
        cmd += argv[1];
        argc -= 2;
        argv += 2;
        while (argc-- > 0) {
            cmd += " " + escape_arg(*argv++);
        }

        std::string error;
        unique_fd fd(adb_connect(cmd, &error));
        if (fd < 0) {
            fprintf(stderr, "error: %s\n", error.c_str());
            return -1;
        }

        if (exec_in) {
            copy_to_file(STDIN_FILENO, fd.get());
        } else {
            copy_to_file(fd.get(), STDOUT_FILENO);
        }
        return 0;
    } else if (!strcmp(argv[0], "kill-server")) {
        return adb_kill_server() ? 0 : 1;
    } else if (!strcmp(argv[0], "sideload")) {
        if (argc != 2) error_exit("sideload requires an argument");
        if (adb_sideload_install(argv[1], false /* rescue_mode */)) {
            return 1;
        } else {
            return 0;
        }
    } else if (!strcmp(argv[0], "rescue")) {
        // adb rescue getprop
        // adb rescue getprop <prop>
        // adb rescue install <filename>
        // adb rescue wipe userdata
        if (argc < 2) error_exit("rescue requires at least one argument");
        if (!strcmp(argv[1], "getprop")) {
            if (argc == 2) {
                return adb_connect_command("rescue-getprop:");
            }
            if (argc == 3) {
                return adb_connect_command(
                        android::base::StringPrintf("rescue-getprop:%s", argv[2]));
            }
            error_exit("invalid rescue getprop arguments");
        } else if (!strcmp(argv[1], "install")) {
            if (argc != 3) error_exit("rescue install requires two arguments");
            if (adb_sideload_install(argv[2], true /* rescue_mode */) != 0) {
                return 1;
            }
        } else if (!strcmp(argv[1], "wipe")) {
            if (argc != 3 || strcmp(argv[2], "userdata") != 0) {
                error_exit("invalid rescue wipe arguments");
            }
            return adb_wipe_devices();
        } else {
            error_exit("invalid rescue argument");
        }
        return 0;
    } else if (!strcmp(argv[0], "tcpip")) {
        if (argc != 2) error_exit("tcpip requires an argument");
        int port;
        if (!android::base::ParseInt(argv[1], &port, 1, 65535)) {
            error_exit("tcpip: invalid port: %s", argv[1]);
        }
        return adb_connect_command(android::base::StringPrintf("tcpip:%d", port));
    } else if (!strcmp(argv[0], "remount") || !strcmp(argv[0], "disable-verity") ||
               !strcmp(argv[0], "enable-verity")) {
        return process_remount_or_verity_service(argc, argv);
    } else if (!strcmp(argv[0], "reboot") || !strcmp(argv[0], "reboot-bootloader") ||
               !strcmp(argv[0], "reboot-fastboot") || !strcmp(argv[0], "usb")) {
        std::string command;
        if (!strcmp(argv[0], "reboot-bootloader")) {
            command = "reboot:bootloader";
        } else if (!strcmp(argv[0], "reboot-fastboot")) {
            command = "reboot:fastboot";
        } else if (argc > 1) {
            command = android::base::StringPrintf("%s:%s", argv[0], argv[1]);
        } else {
            command = android::base::StringPrintf("%s:", argv[0]);
        }
        return adb_connect_command(command);
    } else if (!strcmp(argv[0], "root") || !strcmp(argv[0], "unroot")) {
        return adb_root(argv[0]) ? 0 : 1;
    } else if (!strcmp(argv[0], "bugreport")) {
        Bugreport bugreport;
        return bugreport.DoIt(argc, argv);
    } else if (!strcmp(argv[0], "forward") || !strcmp(argv[0], "reverse")) {
        bool reverse = !strcmp(argv[0], "reverse");
        --argc;
        if (argc < 1) error_exit("%s requires an argument", argv[0]);
        ++argv;

        // Determine the <host-prefix> for this command.
        std::string host_prefix;
        if (reverse) {
            host_prefix = "reverse:";
        } else {
            host_prefix = "host:";
        }

        std::string cmd, error_message;
        if (strcmp(argv[0], "--list") == 0) {
            if (argc != 1) error_exit("--list doesn't take any arguments");
            return adb_query_command(host_prefix + "list-forward");
        } else if (strcmp(argv[0], "--remove-all") == 0) {
            if (argc != 1) error_exit("--remove-all doesn't take any arguments");
            cmd = "killforward-all";
        } else if (strcmp(argv[0], "--remove") == 0) {
            // forward --remove <local>
            if (argc != 2) error_exit("--remove requires an argument");
            cmd = std::string("killforward:") + argv[1];
        } else if (strcmp(argv[0], "--no-rebind") == 0) {
            // forward --no-rebind <local> <remote>
            if (argc != 3) error_exit("--no-rebind takes two arguments");
            if (forward_targets_are_valid(argv[1], argv[2], &error_message) &&
                forward_dest_is_featured(argv[2], &error_message)) {
                cmd = std::string("forward:norebind:") + argv[1] + ";" + argv[2];
            }
        } else {
            // forward <local> <remote>
            if (argc != 2) error_exit("forward takes two arguments");
            if (forward_targets_are_valid(argv[0], argv[1], &error_message) &&
                forward_dest_is_featured(argv[1], &error_message)) {
                cmd = std::string("forward:") + argv[0] + ";" + argv[1];
            }
        }

        if (!error_message.empty()) {
            error_exit("error: %s", error_message.c_str());
        }

        unique_fd fd(adb_connect(nullptr, host_prefix + cmd, &error_message, true));
        if (fd < 0 || !adb_status(fd.get(), &error_message)) {
            error_exit("error: %s", error_message.c_str());
        }

        // Server or device may optionally return a resolved TCP port number.
        std::string resolved_port;
        if (ReadProtocolString(fd, &resolved_port, &error_message) && !resolved_port.empty()) {
            printf("%s\n", resolved_port.c_str());
        }

        ReadOrderlyShutdown(fd);
        return 0;
    } else if (!strcmp(argv[0], "mdns")) {
        --argc;
        if (argc < 1) error_exit("mdns requires an argument");
        ++argv;

        std::string error;
        if (!adb_check_server_version(&error)) {
            error_exit("failed to check server version: %s", error.c_str());
        }

        if (!strcmp(argv[0], "check")) {
            if (argc != 1) {
                error_exit("mdns %s doesn't take any arguments", argv[0]);
            }
            return adb_query_command("host:mdns:check");
        } else if (!strcmp(argv[0], "services")) {
            if (argc != 1) {
                error_exit("mdns %s doesn't take any arguments", argv[0]);
            }
            printf("List of discovered mdns services\n");
            return adb_query_command("host:mdns:services");
        } else if (!strcmp(argv[0], "track-services")) {
            if (argc != 2) {
                error_exit("mdns %s take two arguments", argv[0]);
            }

            std::string service = "host:"s + HostServices::kTrackMdnsServices;
            if (!strcmp(argv[1], "--proto-binary")) {
                adb_connect_command(service);
            } else if (!strcmp(argv[1], "--proto-text")) {
                ProtoBinaryToText<adb::proto::MdnsServices> callback("\nServices:\n");
                adb_connect_command(service, nullptr, &callback);
            } else {
                error_exit("unknown mdns command [%s] flag '%s'", argv[0], argv[1]);
            }
        } else {
            error_exit("unknown mdns command [%s]", argv[0]);
        }
    }
    /* do_sync_*() commands */
    else if (!strcmp(argv[0], "ls")) {
        if (argc != 2) error_exit("ls requires an argument");
        return do_sync_ls(argv[1]) ? 0 : 1;
    } else if (!strcmp(argv[0], "push")) {
        bool copy_attrs = false;
        bool sync = false;
        bool dry_run = false;
        bool quiet = false;
        CompressionType compression = CompressionType::Any;
        std::vector<const char*> srcs;
        const char* dst = nullptr;

        parse_push_pull_args(&argv[1], argc - 1, &srcs, &dst, &copy_attrs, &sync, &quiet,
                             &compression, &dry_run);
        if (srcs.empty() || !dst) {
            error_exit("push requires <source> and <destination> arguments");
        }

        return do_sync_push(srcs, dst, sync, compression, dry_run, quiet) ? 0 : 1;
    } else if (!strcmp(argv[0], "pull")) {
        bool copy_attrs = false;
        bool quiet = false;
        CompressionType compression = CompressionType::None;
        std::vector<const char*> srcs;
        const char* dst = ".";

        parse_push_pull_args(&argv[1], argc - 1, &srcs, &dst, &copy_attrs, nullptr, &quiet,
                             &compression, nullptr);
        if (srcs.empty()) error_exit("pull requires an argument");
        return do_sync_pull(srcs, dst, copy_attrs, compression, nullptr, quiet) ? 0 : 1;
    } else if (!strcmp(argv[0], "install")) {
        if (argc < 2) error_exit("install requires an argument");
        return install_app(argc, argv);
    } else if (!strcmp(argv[0], "install-multiple")) {
        if (argc < 2) error_exit("install-multiple requires an argument");
        return install_multiple_app(argc, argv);
    } else if (!strcmp(argv[0], "install-multi-package")) {
        if (argc < 2) error_exit("install-multi-package requires an argument");
        return install_multi_package(argc, argv);
    } else if (!strcmp(argv[0], "uninstall")) {
        if (argc < 2) error_exit("uninstall requires an argument");
        return uninstall_app(argc, argv);
    } else if (!strcmp(argv[0], "sync")) {
        std::string src;
        bool list_only = false;
        bool dry_run = false;
        bool quiet = false;
        CompressionType compression = CompressionType::Any;

        if (const char* adb_compression = getenv("ADB_COMPRESSION"); adb_compression) {
            compression = parse_compression_type(adb_compression, true);
        }

        int opt;
        while ((opt = getopt(argc, const_cast<char**>(argv), "lnz:Zq")) != -1) {
            switch (opt) {
                case 'l':
                    list_only = true;
                    break;
                case 'n':
                    dry_run = true;
                    break;
                case 'z':
                    compression = parse_compression_type(optarg, false);
                    break;
                case 'Z':
                    compression = CompressionType::None;
                    break;
                case 'q':
                    quiet = true;
                    break;
                default:
                    error_exit("usage: adb sync [-l] [-n]  [-z ALGORITHM] [-Z] [-q] [PARTITION]");
            }
        }

        if (optind == argc) {
            src = "all";
        } else if (optind + 1 == argc) {
            src = argv[optind];
        } else {
            error_exit("usage: adb sync [-l] [-n] [-z ALGORITHM] [-Z] [-q] [PARTITION]");
        }

        std::vector<std::string> partitions{"data",   "odm",        "oem",   "product",
                                            "system", "system_ext", "vendor"};
        bool found = false;
        for (const auto& partition : partitions) {
            if (src == "all" || src == partition || (src == "/" + partition)) {
                std::string src_dir{product_file(partition)};
                if (!directory_exists(src_dir)) continue;
                found = true;
                if (!do_sync_sync(src_dir, "/" + partition, list_only, compression, dry_run, quiet)) {
                    return 1;
                }
            }
        }
        if (!found) error_exit("don't know how to sync %s partition", src.c_str());
        return 0;
    }
    /* passthrough commands */
    else if (!strcmp(argv[0], "get-state") || !strcmp(argv[0], "get-serialno") ||
             !strcmp(argv[0], "get-devpath")) {
        return adb_query_command(format_host_command(argv[0]));
    }
    /* other commands */
    else if (!strcmp(argv[0], "logcat") || !strcmp(argv[0], "lolcat") ||
             !strcmp(argv[0], "longcat")) {
        return logcat(argc, argv);
    } else if (!strcmp(argv[0], "start-server")) {
        std::string error;
        const int result = adb_connect("host:start-server", &error);
        if (result < 0) {
            fprintf(stderr, "error: %s\n", error.c_str());
        }
        return result;
    } else if (!strcmp(argv[0], "backup")) {
        return backup(argc, argv);
    } else if (!strcmp(argv[0], "restore")) {
        return restore(argc, argv);
    } else if (!strcmp(argv[0], "keygen")) {
        if (argc != 2) error_exit("keygen requires an argument");
        // Always print key generation information for keygen command.
        adb_trace_enable(AUTH);
        return adb_auth_keygen(argv[1]);
    } else if (!strcmp(argv[0], "pubkey")) {
        if (argc != 2) error_exit("pubkey requires an argument");
        return adb_auth_pubkey(argv[1]);
    } else if (!strcmp(argv[0], "jdwp")) {
        return adb_connect_command("jdwp");
    } else if (!strcmp(argv[0], "track-jdwp")) {
        return adb_connect_command("track-jdwp");
    } else if (!strcmp(argv[0], "track-app")) {
        auto&& features = adb_get_feature_set_or_die();
        if (!CanUseFeature(*features, kFeatureTrackApp)) {
            error_exit("track-app is not supported by the device");
        }
        ProtoBinaryToText<adb::proto::AppProcesses> callback("\nProcesses:\n");
        if (argc == 1) {
            return adb_connect_command("track-app", nullptr, &callback);
        } else if (argc == 2) {
            if (!strcmp(argv[1], "--proto-binary")) {
                return adb_connect_command("track-app");
            } else if (!strcmp(argv[1], "--proto-text")) {
                return adb_connect_command("track-app", nullptr, &callback);
            }
        } else {
            error_exit("usage: adb track-app [--proto-binary][--proto-text]");
        }
    } else if (!strcmp(argv[0], "track-devices")) {
        const char* listopt;
        if (argc < 2) {
            listopt = "";
        } else {
            if (!strcmp(argv[1], "-l")) {
                listopt = argv[1];
            } else if (!strcmp(argv[1], "--proto-text")) {
                listopt = "-proto-text";
            } else if (!strcmp(argv[1], "--proto-binary")) {
                listopt = "-proto-binary";
            } else {
                error_exit("usage: adb track-devices [-l][--proto-text][--proto-binary]");
            }
        }
        std::string query = android::base::StringPrintf("host:track-devices%s", listopt);
        return adb_connect_command(query);
    } else if (!strcmp(argv[0], "raw")) {
        if (argc != 2) {
            error_exit("usage: adb raw SERVICE");
        }
        return adb_connect_command_bidirectional(argv[1]);
    }

    /* "adb /?" is a common idiom under Windows */
    else if (!strcmp(argv[0], "--help") || !strcmp(argv[0], "help") || !strcmp(argv[0], "/?")) {
        help();
        return 0;
    } else if (!strcmp(argv[0], "--version") || !strcmp(argv[0], "version")) {
        fprintf(stdout, "%s", adb_version().c_str());
        return 0;
    } else if (!strcmp(argv[0], "features")) {
        // Only list the features common to both the adb client and the device.
        auto&& features = adb_get_feature_set_or_die();

        for (const std::string& name : *features) {
            if (CanUseFeature(*features, name)) {
                printf("%s\n", name.c_str());
            }
        }
        return 0;
    } else if (!strcmp(argv[0], "host-features")) {
        return adb_query_command("host:host-features");
    } else if (!strcmp(argv[0], "reconnect")) {
        if (argc == 1) {
            return adb_query_command(format_host_command(argv[0]));
        } else if (argc == 2) {
            if (!strcmp(argv[1], "device")) {
                std::string err;
                adb_connect("reconnect", &err);
                return 0;
            } else if (!strcmp(argv[1], "offline")) {
                std::string err;
                return adb_query_command("host:reconnect-offline");
            } else {
                error_exit("usage: adb reconnect [device|offline]");
            }
        }
    } else if (!strcmp(argv[0], "inc-server")) {
        if (argc < 4) {
#ifdef _WIN32
            error_exit("usage: adb inc-server CONNECTION_HANDLE OUTPUT_HANDLE FILE1 FILE2 ...");
#else
            error_exit("usage: adb inc-server CONNECTION_FD OUTPUT_FD FILE1 FILE2 ...");
#endif
        }
        int connection_fd = atoi(argv[1]);
        if (!_is_valid_os_fd(connection_fd)) {
            error_exit("Invalid connection_fd number given: %d", connection_fd);
        }

        connection_fd = adb_register_socket(connection_fd);
        close_on_exec(connection_fd);

        int output_fd = atoi(argv[2]);
        if (!_is_valid_os_fd(output_fd)) {
            error_exit("Invalid output_fd number given: %d", output_fd);
        }
        output_fd = adb_register_socket(output_fd);
        close_on_exec(output_fd);
        return incremental::serve(connection_fd, output_fd, argc - 3, argv + 3);
    } else if (!strcmp(argv[0], "attach") || !strcmp(argv[0], "detach")) {
        const char* service = strcmp(argv[0], "attach") == 0 ? "host:attach" : "host:detach";
        std::string result;
        std::string error;
        if (!adb_query(service, &result, &error, true)) {
            error_exit("failed to %s: %s", argv[0], error.c_str());
        }
        printf("%s\n", result.c_str());
        return 0;
    } else if (!strcmp(argv[0], "server-status")) {
        AdbServerStateStreamsCallback callback;
        return adb_connect_command("host:server-status", nullptr, &callback);
    }

    error_exit("unknown command %s", argv[0]);
    __builtin_unreachable();
}
