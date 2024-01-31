/*
 * Copyright (C) 2021 The Android Open Source Project
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
#include "common/libs/utils/unix_sockets.h"

#include <fcntl.h>
#include <sys/uio.h>
#include <unistd.h>

#include <cstring>
#include <memory>
#include <ostream>
#include <utility>
#include <vector>

#include <android-base/logging.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/result.h"

// This would use android::base::ReceiveFileDescriptors, but it silently drops
// SCM_CREDENTIALS control messages.

namespace cuttlefish {

ControlMessage ControlMessage::FromRaw(const cmsghdr* cmsg) {
  ControlMessage message;
  message.data_ =
      std::vector<char>((char*)cmsg, ((char*)cmsg) + cmsg->cmsg_len);
  if (message.IsFileDescriptors()) {
    size_t fdcount =
        static_cast<size_t>(cmsg->cmsg_len - CMSG_LEN(0)) / sizeof(int);
    for (int i = 0; i < fdcount; i++) {
      // Use memcpy as CMSG_DATA may be unaligned
      int fd = -1;
      memcpy(&fd, CMSG_DATA(cmsg) + (i * sizeof(int)), sizeof(fd));
      message.fds_.push_back(fd);
    }
  }
  return message;
}

Result<ControlMessage> ControlMessage::FromFileDescriptors(
    const std::vector<SharedFD>& fds) {
  ControlMessage message;
  message.data_.resize(CMSG_SPACE(fds.size() * sizeof(int)), 0);
  message.Raw()->cmsg_len = CMSG_LEN(fds.size() * sizeof(int));
  message.Raw()->cmsg_level = SOL_SOCKET;
  message.Raw()->cmsg_type = SCM_RIGHTS;
  for (int i = 0; i < fds.size(); i++) {
    int fd_copy = fds[i]->Fcntl(F_DUPFD_CLOEXEC, 3);
    CF_EXPECT(fd_copy >= 0, "Failed to duplicate fd: " << fds[i]->StrError());
    message.fds_.push_back(fd_copy);
    // Following the CMSG_DATA spec, use memcpy to avoid alignment issues.
    memcpy(CMSG_DATA(message.Raw()) + (i * sizeof(int)), &fd_copy, sizeof(int));
  }
  return message;
}

#ifdef __linux__
ControlMessage ControlMessage::FromCredentials(const ucred& credentials) {
  ControlMessage message;
  message.data_.resize(CMSG_SPACE(sizeof(ucred)), 0);
  message.Raw()->cmsg_len = CMSG_LEN(sizeof(ucred));
  message.Raw()->cmsg_level = SOL_SOCKET;
  message.Raw()->cmsg_type = SCM_CREDENTIALS;
  // Following the CMSG_DATA spec, use memcpy to avoid alignment issues.
  memcpy(CMSG_DATA(message.Raw()), &credentials, sizeof(credentials));
  return message;
}
#endif

ControlMessage::ControlMessage(ControlMessage&& existing) {
  // Enforce that the old ControlMessage is left empty, so it doesn't try to
  // close any file descriptors. https://stackoverflow.com/a/17735913
  data_ = std::move(existing.data_);
  existing.data_.clear();
  fds_ = std::move(existing.fds_);
  existing.fds_.clear();
}

ControlMessage& ControlMessage::operator=(ControlMessage&& existing) {
  // Enforce that the old ControlMessage is left empty, so it doesn't try to
  // close any file descriptors. https://stackoverflow.com/a/17735913
  data_ = std::move(existing.data_);
  existing.data_.clear();
  fds_ = std::move(existing.fds_);
  existing.fds_.clear();
  return *this;
}

ControlMessage::~ControlMessage() {
  for (const auto& fd : fds_) {
    if (close(fd) != 0) {
      PLOG(ERROR) << "Failed to close fd " << fd
                  << ", may have leaked or closed prematurely";
    }
  }
}

cmsghdr* ControlMessage::Raw() {
  return reinterpret_cast<cmsghdr*>(data_.data());
}

const cmsghdr* ControlMessage::Raw() const {
  return reinterpret_cast<const cmsghdr*>(data_.data());
}

#ifdef __linux__
bool ControlMessage::IsCredentials() const {
  bool right_level = Raw()->cmsg_level == SOL_SOCKET;
  bool right_type = Raw()->cmsg_type == SCM_CREDENTIALS;
  bool enough_data = Raw()->cmsg_len >= sizeof(cmsghdr) + sizeof(ucred);
  return right_level && right_type && enough_data;
}

Result<ucred> ControlMessage::AsCredentials() const {
  CF_EXPECT(IsCredentials(), "Control message does not hold a credential");
  ucred credentials;
  memcpy(&credentials, CMSG_DATA(Raw()), sizeof(ucred));
  return credentials;
}
#endif

bool ControlMessage::IsFileDescriptors() const {
  bool right_level = Raw()->cmsg_level == SOL_SOCKET;
  bool right_type = Raw()->cmsg_type == SCM_RIGHTS;
  return right_level && right_type;
}

Result<std::vector<SharedFD>> ControlMessage::AsSharedFDs() const {
  CF_EXPECT(IsFileDescriptors(), "Message does not contain file descriptors");
  size_t fdcount =
      static_cast<size_t>(Raw()->cmsg_len - CMSG_LEN(0)) / sizeof(int);
  std::vector<SharedFD> shared_fds;
  for (int i = 0; i < fdcount; i++) {
    // Use memcpy as CMSG_DATA may be unaligned
    int fd = -1;
    memcpy(&fd, CMSG_DATA(Raw()) + (i * sizeof(int)), sizeof(fd));
    SharedFD shared_fd = SharedFD::Dup(fd);
    CF_EXPECT(shared_fd->IsOpen(), "Could not dup FD " << fd);
    shared_fds.push_back(shared_fd);
  }
  return shared_fds;
}

bool UnixSocketMessage::HasFileDescriptors() {
  for (const auto& control_message : control) {
    if (control_message.IsFileDescriptors()) {
      return true;
    }
  }
  return false;
}
Result<std::vector<SharedFD>> UnixSocketMessage::FileDescriptors() {
  std::vector<SharedFD> fds;
  for (const auto& control_message : control) {
    if (control_message.IsFileDescriptors()) {
      auto additional_fds = CF_EXPECT(control_message.AsSharedFDs());
      fds.insert(fds.end(), additional_fds.begin(), additional_fds.end());
    }
  }
  return fds;
}
#ifdef __linux__
bool UnixSocketMessage::HasCredentials() {
  for (const auto& control_message : control) {
    if (control_message.IsCredentials()) {
      return true;
    }
  }
  return false;
}
Result<ucred> UnixSocketMessage::Credentials() {
  std::vector<ucred> credentials;
  for (const auto& control_message : control) {
    if (control_message.IsCredentials()) {
      auto creds = CF_EXPECT(control_message.AsCredentials(),
                             "Message claims to have credentials but does not");
      credentials.push_back(creds);
    }
  }
  if (credentials.size() == 0) {
    return CF_ERR("No credentials present");
  } else if (credentials.size() == 1) {
    return credentials[0];
  } else {
    return CF_ERR("Excepted 1 credential, received " << credentials.size());
  }
}
#endif

UnixMessageSocket::UnixMessageSocket(SharedFD socket) : socket_(socket) {
  socklen_t ln = sizeof(max_message_size_);
  CHECK(socket->GetSockOpt(SOL_SOCKET, SO_SNDBUF, &max_message_size_, &ln) == 0)
      << "error: can't retrieve socket max message size: "
      << socket->StrError();
}

#ifdef __linux__
Result<void> UnixMessageSocket::EnableCredentials(bool enable) {
  int flag = enable ? 1 : 0;
  if (socket_->SetSockOpt(SOL_SOCKET, SO_PASSCRED, &flag, sizeof(flag)) != 0) {
    return CF_ERR("Could not set credential status to " << enable << ": "
                                                        << socket_->StrError());
  }
  return {};
}
#endif

Result<void> UnixMessageSocket::WriteMessage(const UnixSocketMessage& message) {
  auto control_size = 0;
  for (const auto& control : message.control) {
    control_size += control.data_.size();
  }
  std::vector<char> message_control(control_size, 0);
  msghdr message_header{};
  message_header.msg_control = message_control.data();
  message_header.msg_controllen = message_control.size();
  auto cmsg = CMSG_FIRSTHDR(&message_header);
  for (const ControlMessage& control : message.control) {
    CF_EXPECT(cmsg != nullptr,
              "Control messages did not fit in control buffer");
    /* size() should match CMSG_SPACE */
    memcpy(cmsg, control.data_.data(), control.data_.size());
    cmsg = CMSG_NXTHDR(&message_header, cmsg);
  }

  iovec message_iovec;
  message_iovec.iov_base = (void*)message.data.data();
  message_iovec.iov_len = message.data.size();
  message_header.msg_name = nullptr;
  message_header.msg_namelen = 0;
  message_header.msg_iov = &message_iovec;
  message_header.msg_iovlen = 1;
  message_header.msg_flags = 0;

  auto bytes_sent = socket_->SendMsg(&message_header, MSG_NOSIGNAL);
  CF_EXPECT(bytes_sent >= 0, "Failed to send message: " << socket_->StrError());
  CF_EXPECT(bytes_sent == message.data.size(),
            "Failed to send entire message. Sent "
                << bytes_sent << ", excepted to send " << message.data.size());
  return {};
}

Result<UnixSocketMessage> UnixMessageSocket::ReadMessage() {
  msghdr message_header{};
  std::vector<char> message_control(max_message_size_, 0);
  message_header.msg_control = message_control.data();
  message_header.msg_controllen = message_control.size();
  std::vector<char> message_data(max_message_size_, 0);
  iovec message_iovec;
  message_iovec.iov_base = message_data.data();
  message_iovec.iov_len = message_data.size();
  message_header.msg_iov = &message_iovec;
  message_header.msg_iovlen = 1;
  message_header.msg_name = nullptr;
  message_header.msg_namelen = 0;
  message_header.msg_flags = 0;

#ifdef __linux__
  auto bytes_read = socket_->RecvMsg(&message_header, MSG_CMSG_CLOEXEC);
#elif defined(__APPLE__)
  auto bytes_read = socket_->RecvMsg(&message_header, 0);
#else
#error "Unsupported operating system"
#endif
  CF_EXPECT(bytes_read >= 0, "Read error: " << socket_->StrError());
  CF_EXPECT(!(message_header.msg_flags & MSG_TRUNC),
            "Message was truncated on read");
  CF_EXPECT(!(message_header.msg_flags & MSG_CTRUNC),
            "Message control data was truncated on read");
#ifdef __linux__
  CF_EXPECT(!(message_header.msg_flags & MSG_ERRQUEUE), "Error queue error");
#endif
  UnixSocketMessage managed_message;
  for (auto cmsg = CMSG_FIRSTHDR(&message_header); cmsg != nullptr;
       cmsg = CMSG_NXTHDR(&message_header, cmsg)) {
    managed_message.control.emplace_back(ControlMessage::FromRaw(cmsg));
  }
  message_data.resize(bytes_read);
  managed_message.data = std::move(message_data);

  return managed_message;
}

}  // namespace cuttlefish
