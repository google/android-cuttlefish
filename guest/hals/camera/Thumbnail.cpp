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

#include "Thumbnail.h"

#define LOG_NDEBUG 0
#define LOG_TAG "EmulatedCamera_Thumbnail"
#include <cutils/log.h>
#include <libexif/exif-data.h>
#include <libyuv.h>

#include "JpegCompressor.h"

#include <vector>

/*
 * The NV21 format is a YUV format with an 8-bit Y-component and the U and V
 * components are stored as 8 bits each but they are shared between a block of
 * 2x2 pixels. So when calculating bits per pixel the 16 bits of U and V are
 * shared between 4 pixels leading to 4 bits of U and V per pixel. Together
 * with the 8 bits of Y this gives us 12 bits per pixel..
 *
 * The components are not grouped by pixels but separated into one Y-plane and
 * one interleaved U and V-plane. The first half of the byte sequence is all of
 * the Y data laid out in a linear fashion. After that the interleaved U and V-
 * plane starts with one byte of V followed by one byte of U followed by one
 * byte of V and so on. Each byte of U or V is associated with a 2x2 pixel block
 * in a linear fashion.
 *
 * For an 8 by 4 pixel image the layout would be:
 *
 * +-----+-----+-----+-----+-----+-----+-----+-----+
 * | Y0  | Y1  | Y2  | Y3  | Y4  | Y5  | Y6  | Y7  |
 * +-----+-----+-----+-----+-----+-----+-----+-----+
 * | Y8  | Y9  | Y10 | Y11 | Y12 | Y13 | Y14 | Y15 |
 * +-----+-----+-----+-----+-----+-----+-----+-----+
 * | Y16 | Y17 | Y18 | Y19 | Y20 | Y21 | Y22 | Y23 |
 * +-----+-----+-----+-----+-----+-----+-----+-----+
 * | Y24 | Y25 | Y26 | Y27 | Y28 | Y29 | Y30 | Y31 |
 * +-----+-----+-----+-----+-----+-----+-----+-----+
 * | V0  | U0  | V1  | U1  | V2  | U2  | V3  | U3  |
 * +-----+-----+-----+-----+-----+-----+-----+-----+
 * | V4  | U4  | V5  | U5  | V6  | U6  | V7  | U7  |
 * +-----+-----+-----+-----+-----+-----+-----+-----+
 *
 * In this image V0 and U0 are the V and U components for the 2x2 block of
 * pixels whose Y components are Y0, Y1, Y8 and Y9. V1 and U1 are matched with
 * the Y components Y2, Y3, Y10, Y11, and so on for that row. For the next row
 * of V and U the V4 and U4 components would be paired with Y16, Y17, Y24 and
 * Y25.
 */

namespace android {

static bool createRawThumbnail(const unsigned char* sourceImage,
                               int sourceWidth, int sourceHeight,
                               int thumbnailWidth, int thumbnailHeight,
                               std::vector<unsigned char>* thumbnail) {
    // Deinterleave the U and V planes into separate planes, this is because
    // libyuv requires the planes to be separate when scaling
    const size_t sourceUVPlaneSize = (sourceWidth * sourceHeight) / 4;
    // Put both U and V planes in one buffer, one after the other, to reduce
    // memory fragmentation and number of allocations
    std::vector<unsigned char> sourcePlanes(sourceUVPlaneSize * 2);
    const unsigned char* ySourcePlane = sourceImage;
    unsigned char* uSourcePlane = &sourcePlanes[0];
    unsigned char* vSourcePlane = &sourcePlanes[sourceUVPlaneSize];

    for (size_t i = 0; i < sourceUVPlaneSize; ++i) {
        vSourcePlane[i] = sourceImage[sourceWidth * sourceHeight + i * 2 + 0];
        uSourcePlane[i] = sourceImage[sourceWidth * sourceHeight + i * 2 + 1];
    }

    // Create enough space in the output vector for the result
    thumbnail->resize((thumbnailWidth * thumbnailHeight * 12) / 8);

    // The downscaled U and V planes will also be linear instead of interleaved,
    // allocate space for them here
    const size_t destUVPlaneSize = (thumbnailWidth * thumbnailHeight) / 4;
    std::vector<unsigned char> destPlanes(destUVPlaneSize * 2);
    unsigned char* yDestPlane = &(*thumbnail)[0];
    unsigned char* uDestPlane = &destPlanes[0];
    unsigned char* vDestPlane = &destPlanes[destUVPlaneSize];

    // The strides for the U and V planes are half the width because the U and V
    // components are common to 2x2 pixel blocks
    int result = libyuv::I420Scale(ySourcePlane, sourceWidth,
                                   uSourcePlane, sourceWidth / 2,
                                   vSourcePlane, sourceWidth / 2,
                                   sourceWidth, sourceHeight,
                                   yDestPlane, thumbnailWidth,
                                   uDestPlane, thumbnailWidth / 2,
                                   vDestPlane, thumbnailWidth / 2,
                                   thumbnailWidth, thumbnailHeight,
                                   libyuv::kFilterBilinear);
    if (result != 0) {
        ALOGE("Unable to create thumbnail, downscaling failed with error: %d",
              result);
        return false;
    }

    // Now we need to interleave the downscaled U and V planes into the
    // output buffer to make it NV21 encoded
    const size_t uvPlanesOffset = thumbnailWidth * thumbnailHeight;
    for (size_t i = 0; i < destUVPlaneSize; ++i) {
        (*thumbnail)[uvPlanesOffset + i * 2 + 0] = vDestPlane[i];
        (*thumbnail)[uvPlanesOffset + i * 2 + 1] = uDestPlane[i];
    }

    return true;
}

bool createThumbnail(const unsigned char* sourceImage,
                     int sourceWidth, int sourceHeight,
                     int thumbWidth, int thumbHeight, int quality,
                     ExifData* exifData) {
    if (thumbWidth <= 0 || thumbHeight <= 0) {
        ALOGE("%s: Invalid thumbnail width=%d or height=%d, must be > 0",
              __FUNCTION__, thumbWidth, thumbHeight);
        return false;
    }

    // First downscale the source image into a thumbnail-sized raw image
    std::vector<unsigned char> rawThumbnail;
    if (!createRawThumbnail(sourceImage, sourceWidth, sourceHeight,
                            thumbWidth, thumbHeight, &rawThumbnail)) {
        // The thumbnail function will log an appropriate error if needed
        return false;
    }

    // And then compress it into JPEG format without any EXIF data
    NV21JpegCompressor compressor;
    status_t result = compressor.compressRawImage(&rawThumbnail[0],
                                                  nullptr /* EXIF */,
                                                  quality, thumbWidth, thumbHeight);
    if (result != NO_ERROR) {
        ALOGE("%s: Unable to compress thumbnail", __FUNCTION__);
        return false;
    }

    // And finally put it in the EXIF data. This transfers ownership of the
    // malloc'd memory to the EXIF data structure. As long as the EXIF data
    // structure is free'd using the EXIF library this memory will be free'd.
    exifData->size = compressor.getCompressedSize();
    exifData->data = reinterpret_cast<unsigned char*>(malloc(exifData->size));
    if (exifData->data == nullptr) {
        ALOGE("%s: Unable to allocate %u bytes of memory for thumbnail",
              __FUNCTION__, exifData->size);
        exifData->size = 0;
        return false;
    }
    compressor.getCompressedImage(exifData->data);
    return true;
}

}  // namespace android

