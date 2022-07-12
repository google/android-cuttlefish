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

#include <string>

namespace cuttlefish {

std::string AdbConnectorBinary();
std::string ConfigServerBinary();
std::string ConsoleForwarderBinary();
std::string GnssGrpcProxyBinary();
std::string KernelLogMonitorBinary();
std::string LogcatReceiverBinary();
std::string MetricsBinary();
std::string ModemSimulatorBinary();
std::string RootCanalBinary();
std::string SocketVsockProxyBinary();
std::string TombstoneReceiverBinary();
std::string VehicleHalGrpcServerBinary();
std::string WebRtcBinary();
std::string WebRtcSigServerBinary();
std::string WebRtcSigServerProxyBinary();
std::string WmediumdBinary();
std::string WmediumdGenConfigBinary();

} // namespace cuttlefish
