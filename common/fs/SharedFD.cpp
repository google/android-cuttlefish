/*
 * Copyright (C) 2016 The Android Open Source Project
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
#include <SharedFD.h>
#include <SharedSelect.h>
#include <AutoResources.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#define LOG_TAG "SharedFD"

#include <cutils/log.h>

// #define ENABLE_GCE_SHARED_FD_LOGGING 1

namespace {
using avd::SharedFDSet;

void MarkAll(const SharedFDSet& input, fd_set* dest, int* max_index) {
  for (SharedFDSet::const_iterator it = input.begin(); it != input.end();
       ++it) {
    (*it)->Set(dest, max_index);
  }
}

void CheckMarked(fd_set* in_out_mask, fd_set* error_mask,
    SharedFDSet* in_out_set, SharedFDSet* error_set) {
  if (!in_out_set) {
    return;
  }
  SharedFDSet save;
  save.swap(in_out_set);
  for (SharedFDSet::iterator it = save.begin(); it != save.end(); ++it) {
    if (error_set && (*it)->IsSet(error_mask)) {
      error_set->Set(*it);
    }
    if ((*it)->IsSet(in_out_mask)) {
      in_out_set->Set(*it);
    }
  }
}
}  // namespace

namespace avd {

bool FileInstance::CopyFrom(FileInstance& in) {
  AutoFreeBuffer buffer;
  buffer.Resize(8192);
  while (true) {
    ssize_t num_read = in.Read(buffer.data(), buffer.size());
    if (!num_read) {
      return true;
    }
    if (num_read == -1) {
      return false;
    }
    if (num_read > 0) {
      if (Write(buffer.data(), num_read) != num_read) {
        // The caller will have to log an appropriate message.
        return false;
      }
    }
  }
  return true;
}

void FileInstance::Close() {
  AutoFreeBuffer message;
  if (fd_ == -1) {
    errno_ = EBADF;
  } else if (close(fd_) == -1) {
    errno_ = errno;
    if (identity_.size()) {
      message.PrintF("%s: %s failed (%s)", __FUNCTION__, identity_.data(),
                     StrError());
      Log(message.data());
    }
  } else {
    if (identity_.size()) {
      message.PrintF("%s: %s succeeded", __FUNCTION__, identity_.data());
      Log(message.data());
    }
  }
  fd_ = -1;
}

void FileInstance::Identify(const char* identity) {
  identity_.PrintF("fd=%d @%p is %s", fd_, this, identity);
  AutoFreeBuffer message;
  message.PrintF("%s: %s", __FUNCTION__, identity_.data());
  Log(message.data());
}

bool FileInstance::IsSet(fd_set* in) const {
  if (IsOpen() && FD_ISSET(fd_, in)) {
    return true;
  }
  return false;
}

#if ENABLE_GCE_SHARED_FD_LOGGING
void FileInstance::Log(const char* message) {
  static int log_fd = open("/dev/null", O_WRONLY|O_APPEND|O_CREAT, 0666);
  write(log_fd, message, strlen(message));
}
#else
void FileInstance::Log(const char*) { }
#endif

void FileInstance::Set(fd_set* dest, int* max_index) const {
  if (!IsOpen()) {
    return;
  }
  if (fd_ >= *max_index) {
    *max_index = fd_ + 1;
  }
  FD_SET(fd_, dest);
}


int Select(SharedFDSet* read_set, SharedFDSet* write_set,
           SharedFDSet* error_set, struct timeval* timeout) {
  int max_index = 0;
  fd_set readfds;
  FD_ZERO(&readfds);
  if (read_set) {
    MarkAll(*read_set, &readfds, &max_index);
  }
  fd_set writefds;
  FD_ZERO(&writefds);
  if (write_set) {
    MarkAll(*write_set, &writefds, &max_index);
  }
  fd_set errorfds;
  FD_ZERO(&errorfds);
  int rval = TEMP_FAILURE_RETRY(select(
      max_index, &readfds, &writefds, &errorfds, timeout));
  FileInstance::Log("select\n");
  if (error_set) {
    error_set->Zero();
  }
  CheckMarked(&readfds, &errorfds, read_set, error_set);
  CheckMarked(&writefds, &errorfds, write_set, error_set);
  return rval;
}

static void MakeAddress(const char* name, struct sockaddr_un* dest) {
  memset(dest, 0, sizeof(*dest));
  dest->sun_family = AF_UNIX;
  snprintf(dest->sun_path, sizeof(dest->sun_path) - 1, "%s", name);
}

SharedFD SharedFD::SocketSeqPacketServer(const char* name, mode_t mode) {
  struct sockaddr_un addr;
  MakeAddress(name, &addr);
  (void)unlink(addr.sun_path);
  SharedFD rval = SharedFD::Socket(PF_UNIX, SOCK_SEQPACKET, 0);
  if (!rval->IsOpen()) {
    return rval;
  }
  int n = 1;
  if (rval->SetSockOpt(SOL_SOCKET, SO_REUSEADDR, &n, sizeof(n)) == -1) {
    ALOGE("%s: SetSockOpt failed (%s)", __FUNCTION__,
          rval->StrError());
    return SharedFD(shared_ptr<FileInstance>(
        new FileInstance(-1, rval->GetErrno())));
  }
  if (rval->Bind((struct sockaddr *) &addr, sizeof(addr)) == -1) {
    ALOGE("%s: Bind failed name=%s (%s)", __FUNCTION__, name, rval->StrError());
    return SharedFD(shared_ptr<FileInstance>(
        new FileInstance(-1, rval->GetErrno())));
  }
  // Follows the default from socket_local_server
  if (rval->Listen(1) == -1) {
    ALOGE("%s: Listen failed (%s)", __FUNCTION__, rval->StrError());
    return SharedFD(shared_ptr<FileInstance>(
        new FileInstance(-1, rval->GetErrno())));
  }
  if (TEMP_FAILURE_RETRY(chmod(name, mode)) == -1) {
    ALOGE("%s: chmod failed (%s)", __FUNCTION__,  strerror(errno));
    // However, continue since we do have a listening socket
  }
  return rval;
}

SharedFD SharedFD::SocketSeqPacketClient(const char* name) {
  struct sockaddr_un addr;
  MakeAddress(name, &addr);
  SharedFD rval = SharedFD::Socket(PF_UNIX, SOCK_SEQPACKET, 0);
  if (!rval->IsOpen()) {
    return rval;
  }
  if (rval->Connect((struct sockaddr *) &addr, sizeof(addr)) == -1) {
    ALOGE("%s: Connect failed name=%s (%s)",
          __FUNCTION__, name, rval->StrError());
    return SharedFD(shared_ptr<FileInstance>(
        new FileInstance(-1, rval->GetErrno())));
  }
  return rval;
}

}  // namespace avd
