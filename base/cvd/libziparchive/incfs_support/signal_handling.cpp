/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include "incfs_support/signal_handling.h"

#if defined(__BIONIC__) && !INCFS_SUPPORT_DISABLED

#include <errno.h>

namespace incfs {

static void enableSignal(int code) {
  sigset_t allowed;
  sigemptyset(&allowed);
  sigaddset(&allowed, code);
  pthread_sigmask(SIG_UNBLOCK, &allowed, nullptr);
}

ScopedJmpBuf::~ScopedJmpBuf() { SignalHandler::mJmpBuf = mPrev; }

SignalHandler::SignalHandler() {
  const struct sigaction action = {
      .sa_sigaction = &handler,
      .sa_flags = SA_SIGINFO,
  };
  if (sigaction(SIGBUS, &action, &mOldSigaction)) {
    LOG_ALWAYS_FATAL("sigaction(SIGBUS) failed: %d", errno);
    // not much can be done now
    return;
  }

  // ensure SIGBUS is unblocked, so the process won't get insta-killed
  enableSignal(SIGBUS);
}

void SignalHandler::handler(int sig, siginfo_t* info, void* ucontext) {
  if (sig != SIGBUS) {
    LOG_FATAL("SIGBUS handler called for unexpected signal %d", sig);
    return;
  }

  if (!mJmpBuf.armed) {
    // No error handler installed - run the previous one
    if (mOldSigaction.sa_handler == SIG_DFL) {
      // reset the action to default and re-raise the signal. It will kill the
      // process
      signal(sig, SIG_DFL);
      raise(sig);
      return;
    }
    if (mOldSigaction.sa_handler == SIG_IGN) {
      // ignoring SIGBUS won't help us much, as we'll get back right here after
      // retrying.
      return;
    }
    if (mOldSigaction.sa_flags & SA_SIGINFO) {
      mOldSigaction.sa_sigaction(sig, info, ucontext);
    } else {
      mOldSigaction.sa_handler(sig);
    }
    // Returning from a signal handler for SIGBUS is undefined, but if it
    // happens better to at least forward that undefined thing further. Who are
    // we to argue with the user?
    return;
  }

  // restore SIGBUS as signal handling blocks it before running the callback
  enableSignal(SIGBUS);

  longjmp(mJmpBuf.buf, 1);
}

}  // namespace incfs

#endif
