//
// Copyright (C) 2026 The Android Open Source Project
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

#include "cuttlefish/process/subprocess.h"

#include <pthread.h>  // IWYU pragma: keep
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>

#include <chrono>
#include <thread>
#include <utility>

#include "absl/cleanup/cleanup.h"
#include "gtest/gtest.h"

#include "cuttlefish/common/libs/fs/fd.h"
#include "cuttlefish/result/assert.h"

namespace cuttlefish {
namespace {

volatile sig_atomic_t g_usr1_count = 0;

void RecordUsr1(int) { g_usr1_count = g_usr1_count + 1; }

TEST(SubprocessWaitTest, RetriesWaitpidOnEintr) {
  g_usr1_count = 0;

  // Leave sa_flags zero (no SA_RESTART) so SIGUSR1 interrupts waitpid(2).
  struct sigaction new_action = {};
  new_action.sa_handler = RecordUsr1;
  sigemptyset(&new_action.sa_mask);

  struct sigaction old_action = {};
  ASSERT_EQ(sigaction(SIGUSR1, &new_action, &old_action), 0);
  absl::Cleanup restore_action = [&old_action]() {
    sigaction(SIGUSR1, &old_action, nullptr);
  };

  auto [ready_read, ready_write] = CF_ASSERT(Fd::Pipe());
  auto [release_read, release_write] = CF_ASSERT(Fd::Pipe());

  const pid_t child_pid = fork();
  ASSERT_GE(child_pid, 0);
  if (child_pid == 0) {
    ready_read.Close();
    release_write.Close();
    const char ready = '1';
    if (ready_write.Write(&ready, 1).value_or(0) != 1) {
      _exit(2);
    }
    char done = 0;
    (void)release_read.Read(&done, 1);
    _exit(42);
  }

  ready_write.Close();
  release_read.Close();

  Subprocess subprocess(child_pid);
  // NOLINTNEXTLINE(misc-include-cleaner): <pthread.h>
  const pthread_t waiter_tid = pthread_self();

  std::thread signaler([waiter_tid, ready_read = std::move(ready_read),
                        release_write = std::move(release_write)]() mutable {
    char ready = 0;
    (void)ready_read.Read(&ready, 1);
    for (int i = 0; i < 5; ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
      pthread_kill(waiter_tid, SIGUSR1);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    release_write.Close();
  });

  EXPECT_EQ(subprocess.Wait(), 42);
  signaler.join();
  EXPECT_EQ(subprocess.pid(), -1);
  EXPECT_GE(g_usr1_count, 1);
}

}  // namespace
}  // namespace cuttlefish
