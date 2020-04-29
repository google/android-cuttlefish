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

#define LOG_TAG "vtpm_manager"

#include <linux/types.h>
#include <linux/vtpm_proxy.h>
#include <sys/sysmacros.h>
#include <tss2/tss2_rc.h>

#include <future>

#include <gflags/gflags.h>
#include <android-base/endian.h>
#include <android-base/logging.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "guest/commands/vtpm_manager/commands.h"

DEFINE_uint32(tpm_vsock_port, 0, "vsock port to connect to for the TPM");

struct __attribute__((__packed__)) tpm_message_header {
  __be16 tag;
  __be32 length;
  __be32 ordinal;
};

unsigned char locality = 0;

bool ReadResponseLoop(cvd::SharedFD in_fd, cvd::SharedFD out_fd) {
  std::vector<char> message;
  while (true) {
    std::uint32_t response_size;
    CHECK(cvd::ReadExactBinary(in_fd, &response_size) == 4)
        << "Could not read response size";
    // the tpm simulator writes 4 extra bytes at the end of the message.
    response_size = be32toh(response_size);
    message.resize(response_size, '\0');
    CHECK(cvd::ReadExact(in_fd, &message) == response_size)
        << "Could not read response message";
    auto header = reinterpret_cast<tpm_message_header*>(message.data());
    auto host_rc = betoh32(header->ordinal);
    LOG(DEBUG) << "TPM response was: \"" << Tss2_RC_Decode(host_rc) << "\" ("
               << host_rc << ")";
    std::vector<char> response_bytes(4, 0);
    CHECK(cvd::ReadExact(in_fd, &response_bytes) == 4)
        << "Could not read parity response";
    CHECK(cvd::WriteAll(out_fd, message) == message.size())
        << "Could not forward message to vTPM";
  }
}

void SendCommand(cvd::SharedFD out_fd, std::vector<char> command) {
  // TODO(schuffelen): Implement this logic on the host.
  // TPM2 simulator command protocol.
  std::uint32_t command_num = htobe32(8); // TPM_SEND_COMMAND
  CHECK(cvd::WriteAllBinary(out_fd, &command_num) == 4)
      << "Could not send TPM_SEND_COMMAND";
  CHECK(cvd::WriteAllBinary(out_fd, (char*)&locality) == 1)
      << "Could not send locality";
  std::uint32_t length = htobe32(command.size());
  CHECK(cvd::WriteAllBinary(out_fd, &length) == 4)
      << "Could not send command length";
  CHECK(cvd::WriteAll(out_fd, command) == command.size())
      << "Could not write TPM message";
}

bool SendCommandLoop(cvd::SharedFD in_fd, cvd::SharedFD out_fd) {
  std::vector<char> message(8192, '\0');
  while (true) {
    std::int32_t data_length = 0;
    // Read the whole message in one chunk. The kernel returns EIO if the buffer
    // is not large enough.
    // https://github.com/torvalds/linux/blob/407e9ef72476e64937ebec44cc835e03a25fb408/drivers/char/tpm/tpm_vtpm_proxy.c#L98
    while ((data_length = in_fd->Read(message.data(), message.size())) < 0) {
      CHECK(in_fd->GetErrno() == EIO) << "Error in reading TPM command from "
                                      << "kernel: " << in_fd->StrError();
      message.resize((message.size() + 1) * 2, '\0');
    }
    message.resize(data_length, 0);
    auto header = reinterpret_cast<tpm_message_header*>(message.data());
    LOG(DEBUG) << "Received TPM command "
               << TpmCommandName(betoh32(header->ordinal));
    if (header->ordinal == htobe32(TPM2_CC_SET_LOCALITY)) { // "Driver command"
      locality = *reinterpret_cast<unsigned char*>(header + 1);
      header->ordinal = htobe32(locality);
      header->length = htobe32(sizeof(tpm_message_header));
      message.resize(sizeof(tpm_message_header), '\0');
      CHECK(cvd::WriteAll(in_fd, message) == message.size())
          << "Could not write TPM message";
    } else {
      SendCommand(out_fd, message);
    }
  }
  return false;
}

int main(int argc, char** argv) {
  setenv("ANDROID_LOG_TAGS", "*:v", 1);
  ::android::base::InitLogging(argv);
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  CHECK(FLAGS_tpm_vsock_port != 0) <<  "Need a value for -tpm_vsock_port";

  auto proxy = cvd::SharedFD::VsockClient(2, FLAGS_tpm_vsock_port, SOCK_STREAM);
  CHECK(proxy->IsOpen()) << proxy->StrError();

  auto vtpmx = cvd::SharedFD::Open("/dev/vtpmx", O_RDWR | O_CLOEXEC);
  CHECK(vtpmx->IsOpen()) << vtpmx->StrError();

  vtpm_proxy_new_dev vtpm_creation;
  vtpm_creation.flags = VTPM_PROXY_FLAG_TPM2;

  CHECK(vtpmx->Ioctl(VTPM_PROXY_IOC_NEW_DEV, &vtpm_creation) == 0) << vtpmx->StrError();

  auto device_fd = cvd::SharedFD::Dup(vtpm_creation.fd);
  CHECK(device_fd->IsOpen()) << device_fd->StrError();
  close(vtpm_creation.fd);

  LOG(VERBOSE) << "major was " << vtpm_creation.major << " minor was " << vtpm_creation.minor;

  auto proxy_to_device = std::async(std::launch::async, ReadResponseLoop, proxy, device_fd);
  auto device_to_proxy = std::async(std::launch::async, SendCommandLoop, device_fd, proxy);

  CHECK(proxy_to_device.get())
      << "(" << device_fd->StrError() << ")"
      << "(" << proxy->StrError() << ")";
  CHECK(device_to_proxy.get())
      << "(" << device_fd->StrError() << ")"
      << "(" << proxy->StrError() << ")";
}

