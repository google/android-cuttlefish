/*
 * Copyright (C) 2026 The Android Open Source Project
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

#include "cuttlefish/host/commands/run_cvd/launch/virtual_tuner_server.h"

#include <sys/socket.h>  // IWYU pragma: keep: SOCK_STREAM

#include <optional>
#include <string>
#include <utility>

#include "fmt/format.h"

#include "cuttlefish/common/libs/fs/fd.h"
#include "cuttlefish/common/libs/fs/vm_sockets.h"
#include "cuttlefish/host/commands/assemble_cvd/proto/guest_config.pb.h"
#include "cuttlefish/host/libs/audio/channel_layout.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/host/libs/config/known_paths.h"
#include "cuttlefish/host/libs/feature/command_source.h"
#include "cuttlefish/process/command.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

// Returns the first virtual_tuner stream declared on any PCM device, or
// nullopt if audio is disabled or the guest audio config declares none.
std::optional<config::Audio::PCMDevice::Stream> VirtualTunerStream(
    const CuttlefishConfig::InstanceSpecific& instance) {
  if (!instance.enable_audio()) {
    return std::nullopt;
  }
  const std::optional<config::Audio> audio = instance.audio_settings();
  if (!audio.has_value()) {
    return std::nullopt;
  }
  for (const config::Audio::PCMDevice& device : audio->pcm_devices()) {
    if (device.has_virtual_tuner()) {
      return device.virtual_tuner();
    }
  }
  return std::nullopt;
}

// Returns the address the daemon's gRPC server listens on for the guest radio
// HAL: this instance's vhost-user vsock socket, or a host AF_VSOCK port.
std::string ControlServerAddress(
    const CuttlefishConfig::InstanceSpecific& instance) {
  const int port = instance.virtual_tuner_server_port();
  if (instance.vhost_user_vsock()) {
    return fmt::format("unix://{}", Fd::GetVhostUserVsockServerAddr(
                                        port, instance.vsock_guest_cid()));
  }
  return fmt::format("vsock:{}:{}", VMADDR_CID_HOST, port);
}

}  // namespace

bool HasVirtualTunerStream(const CuttlefishConfig::InstanceSpecific& instance) {
  return VirtualTunerStream(instance).has_value();
}

std::string VirtualTunerPcmSocketPath(
    const CuttlefishConfig::InstanceSpecific& instance) {
  return instance.PerInstanceInternalUdsPath("virtual_tuner_pcm.sock");
}

Result<std::optional<MonitorCommand>> VirtualTunerServer(
    const CuttlefishConfig::InstanceSpecific& instance) {
  const std::optional<config::Audio::PCMDevice::Stream> tuner =
      VirtualTunerStream(instance);
  if (!tuner.has_value()) {
    return std::nullopt;
  }

  // run_cvd owns the listening socket so that it exists before webRTC starts:
  // TunerAudioSource connects once, at construction, and never retries.
  const std::string path = VirtualTunerPcmSocketPath(instance);
  Fd pcm_server = CF_EXPECTF(
      Fd::SocketLocalServer(path, /*is_abstract=*/false, SOCK_STREAM, 0600),
      "Could not create the virtual tuner PCM socket \"{}\"", path);

  const int channels =
      GetChannelsCount(ConvertChannelLayout(tuner->channel_layout()));

  Command command(VirtualTunerDaemonBinary());
  // The guest radio HAL may connect before the daemon listens. Passing down a
  // socket created here, as for the PCM socket, would avoid that, but
  // grpc::ServerBuilder only listens on addresses it binds itself.
  command.AddParameter("--server_address=", ControlServerAddress(instance))
      .AddParameter("--pcm_server_fd=", std::move(pcm_server))
      .AddParameter("--pcm_channels=", channels);
  // Not critical: if the daemon exits, the device keeps running without tuner
  // audio.
  return MonitorCommand(std::move(command), /*is_critical=*/false);
}

}  // namespace cuttlefish
