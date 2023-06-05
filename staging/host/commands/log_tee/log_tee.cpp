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

#include <signal.h>
#include <sys/signalfd.h>

#include <regex>

#include <android-base/logging.h>
#include <android-base/strings.h>
#include <gflags/gflags.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/tee_logging.h"
#include "host/libs/config/cuttlefish_config.h"

DEFINE_string(process_name, "", "The process to credit log messages to");
DEFINE_int32(log_fd_in, -1, "The file descriptor to read logs from.");

// Crosvm formats logs starting with a local ISO 8601 timestamp and then a
// log level (based on external/crosvm/base/src/syslog.rs).
const std::regex kCrosvmLogPattern(
    "^\\["
    "\\d{4}" /* year */
    "-"
    "\\d{2}" /* month */
    "-"
    "\\d{2}" /* day */
    "T"
    "\\d{2}" /* hour */
    ":"
    "\\d{2}" /* minute*/
    ":"
    "\\d{2}" /* second */
    "\\."
    "\\d{9}"                          /* millisecond */
    "(Z|[+-]\\d{2}(:\\d{2}|\\d{2})?)" /* timezone */
    "\\s"
    "(ERROR|WARN|INFO|DEBUG|TRACE)");

int main(int argc, char** argv) {
  ::android::base::InitLogging(argv, android::base::StderrLogger);
  google::ParseCommandLineFlags(&argc, &argv, /* remove_flags */ true);

  CHECK(FLAGS_log_fd_in >= 0) << "-log_fd_in is required";

  auto config = cuttlefish::CuttlefishConfig::Get();

  CHECK(config) << "Could not open cuttlefish config";

  auto instance = config->ForDefaultInstance();

  if (instance.run_as_daemon()) {
    android::base::SetLogger(
        cuttlefish::LogToFiles({instance.launcher_log_path()}));
  } else {
    android::base::SetLogger(
        cuttlefish::LogToStderrAndFiles({instance.launcher_log_path()}));
  }

  auto log_fd = cuttlefish::SharedFD::Dup(FLAGS_log_fd_in);
  CHECK(log_fd->IsOpen()) << "Failed to dup log_fd_in: " <<  log_fd->StrError();
  close(FLAGS_log_fd_in);

  if (FLAGS_process_name.size() > 0) {
    android::base::SetDefaultTag(FLAGS_process_name);
  }

  // mask SIGINT and handle it using signalfd
  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGINT);
  CHECK(sigprocmask(SIG_BLOCK, &mask, NULL) == 0)
      << "sigprocmask failed: " << strerror(errno);
  int sfd = signalfd(-1, &mask, 0);
  CHECK(sfd >= 0) << "signalfd failed: " << strerror(errno);
  auto int_fd = cuttlefish::SharedFD::Dup(sfd);
  close(sfd);

  auto poll_fds = std::vector<cuttlefish::PollSharedFd>{
      cuttlefish::PollSharedFd{
          .fd = log_fd,
          .events = POLL_IN,
          .revents = 0,
      },
      cuttlefish::PollSharedFd{
          .fd = int_fd,
          .events = POLL_IN,
          .revents = 0,
      },
  };

  LOG(DEBUG) << "Starting to read from process " << FLAGS_process_name;

  char buf[1 << 16];
  ssize_t chars_read = 0;
  for (;;) {
    // We can assume all writers to `log_fd` have completed before a SIGINT is
    // sent, but we need to make sure we've actually read all the data before
    // exiting. So, keep reading from `log_fd` until both (1) we get SIGINT and
    // (2) `log_fd` is empty (but not necessarily EOF).
    //
    // This could be simpler if all the writers would close their FDs when they
    // are finished. Then, we could just read until EOF. However that would
    // require more work elsewhere in cuttlefish.
    CHECK(cuttlefish::SharedFD::Poll(poll_fds, /*timeout=*/-1) >= 0)
        << "poll failed: " << strerror(errno);
    if (poll_fds[0].revents) {
      chars_read = log_fd->Read(buf, sizeof(buf));
      if (chars_read < 0) {
        LOG(DEBUG) << "Failed to read from process " << FLAGS_process_name
                   << ": " << log_fd->StrError();
        break;
      }
      if (chars_read == 0) {
        break;
      }
      auto trimmed = android::base::Trim(std::string_view(buf, chars_read));
      // Newlines inside `trimmed` are handled by the android logging code.
      // These checks attempt to determine the log severity coming from crosvm.
      // There is no guarantee of success all the time since log line boundaries
      // could be out sync with the reads, but that's ok.
      if (android::base::StartsWith(trimmed, "[INFO")) {
        LOG(DEBUG) << trimmed;
      } else if (android::base::StartsWith(trimmed, "[ERROR")) {
        LOG(ERROR) << trimmed;
      } else if (android::base::StartsWith(trimmed, "[WARNING")) {
        LOG(WARNING) << trimmed;
      } else if (android::base::StartsWith(trimmed, "[VERBOSE")) {
        LOG(VERBOSE) << trimmed;
      } else {
        std::smatch match_result;
        if (std::regex_search(trimmed, match_result, kCrosvmLogPattern)) {
          if (match_result.size() == 4) {
            const auto& level = match_result[3];
            if (level == "ERROR") {
              LOG(ERROR) << trimmed;
            } else if (level == "WARN") {
              LOG(WARNING) << trimmed;
            } else if (level == "INFO") {
              LOG(INFO) << trimmed;
            } else if (level == "DEBUG") {
              LOG(DEBUG) << trimmed;
            } else if (level == "TRACE") {
              LOG(VERBOSE) << trimmed;
            } else {
              LOG(DEBUG) << trimmed;
            }
          }
        } else {
          LOG(DEBUG) << trimmed;
        }
      }

      // Go back to polling immediately to see if there is more data, don't
      // handle any signals yet.
      continue;
    }
    if (poll_fds[1].revents) {
      struct signalfd_siginfo siginfo;
      int s = int_fd->Read(&siginfo, sizeof(siginfo));
      CHECK(s == sizeof(siginfo)) << "bad read size on signalfd, expected "
                                  << sizeof(siginfo) << " got " << s;
      CHECK(siginfo.ssi_signo == SIGINT)
          << "unexpected signal: " << siginfo.ssi_signo;
      break;
    }
  }

  LOG(DEBUG) << "Finished reading from process " << FLAGS_process_name;
}
