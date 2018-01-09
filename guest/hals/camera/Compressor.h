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

#ifndef CUTTLEFISH_CAMERA_JPEG_STUB_COMPRESSOR_H
#define CUTTLEFISH_CAMERA_JPEG_STUB_COMPRESSOR_H

#include <setjmp.h>
#include <stdlib.h>
extern "C" {
#include <jpeglib.h>
#include <jerror.h>
}

#include <vector>

struct _ExifData;
typedef _ExifData ExifData;

class Compressor {
public:
    Compressor();

    /* Compress |data| which represents raw NV21 encoded data of dimensions
     * |width| * |height|. |exifData| is optional EXIF data that will be
     * attached to the compressed data if present, set to null if not needed.
     */
    bool compress(const unsigned char* data,
                  int width, int height, int quality,
                  ExifData* exifData);

    /* Get a reference to the compressed data, this will return an empty vector
     * if compress has not been called yet
     */
    const std::vector<unsigned char>& getCompressedData() const;

private:
    struct DestinationManager : jpeg_destination_mgr {
        DestinationManager();

        static void initDestination(j_compress_ptr cinfo);
        static boolean emptyOutputBuffer(j_compress_ptr cinfo);
        static void termDestination(j_compress_ptr cinfo);

        std::vector<unsigned char> mBuffer;
    };
    struct ErrorManager : jpeg_error_mgr {
        ErrorManager();

        static void onJpegError(j_common_ptr cinfo);

        jmp_buf mJumpBuffer;
    };

    jpeg_compress_struct mCompressInfo;
    DestinationManager mDestManager;
    ErrorManager mErrorManager;

    bool configureCompressor(int width, int height, int quality);
    bool compressData(const unsigned char* data, ExifData* exifData);
    bool attachExifData(ExifData* exifData);
};

#endif  // CUTTLEFISH_CAMERA_JPEG_STUB_COMPRESSOR_H

