
/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include <linux/videodev2.h>
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/result.h"

namespace cuttlefish {

// Opens a v4l2 device, located at given [device_path]. The device is then
// configured to receive frames of the given format, width, and height. Note
// that only format V4L2_PIX_FMT_BGRX32 is supported at this time
Result<SharedFD> V4l2InitDevice(const std::string& device_path, int format,
                                int width, int height);

// Returns # of bytes per pixel of given format, for
// frame size calculations
// Note that only format V4L2_PIX_FMT_BGRX32 is supported at this time
Result<size_t> V4l2GetBPP(int format);

// Returns size in bytes of single frame of given v4l2 format
// Note that only format V4L2_PIX_FMT_BGRX32 is supported at this time
Result<size_t> V4l2GetFrameSize(int format, int width, int height);

// Returns size in bytes of a single line data in video fram image
// Note that only format V4L2_PIX_FMT_BGRX32 is supported at this time
Result<size_t> V4l2GetLineWidth(int format, int width);

// Dump to logger debug info of the given v4l2_format
void V4l2PrintFormat(struct v4l2_format* vid_format);

// The following two optional methods are used for debugging / testing v4l2
// devices, not by the runtime streamer.
Result<void> V4l2StreamFile();

// Reads a file containing raw frames in BGRA32 format.
Result<std::vector<char>> V4l2ReadRawFile(const std::string& filename);

}  // End namespace cuttlefish