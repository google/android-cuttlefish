//
// Copyright (C) 2020 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <functional>
#include <mutex>
#include <optional>
#include <thread>

#include <android-base/logging.h>
#include <android-base/strings.h>
#include <gflags/gflags.h>

#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/common/libs/transport/channel_sharedfd.h"
#include "cuttlefish/host/commands/kernel_log_monitor/kernel_log_server.h"
#include "cuttlefish/host/commands/kernel_log_monitor/utils.h"
#include "cuttlefish/host/commands/secure_env/oemlock/oemlock.h"
#include "cuttlefish/host/commands/secure_env/oemlock/oemlock_responder.h"
#include "cuttlefish/host/commands/secure_env/storage/insecure_json_storage.h"
#include "cuttlefish/host/commands/secure_env/suspend_resume_handler.h"
#include "cuttlefish/host/commands/secure_env/worker_thread_loop_body.h"
#include "cuttlefish/host/libs/config/known_paths.h"
#include "cuttlefish/host/libs/config/logging.h"

DEFINE_int32(confui_server_fd, -1, "A named socket to serve confirmation UI");
DEFINE_int32(snapshot_control_fd, -1,
             "A socket connected to run_cvd for snapshot operations and"
             "responses");
DEFINE_int32(keymaster_fd_in, -1, "A pipe for keymaster communication");
DEFINE_int32(keymaster_fd_out, -1, "A pipe for keymaster communication");
DEFINE_int32(keymint_fd_in, -1, "A pipe for keymint communication");
DEFINE_int32(keymint_fd_out, -1, "A pipe for keymint communication");
DEFINE_int32(gatekeeper_fd_in, -1, "A pipe for gatekeeper communication");
DEFINE_int32(gatekeeper_fd_out, -1, "A pipe for gatekeeper communication");
DEFINE_int32(oemlock_fd_in, -1, "A pipe for oemlock communication");
DEFINE_int32(oemlock_fd_out, -1, "A pipe for oemlock communication");
DEFINE_int32(kernel_events_fd, -1,
             "A pipe for monitoring events based on "
             "messages written to the kernel log. This "
             "is used by secure_env to monitor for "
             "device reboots.");

DEFINE_string(tpm_impl, "in_memory",
              "The TPM implementation. \"in_memory\" or \"host_device\"");

DEFINE_string(keymint_impl, "tpm",
              "The KeyMint implementation. \"tpm\" or \"software\"");

DEFINE_string(gatekeeper_impl, "tpm",
              "The gatekeeper implementation. \"tpm\" or \"software\"");

DEFINE_string(oemlock_impl, "tpm",
              "The oemlock implementation. \"tpm\" or \"software\"");

DEFINE_int32(jcardsim_fd_in, -1, "A pipe for jcardsim communication");
DEFINE_int32(jcardsim_fd_out, -1, "A pipe for jcardsim communication");
DEFINE_bool(enable_jcard_simulator, false, "Whether to enable jcardsimulator.");

namespace cuttlefish {
namespace {

constexpr std::chrono::seconds kRestartLockTimeout(2);

// Dup a command line file descriptor into a SharedFD.
SharedFD DupFdFlag(gflags::int32 fd) {
  CHECK(fd != -1);
  SharedFD duped = SharedFD::Dup(fd);
  CHECK(duped->IsOpen()) << "Could not dup output fd: " << duped->StrError();
  // The original FD is intentionally kept open so that we can re-exec this
  // process without having to do a bunch of argv book-keeping.
  return duped;
}

// Re-launch this process with all the same flags it was originallys started
// with.
[[noreturn]] void ReExecSelf() {
  // Allocate +1 entry for terminating nullptr.
  std::vector<char*> argv(gflags::GetArgvs().size() + 1, nullptr);
  for (size_t i = 0; i < gflags::GetArgvs().size(); ++i) {
    argv[i] = strdup(gflags::GetArgvs()[i].c_str());
    CHECK(argv[i] != nullptr) << "OOM";
  }
  execv(SecureEnvBinary().c_str(), argv.data());
  char buf[128];
  LOG(FATAL) << "Exec failed, secure_env is out of sync with the guest: "
             << errno << "(" << strerror_r(errno, buf, sizeof(buf)) << ")";
  abort();  // LOG(FATAL) isn't marked as noreturn
}

// Spin up a thread that monitors for a kernel loaded event, then re-execs
// this process. This way, secure_env's boot tracking matches up with the guest.
std::thread StartKernelEventMonitor(SharedFD kernel_events_fd,
                                    std::timed_mutex& oemlock_lock) {
  return std::thread([kernel_events_fd, &oemlock_lock]() {
    while (kernel_events_fd->IsOpen()) {
      auto read_result = monitor::ReadEvent(kernel_events_fd);
      CHECK(read_result.ok()) << read_result.error();
      CHECK(read_result->has_value()) << "EOF in kernel log monitor";
      if ((*read_result)->event == monitor::Event::BootloaderLoaded) {
        LOG(DEBUG) << "secure_env detected guest reboot, restarting.";

        // secure_env app potentially may become stuck at IO during holding the
        // lock, so limit the waiting time to make sure self-restart is executed
        // as expected
        const bool locked = oemlock_lock.try_lock_for(kRestartLockTimeout);
        if (!locked) {
          LOG(WARNING) << "Couldn't acquire oemlock lock within timeout. "
                          "Executing self-restart anyway";
        }

        ReExecSelf();

        if (locked) {
          oemlock_lock.unlock();
        }
      }
    }
  });
}

Result<void> SecureEnvMain(int argc, char** argv) {
  DefaultSubprocessLogging(argv);
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  secure_env::InsecureJsonStorage storage("oemlock_insecure");
  oemlock::OemLock oemlock(storage);

  std::timed_mutex oemlock_lock;

  // go/cf-secure-env-snapshot
  auto [rust_snapshot_socket1, rust_snapshot_socket2] =
      CF_EXPECT(SharedFD::SocketPair(AF_UNIX, SOCK_STREAM, 0));
  auto [keymaster_snapshot_socket1, keymaster_snapshot_socket2] =
      CF_EXPECT(SharedFD::SocketPair(AF_UNIX, SOCK_STREAM, 0));
  auto [gatekeeper_snapshot_socket1, gatekeeper_snapshot_socket2] =
      CF_EXPECT(SharedFD::SocketPair(AF_UNIX, SOCK_STREAM, 0));
  auto [oemlock_snapshot_socket1, oemlock_snapshot_socket2] =
      CF_EXPECT(SharedFD::SocketPair(AF_UNIX, SOCK_STREAM, 0));
  SharedFD channel_to_run_cvd = DupFdFlag(FLAGS_snapshot_control_fd);

  SnapshotCommandHandler suspend_resume_handler(
      channel_to_run_cvd,
      SnapshotCommandHandler::SnapshotSockets{
          .rust = std::move(rust_snapshot_socket1),
          .keymaster = std::move(keymaster_snapshot_socket1),
          .gatekeeper = std::move(gatekeeper_snapshot_socket1),
          .oemlock = std::move(oemlock_snapshot_socket1),
      });


  std::vector<std::thread> threads;

  auto oemlock_in = DupFdFlag(FLAGS_oemlock_fd_in);
  auto oemlock_out = DupFdFlag(FLAGS_oemlock_fd_out);
  threads.emplace_back(
      [oemlock_in, oemlock_out, &oemlock, &oemlock_lock,
       oemlock_snapshot_socket2 = std::move(oemlock_snapshot_socket2)]() {
        while (true) {
          transport::SharedFdChannel channel(oemlock_in, oemlock_out);
          oemlock::OemLockResponder responder(channel, oemlock, oemlock_lock);

          std::function<bool()> oemlock_process_cb = [&responder]() -> bool {
            return (responder.ProcessMessage().ok());
          };

          // infinite loop that returns if resetting responder is needed
          auto result = secure_env_impl::WorkerInnerLoop(
              oemlock_process_cb, oemlock_in, oemlock_snapshot_socket2);
          if (!result.ok()) {
            LOG(FATAL) << "oemlock worker failed: " << result.error().Trace();
          }
        }
      });

  auto kernel_events_fd = DupFdFlag(FLAGS_kernel_events_fd);
  threads.emplace_back(StartKernelEventMonitor(kernel_events_fd, oemlock_lock));

  for (auto& t : threads) {
    t.join();
  }
  return {};
}

}  // namespace
}  // namespace cuttlefish

int main(int argc, char** argv) {
  auto result = cuttlefish::SecureEnvMain(argc, argv);
  if (result.ok()) {
    return 0;
  }
  LOG(FATAL) << result.error().Trace();
  return -1;
}
