/*
 * Copyright (C) 2018 The Android Open Source Project
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

namespace cuttlefish {

enum RunnerExitCodes : int {
  kSuccess = 0,
  kArgumentParsingError = 1,
  kInvalidHostConfiguration = 2,
  kCuttlefishConfigurationInitError = 3,
  kInstanceDirCreationError = 4,
  kPrioFilesCleanupError = 5,
  kBootImageUnpackError = 6,
  kCuttlefishConfigurationSaveError = 7,
  kDaemonizationError = 8,
  kVMCreationError = 9,
  kPipeIOError = 10,
  kVirtualDeviceBootFailed = 11,
  kProcessGroupError = 12,
  kMonitorCreationFailed = 13,
  kServerError = 14,
  kUsbV1SocketError = 15,
  kE2eTestFailed = 16,
  kKernelDecompressError = 17,
  kLogcatServerError = 18,
  kConfigServerError = 19,
  kTombstoneServerError = 20,
  kTombstoneDirCreationError = 21,
  kInitRamFsConcatError = 22,
  kTapDeviceInUse = 23,
  kTpmPassthroughError = 24,
};

// Actions supported by the launcher server
enum class LauncherAction : char {
  kPowerwash = 'P',
  kStatus = 'I',
  kStop = 'X',
};

// Responses from the launcher server
enum class LauncherResponse : char {
  kSuccess = 'S',
  kError = 'E',
  kUnknownAction = 'U',
};
}  // namespace cuttlefish
