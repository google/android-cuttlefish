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

#include "v4l2_helpers.h"

#include <fcntl.h>
#include <linux/videodev2.h>
#include <log/log.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace cuttlefish {

Result<size_t> V4l2GetBpp(int format) {
  CF_EXPECT(format == V4L2_PIX_FMT_BGRX32,
            "Error: v4l2_get_bpp; only V4L2_PIX_FMT_BGRX32 supported");
  return 4;
}

Result<size_t> V4l2GetFrameSize(int format, int width, int height) {
  size_t bytes_per_pixel =
      CF_EXPECT(V4l2GetBpp(format), "Error: invalid bpp format");

  return width * height * bytes_per_pixel;
}

Result<size_t> V4l2GetLineWidth(int format, int width) {
  size_t bytes_per_pixel =
      CF_EXPECT(V4l2GetBpp(format), "Error: invalid bpp format");

  return width * bytes_per_pixel;
}

void V4l2PrintFormat(struct v4l2_format* vid_format) {
  ALOGI("	vid_format->type                =%d", vid_format->type);
  ALOGI("	vid_format->fmt.pix.width       =%d",
        vid_format->fmt.pix.width);
  ALOGI("	vid_format->fmt.pix.height      =%d",
        vid_format->fmt.pix.height);
  ALOGI("	vid_format->fmt.pix.pixelformat =%d",
        vid_format->fmt.pix.pixelformat);
  ALOGI("	vid_format->fmt.pix.sizeimage   =%d",
        vid_format->fmt.pix.sizeimage);
  ALOGI("	vid_format->fmt.pix.field       =%d",
        vid_format->fmt.pix.field);
  ALOGI("	vid_format->fmt.pix.bytesperline=%d",
        vid_format->fmt.pix.bytesperline);
  ALOGI("	vid_format->fmt.pix.colorspace  =%d",
        vid_format->fmt.pix.colorspace);
}

Result<std::vector<char>> V4l2ReadRawFile(const std::string& filename) {
  std::streampos filepos = 0;
  std::ifstream file(filename, std::ios::binary);

  filepos = file.tellg();
  file.seekg(0, std::ios::end);
  long buffersize = file.tellg() - filepos;
  file.seekg(0, std::ios::beg);

  std::vector<char> buffer;
  buffer.resize(buffersize);

  file.read(buffer.data(), buffersize);

  CF_EXPECT_NE(file.fail(), 0,
               "Error reading Raw file buffer: " << strerror(errno));

  ALOGI("Allocated and read %ld bytes", buffersize);

  return buffer;
}

Result<SharedFD> V4l2InitDevice(const std::string& device_path, int format,
                                int width, int height) {
  int framesize = CF_EXPECT(V4l2GetFrameSize(format, width, height),
                            "Error calculating frame size");
  int linewidth =
      CF_EXPECT(V4l2GetLineWidth(format, width), "Error calculating linewidth");

  SharedFD fdwr = SharedFD::Open(device_path, O_RDWR);

  CF_EXPECT(fdwr->IsOpen(), "Error: Could not open v4l2 device for O_RDWR: "
                                << fdwr->StrError());

  struct v4l2_capability vid_caps;
  int ret_code = fdwr->Ioctl(VIDIOC_QUERYCAP, &vid_caps);

  CF_EXPECT_NE(ret_code, -1,
               "Error: VIDIOC_QUERYCAP failed: " << fdwr->StrError());

  struct v4l2_format vid_format = v4l2_format{};

  V4l2PrintFormat(&vid_format);

  vid_format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
  vid_format.fmt.pix.width = width;
  vid_format.fmt.pix.height = height;
  vid_format.fmt.pix.pixelformat = format;
  vid_format.fmt.pix.sizeimage = framesize;
  vid_format.fmt.pix.field = V4L2_FIELD_NONE;
  vid_format.fmt.pix.bytesperline = linewidth;
  vid_format.fmt.pix.colorspace = V4L2_COLORSPACE_SRGB;

  V4l2PrintFormat(&vid_format);

  ret_code = fdwr->Ioctl(VIDIOC_S_FMT, &vid_format);

  CF_EXPECT_NE(ret_code, -1,
               "Error: VIDIOC_S_FMT failed: " << fdwr->StrError());

  ALOGI("frame: format=%d\tsize=%d", format, framesize);
  V4l2PrintFormat(&vid_format);

  return fdwr;
}

// This is a testing / debugging method. Only used optionally for
// troubleshooting a v4l2 by dumping raw movie frames direct to the device. It
// avoids using the network for simplifying the debug process.   It also shows
// how to use the API methods provided in this file.
Result<void> V4l2StreamFile(const std::string& device_path,
                            const std::string& raw_movie_file) {
  int width = 640;
  int height = 480;
  int format = V4L2_PIX_FMT_BGRX32;
  int framesize = CF_EXPECT(V4l2GetFrameSize(format, width, height),
                            "Error getting frame size");

  ALOGI("Starting.... using framesize(%d)", framesize);

  std::vector<char> buffer =
      CF_EXPECT(V4l2ReadRawFile(raw_movie_file), "Error reading buffer");

  ALOGI("Beginning frame push with buffersize(%ld)", buffer.size());

  SharedFD fdwr = CF_EXPECT(V4l2InitDevice(device_path, format, width, height),
                            "Error initializing device");

  CF_EXPECT(fdwr->IsOpen(), "Error: initdevice == 0");

  ALOGI("Device initialized(%s)", device_path.c_str());

  ALOGI("Beginning stream:");

  CF_EXPECT(buffer.size() > framesize, "Error: invalid buffer size");

  for (long i = 0; i < buffer.size() - framesize; i += framesize) {
    ALOGI("Beginning frame:");
    if (fdwr->Write(((char*)buffer.data()) + i, framesize) == -1) {
      ALOGE("Error writing buffer data: %s", fdwr->StrError().c_str());
    }
    sleep(1);
    if (i % 20 == 0) {
      ALOGI("Wrote %ld frames", ((i + framesize) / framesize));
    }
  }

  ALOGI("ended stream:");

  fdwr->Close();

  ALOGI("Streaming complete.");

  return {};
}

}  // End namespace cuttlefish