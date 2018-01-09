/*
 * Copyright (C) 2013 The Android Open Source Project
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

#include "JpegStub.h"

#define LOG_NDEBUG 0
#define LOG_TAG "EmulatedCamera_JPEGStub"
#include <errno.h>
#include <cutils/log.h>
#include <stdlib.h>

#include "Compressor.h"

extern "C" void JpegStub_init(JpegStub* stub) {
    stub->mCompressor = static_cast<void*>(new Compressor());
}

extern "C" void JpegStub_cleanup(JpegStub* stub) {
    delete reinterpret_cast<Compressor*>(stub->mCompressor);
    stub->mCompressor = nullptr;
}

extern "C" int JpegStub_compress(JpegStub* stub,
                                 const void* buffer,
                                 int width,
                                 int height,
                                 int quality,
                                 ExifData* exifData)
{
    Compressor* compressor = reinterpret_cast<Compressor*>(stub->mCompressor);

    if (compressor->compress(reinterpret_cast<const unsigned char*>(buffer),
                              width, height, quality, exifData)) {
        ALOGV("%s: Compressed JPEG: %d[%dx%d] -> %zu bytes",
              __FUNCTION__, (width * height * 12) / 8,
              width, height, compressor->getCompressedData().size());
        return 0;
    }
    ALOGE("%s: JPEG compression failed", __FUNCTION__);
    return errno ? errno : EINVAL;
}

extern "C" void JpegStub_getCompressedImage(JpegStub* stub, void* buff) {
    Compressor* compressor = reinterpret_cast<Compressor*>(stub->mCompressor);

    const std::vector<unsigned char>& data = compressor->getCompressedData();
    memcpy(buff, &data[0], data.size());
}

extern "C" size_t JpegStub_getCompressedSize(JpegStub* stub) {
    Compressor* compressor = reinterpret_cast<Compressor*>(stub->mCompressor);

    return compressor->getCompressedData().size();
}
