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
#pragma once

//
// This file defines low-level primitives for handling SIGBUS signals that get
// generated when accessing memory mapped files on IncFS and hitting a missing
// page. These primitives provide a way for a process to not crash, but require
// careful attention to use properly.
//
// A slightly safer alternative is in "access.h", you probably want it unless
// the performance is so critical that a single function call overhead is
// unacceptable.
//
// Macros:
//   SCOPED_SIGBUS_HANDLER(code)
//   SCOPED_SIGBUS_HANDLER_CONDITIONAL(condition, code)
//
// Both macros set up a thread-local handler for SIGBUS, and make the (code)
// passed as a parameter run when it happens; _CONDITIONAL version only sets the
// handler up if the (condition) is true, making it easier to write generic
// code. (code) runs in a context of a current function. Macros work via a
// setjmp/longjmp machinery, and those result in the following pitfalls:
//
//   - Last action of the (code) MUST BE a return from the function. Current
//   state is too badly corrupted to continue execution.
//      -- Macro checks for code that failed to return and kills the process.
//   - C++ destructors of the objects created since the macro DO NOT RUN. (code)
//   has to manually clean up all of those.
//      -- this means ALL allocated memory, locked mutexes, created temp files,
//      etc.
//      -- what this really means is it's better to not do anything that needs
//      cleanup in the code protected with the macro
//   - Signal handler jumps out of any level of a nested function call straight
//   into (code); stack unwinding doesn't happen in a regular way - only the SP
//   gets reset. Nested functions have even stronger restrictions: no destructor
//   will run in them.
//      -- under no circumstance one may call a user-supplied callback in a code
//      protected with these macros. It's a recipe for a huge disaster.
//   - If some other code overrides the signal handler, protections cease to
//   work. Unfortunately, this is just the way signal handling works in Linux.
//
// Usage:
//
// In a function that will access mapped memory, as close to the access point as
// possible:
//
// int foo(char* mapped, long size) {
//      ...
//      SCOPED_SIGBUS_HANDLER(return -1);
//      for (...) {
//          <access |mapped|>
//      }
//      return 0;
// }
//
// If you need to perform some non-trivial cleanup:
//
//      ...
//      int fd = -1;
//      SCOPED_SIGBUS_HANDLER({
//          if (fd >= 0) close(fd);
//          return -1;
//      });
//      ...
//      <access |mapped|>
//      fd = open(...);
//      <access |mapped|>
//      ...
//
// Cleanup:
//  Pay attention when releasing the allocated resources - some functions may appear
//  to do that while in reality they aren't; use the function(s) from incfs_support/util.h to
//  do it safely, e.g.:
//
//      std::vector<int> numbers;
//      SCOPED_SIGBUS_HANDLER({
//          incfs::util::clearAndFree(numbers);
//          // putting 'numbers.clear()' here instead would leak it as vector doesn't free its
//          // capacity on that call, and SIGBUS may prevent its destructor from running.
//          return -1;
//      });

// Performance: each macro sets up a couple of thread-local variables and calls
// a single syscall,
//  setjmp(). The only serious consideration is to  not call it on each
//  iteration of a tight loop, as syscalls aren't particularly instant. See
//  "incfs-support-benchmark" project for more details.
//

#include <sys/types.h>

#if !defined(__BIONIC__) || INCFS_SUPPORT_DISABLED

// IncFS signal handling isn't needed anywhere but on Android as of now;
// or if we're told it's not desired.
#define SCOPED_SIGBUS_HANDLER(code)
#define SCOPED_SIGBUS_HANDLER_CONDITIONAL(condition, code)

#else

#ifndef LOG_TAG
#define LOG_TAG "incfs:hardening"
#endif

#include <log/log.h>
#include <setjmp.h>
#include <signal.h>
#include <string.h>

namespace incfs {

struct JmpBufState final {
  jmp_buf buf;
  bool armed = false;

  JmpBufState() = default;
  JmpBufState(const JmpBufState& other) {
    if (other.armed) {
      memcpy(&buf, &other.buf, sizeof(buf));
      armed = true;
    }
  }

  JmpBufState& operator=(const JmpBufState& other) {
    if (&other != this) {
      if (other.armed) {
        memcpy(&buf, &other.buf, sizeof(buf));
        armed = true;
      } else {
        armed = false;
      }
    }
    return *this;
  }
};

class ScopedJmpBuf final {
 public:
  ScopedJmpBuf(const JmpBufState& prev) : mPrev(prev) {}
  ~ScopedJmpBuf();

  ScopedJmpBuf(const ScopedJmpBuf&) = delete;

 private:
  const JmpBufState& mPrev;
};

#define SCOPED_SIGBUS_HANDLER_CONDITIONAL(condition, code)                     \
  (void)incfs::SignalHandler::instance();                                      \
  auto& tlsBuf_macro = incfs::SignalHandler::mJmpBuf;                          \
  incfs::JmpBufState oldBuf_macro = tlsBuf_macro;                              \
  const bool condition_macro_val = (condition);                                \
  if (condition_macro_val && setjmp(tlsBuf_macro.buf) != 0) {                  \
    ALOGI("%s: handling SIGBUS at line %d", __func__, __LINE__);               \
    tlsBuf_macro = oldBuf_macro;                                               \
    do {                                                                       \
      code;                                                                    \
    } while (0);                                                               \
    LOG_ALWAYS_FATAL("%s(): signal handler was supposed to return", __func__); \
  }                                                                            \
  tlsBuf_macro.armed |= (condition_macro_val);                                 \
  incfs::ScopedJmpBuf oldBufRestore_macro(oldBuf_macro)

#define SCOPED_SIGBUS_HANDLER(code) \
  SCOPED_SIGBUS_HANDLER_CONDITIONAL(true, code)

class SignalHandler final {
 public:
  static SignalHandler& instance() {
    static SignalHandler self;
    return self;
  }

 private:
  SignalHandler();
  inline static struct sigaction mOldSigaction = {};

  static void handler(int sig, siginfo_t* info, void* ucontext);

 public:
  inline static thread_local JmpBufState mJmpBuf = {};
};

}  // namespace incfs

#endif
