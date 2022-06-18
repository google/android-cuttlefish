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

#include "host/libs/audio_connector/server.h"

#include <errno.h>
#include <fcntl.h>
#include <strings.h>
#include <unistd.h>

#include <utility>
#include <vector>

#include <android-base/logging.h>

#include "common/libs/fs/shared_select.h"

namespace cuttlefish {

namespace {

ScopedMMap AllocateShm(size_t size, const std::string& name, SharedFD* shm_fd) {
  *shm_fd = SharedFD::MemfdCreate(name, 0);
  if (!(*shm_fd)->IsOpen()) {
    LOG(FATAL) << "Unable to allocate create file for " << name << ": "
               << (*shm_fd)->StrError();
    return ScopedMMap();
  }

  auto truncate_ret = (*shm_fd)->Truncate(size);
  if (truncate_ret != 0) {
    LOG(FATAL) << "Unable to resize " << name << " to " << size
               << " bytes: " << (*shm_fd)->StrError();
    return ScopedMMap();
  }

  auto mmap_res = (*shm_fd)->MMap(NULL /* addr */, size, PROT_READ | PROT_WRITE,
                                  MAP_SHARED, 0 /*offset*/);
  if (!mmap_res) {
    LOG(FATAL) << "Unable to memory map " << name << ": "
               << (*shm_fd)->StrError();
  }
  return mmap_res;
}

bool CreateSocketPair(SharedFD* local, SharedFD* remote) {
  auto ret = SharedFD::SocketPair(AF_UNIX, SOCK_SEQPACKET, 0, local, remote);
  if (!ret) {
    LOG(ERROR) << "Unable to create socket pair for audio IO signaling: "
               << (*local)->StrError();
  }
  return ret;
}

std::function<void(AudioStatus, uint32_t, uint32_t)> SendStatusCallback(
    uint32_t buffer_offset, SharedFD socket) {
  // Consumption of an audio buffer is an asynchronous event, which could
  // trigger after the client disconnected. A WeakFD ensures that the response
  // will only be sent if there is still a client available.
  auto weak_socket = WeakFD(socket);
  return
      [buffer_offset, weak_socket](AudioStatus status, uint32_t latency_bytes,
                                   uint32_t consumed_length) {
        auto socket = weak_socket.lock();
        if (!socket->IsOpen()) {
          return;
        }
        IoStatusMsg reply;
        reply.status.status = Le32(static_cast<uint32_t>(status));
        reply.status.latency_bytes = Le32(latency_bytes);
        reply.buffer_offset = buffer_offset;
        reply.consumed_length = consumed_length;
        // Send the acknowledgment non-blockingly to avoid a slow client from
        // blocking the server.
        auto sent = socket->Send(&reply, sizeof(reply), MSG_DONTWAIT);
        if (sent < sizeof(reply)) {
          LOG(ERROR) << "Failed to send entire reply: " << socket->StrError();
        }
      };
}

}  // namespace

std::unique_ptr<AudioClientConnection> AudioServer::AcceptClient(
    uint32_t num_streams, uint32_t num_jacks, uint32_t num_chmaps,
    size_t tx_shm_len, size_t rx_shm_len) {
  auto conn_fd = SharedFD::Accept(*server_socket_, nullptr, 0);
  if (!conn_fd->IsOpen()) {
    LOG(ERROR) << "Connection failed on audio server: " << conn_fd->StrError();
    return nullptr;
  }
  return AudioClientConnection::Create(conn_fd, num_streams, num_jacks,
                                       num_chmaps, tx_shm_len, rx_shm_len);
}

/* static */
std::unique_ptr<AudioClientConnection> AudioClientConnection::Create(
    SharedFD client_socket, uint32_t num_streams, uint32_t num_jacks,
    uint32_t num_chmaps, size_t tx_shm_len, size_t rx_shm_len) {
  SharedFD event_socket, event_pair;
  SharedFD tx_socket, tx_pair;
  SharedFD rx_socket, rx_pair;

  bool pairs_created = true;
  pairs_created &= CreateSocketPair(&event_socket, &event_pair);
  pairs_created &= CreateSocketPair(&tx_socket, &tx_pair);
  pairs_created &= CreateSocketPair(&rx_socket, &rx_pair);
  if (!pairs_created) {
    return nullptr;
  }

  SharedFD tx_shm_fd, rx_shm_fd;
  auto tx_shm =
      AllocateShm(tx_shm_len, "vios_audio_server_tx_queue", &tx_shm_fd);
  if (!tx_shm) {
    return nullptr;
  }
  auto rx_shm =
      AllocateShm(rx_shm_len, "vios_audio_server_rx_queue", &rx_shm_fd);
  if (!rx_shm) {
    return nullptr;
  }

  VioSConfig welcome_msg = {
      .version = VIOS_VERSION,
      .jacks = num_jacks,
      .streams = num_streams,
      .chmaps = num_chmaps,
  };

  auto sent = client_socket->SendFileDescriptors(
      &welcome_msg, sizeof(welcome_msg), event_pair, tx_pair, rx_pair,
      tx_shm_fd, rx_shm_fd);
  if (sent < 0) {
    LOG(ERROR) << "Failed to send file descriptors to client: "
               << client_socket->StrError();
    return nullptr;
  }

  return std::unique_ptr<AudioClientConnection>(new AudioClientConnection(
      std::move(tx_shm), std::move(rx_shm), client_socket,
      event_socket, tx_socket, rx_socket));
}

bool AudioClientConnection::ReceiveCommands(AudioServerExecutor& executor) {
  // The largest msg the client will send is 24 bytes long, using uint64_t
  // guarantees it's aligned to 64 bits.
  uint64_t recv_buffer[3];
  auto recv_size =
      ReceiveMsg(control_socket_, &recv_buffer, sizeof(recv_buffer));
  if (recv_size <= 0) {
    return false;
  }
  const auto cmd_hdr = reinterpret_cast<const virtio_snd_hdr*>(&recv_buffer[0]);
  if (recv_size < sizeof(virtio_snd_hdr)) {
    LOG(ERROR) << "Received control message is too small: " << recv_size;
    return false;
  }
  switch (static_cast<AudioCommandType>(cmd_hdr->code.as_uint32_t())) {
    case AudioCommandType::VIRTIO_SND_R_PCM_INFO: {
      if (recv_size < sizeof(virtio_snd_query_info)) {
        LOG(ERROR) << "Received QUERY_INFO message is too small: " << recv_size;
        return false;
      }
      auto query_info = reinterpret_cast<const virtio_snd_query_info*>(cmd_hdr);
      auto info_count = query_info->count.as_uint32_t();
      auto start_id = query_info->start_id.as_uint32_t();
      std::unique_ptr<virtio_snd_pcm_info[]> reply(
          new virtio_snd_pcm_info[info_count]);
      StreamInfoCommand cmd(start_id, info_count, reply.get());

      executor.StreamsInfo(cmd);
      return CmdReply(cmd.status(), reply.get(),
                      info_count * sizeof(reply[0]));
    }
    case AudioCommandType::VIRTIO_SND_R_PCM_SET_PARAMS: {
      if (recv_size < sizeof(virtio_snd_pcm_set_params)) {
        LOG(ERROR) << "Received SET_PARAMS message is too small: " << recv_size;
        return false;
      }
      auto set_param_msg =
          reinterpret_cast<const virtio_snd_pcm_set_params*>(cmd_hdr);
      StreamSetParamsCommand cmd(set_param_msg->hdr.stream_id.as_uint32_t(),
                                 set_param_msg->buffer_bytes.as_uint32_t(),
                                 set_param_msg->period_bytes.as_uint32_t(),
                                 set_param_msg->features.as_uint32_t(),
                                 set_param_msg->channels, set_param_msg->format,
                                 set_param_msg->rate);
      executor.SetStreamParameters(cmd);
      return CmdReply(cmd.status());
    }
    case AudioCommandType::VIRTIO_SND_R_PCM_PREPARE: {
      if (recv_size < sizeof(virtio_snd_pcm_hdr)) {
        LOG(ERROR) << "Received PREPARE message is too small: " << recv_size;
        return false;
      }
      auto pcm_op_msg = reinterpret_cast<const virtio_snd_pcm_hdr*>(cmd_hdr);
      StreamControlCommand cmd(AudioCommandType::VIRTIO_SND_R_PCM_PREPARE,
                               pcm_op_msg->stream_id.as_uint32_t());
      executor.PrepareStream(cmd);
      return CmdReply(cmd.status());
    }
    case AudioCommandType::VIRTIO_SND_R_PCM_RELEASE: {
      if (recv_size < sizeof(virtio_snd_pcm_hdr)) {
        LOG(ERROR) << "Received RELEASE message is too small: " << recv_size;
        return false;
      }
      auto pcm_op_msg = reinterpret_cast<const virtio_snd_pcm_hdr*>(cmd_hdr);
      StreamControlCommand cmd(AudioCommandType::VIRTIO_SND_R_PCM_RELEASE,
                               pcm_op_msg->stream_id.as_uint32_t());
      executor.ReleaseStream(cmd);
      return CmdReply(cmd.status());
    }
    case AudioCommandType::VIRTIO_SND_R_PCM_START: {
      if (recv_size < sizeof(virtio_snd_pcm_hdr)) {
        LOG(ERROR) << "Received START message is too small: " << recv_size;
        return false;
      }
      auto pcm_op_msg = reinterpret_cast<const virtio_snd_pcm_hdr*>(cmd_hdr);
      StreamControlCommand cmd(AudioCommandType::VIRTIO_SND_R_PCM_START,
                               pcm_op_msg->stream_id.as_uint32_t());
      executor.StartStream(cmd);
      return CmdReply(cmd.status());
    }
    case AudioCommandType::VIRTIO_SND_R_PCM_STOP: {
      if (recv_size < sizeof(virtio_snd_pcm_hdr)) {
        LOG(ERROR) << "Received STOP message is too small: " << recv_size;
        return false;
      }
      auto pcm_op_msg = reinterpret_cast<const virtio_snd_pcm_hdr*>(cmd_hdr);
      StreamControlCommand cmd(AudioCommandType::VIRTIO_SND_R_PCM_STOP,
                               pcm_op_msg->stream_id.as_uint32_t());
      executor.StopStream(cmd);
      return CmdReply(cmd.status());
    }
    case AudioCommandType::VIRTIO_SND_R_CHMAP_INFO: {
      if (recv_size < sizeof(virtio_snd_query_info)) {
        LOG(ERROR) << "Received QUERY_INFO message is too small: " << recv_size;
        return false;
      }
      auto query_info = reinterpret_cast<const virtio_snd_query_info*>(cmd_hdr);
      auto info_count = query_info->count.as_uint32_t();
      auto start_id = query_info->start_id.as_uint32_t();
      std::unique_ptr<virtio_snd_chmap_info[]> reply(
          new virtio_snd_chmap_info[info_count]);
      ChmapInfoCommand cmd(start_id, info_count, reply.get());

      executor.ChmapsInfo(cmd);
      return CmdReply(cmd.status(), reply.get(),
                      info_count * sizeof(reply[0]));
    }
    case AudioCommandType::VIRTIO_SND_R_JACK_INFO: {
      if (recv_size < sizeof(virtio_snd_query_info)) {
        LOG(ERROR) << "Received QUERY_INFO message is too small: " << recv_size;
        return false;
      }
      auto query_info = reinterpret_cast<const virtio_snd_query_info*>(cmd_hdr);
      auto info_count = query_info->count.as_uint32_t();
      auto start_id = query_info->start_id.as_uint32_t();
      std::unique_ptr<virtio_snd_jack_info[]> reply(
          new virtio_snd_jack_info[info_count]);
      JackInfoCommand cmd(start_id, info_count, reply.get());

      executor.JacksInfo(cmd);
      return CmdReply(cmd.status(), reply.get(),
                      info_count * sizeof(reply[0]));
    }
    case AudioCommandType::VIRTIO_SND_R_JACK_REMAP:
      LOG(ERROR) << "Unsupported command type: " << cmd_hdr->code.as_uint32_t();
      return CmdReply(AudioStatus::VIRTIO_SND_S_NOT_SUPP);
    default:
      LOG(ERROR) << "Unknown command type: " << cmd_hdr->code.as_uint32_t();
      return CmdReply(AudioStatus::VIRTIO_SND_S_BAD_MSG);
  }
  return true;
}

bool AudioClientConnection::ReceivePlayback(AudioServerExecutor& executor) {
  // The largest msg the client will send is 12 bytes long, using uint32_t
  // guarantees it's aligned to 32 bits.
  uint32_t recv_buffer[3];
  auto recv_size = ReceiveMsg(tx_socket_, &recv_buffer, sizeof(recv_buffer));
  if (recv_size <= 0) {
    return false;
  }
  const auto msg_hdr = reinterpret_cast<const IoTransferMsg*>(&recv_buffer[0]);

  if (recv_size < sizeof(IoTransferMsg)) {
    LOG(ERROR) << "Received PCM_XFER message is too small: " << recv_size;
    return false;
  }
  TxBuffer buffer(msg_hdr->io_xfer,
                  TxBufferAt(msg_hdr->buffer_offset, msg_hdr->buffer_len),
                  msg_hdr->buffer_len,
                  SendStatusCallback(msg_hdr->buffer_offset, tx_socket_));
  executor.OnPlaybackBuffer(std::move(buffer));
  return true;
}

bool AudioClientConnection::ReceiveCapture(AudioServerExecutor& executor) {
  uint32_t recv_buffer[3];
  auto recv_size = ReceiveMsg(rx_socket_, &recv_buffer, sizeof(recv_buffer));
  if (recv_size <= 0) {
    return false;
  }
  const auto msg_hdr = reinterpret_cast<const IoTransferMsg*>(&recv_buffer[0]);
  if (recv_size < sizeof(IoTransferMsg)) {
    LOG(ERROR) << "Received PCM_XFER message is too small: " << recv_size;
    return false;
  }
  RxBuffer buffer(msg_hdr->io_xfer,
                  RxBufferAt(msg_hdr->buffer_offset, msg_hdr->buffer_len),
                  msg_hdr->buffer_len,
                  SendStatusCallback(msg_hdr->buffer_offset, rx_socket_));
  executor.OnCaptureBuffer(std::move(buffer));
  return true;
}

bool AudioClientConnection::CmdReply(AudioStatus status, const void* data,
                                     size_t size) {
  virtio_snd_hdr vio_status = {
      .code = Le32(static_cast<uint32_t>(status)),
  };
  std::vector<uint8_t> buffer(sizeof(vio_status) + size, 0);
  std::memcpy(buffer.data(), &vio_status, sizeof(vio_status));
  if (data) {
    std::memcpy(buffer.data() + sizeof(vio_status), data, size);
  }
  auto status_sent = control_socket_->Send(buffer.data(), buffer.size(), 0);
  if (status_sent < sizeof(vio_status) + size) {
    LOG(ERROR) << "Failed to send entire command status: "
               << control_socket_->StrError();
    return false;
  }
  return true;
}

const volatile uint8_t* AudioClientConnection::TxBufferAt(size_t offset,
                                                          size_t len) const {
  CHECK(tx_shm_.WithinBounds(offset, len))
      << "Tx buffer bounds outside the buffer area: " << offset << " " << len;
  const void* ptr = tx_shm_.get();
  return &reinterpret_cast<const volatile uint8_t*>(ptr)[offset];
}

volatile uint8_t* AudioClientConnection::RxBufferAt(size_t offset,
                                                    size_t len) {
  CHECK(rx_shm_.WithinBounds(offset, len))
      << "Rx buffer bounds outside the buffer area: " << offset << " " << len;
  void* ptr = rx_shm_.get();
  return &reinterpret_cast<volatile uint8_t*>(ptr)[offset];
}

bool AudioClientConnection::SendEvent(/*TODO*/) { return false; }

ssize_t AudioClientConnection::ReceiveMsg(SharedFD socket, void* buffer,
                                          size_t size) {
  auto read = socket->Recv(buffer, size, MSG_TRUNC);
  CHECK(read < 0 || read <= size)
      << "Received a msg bigger than the buffer, msg was truncated: " << read
      << " vs " << size;
  if (read == 0) {
    LOG(ERROR) << "Client closed the connection";
  }
  if (read < 0) {
    LOG(ERROR) << "Error receiving messages from client: "
               << socket->StrError();
  }
  return read;
}

}  // namespace cuttlefish
