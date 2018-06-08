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

#include "Compressor.h"

#define LOG_NDEBUG 0
#define LOG_TAG "EmulatedCamera_JPEGStub_Compressor"
#include <cutils/log.h>
#include <libexif/exif-data.h>

Compressor::Compressor() {

}

bool Compressor::compress(const unsigned char* data,
                          int width, int height, int quality,
                          ExifData* exifData) {
    if (!configureCompressor(width, height, quality)) {
        // The method will have logged a more detailed error message than we can
        // provide here so just return.
        return false;
    }

    return compressData(data, exifData);
}

const std::vector<uint8_t>& Compressor::getCompressedData() const {
    return mDestManager.mBuffer;
}

bool Compressor::configureCompressor(int width, int height, int quality) {
    mCompressInfo.err = jpeg_std_error(&mErrorManager);
    // NOTE! DANGER! Do not construct any non-trivial objects below setjmp!
    // The compiler will not generate code to destroy them during the return
    // below so they will leak. Additionally, do not place any calls to libjpeg
    // that can fail above this line or any error will cause undefined behavior.
    if (setjmp(mErrorManager.mJumpBuffer)) {
        // This is where the error handler will jump in case setup fails
        // The error manager will ALOG an appropriate error message
        return false;
    }

    jpeg_create_compress(&mCompressInfo);

    mCompressInfo.image_width = width;
    mCompressInfo.image_height = height;
    mCompressInfo.input_components = 3;
    mCompressInfo.in_color_space = JCS_YCbCr;
    jpeg_set_defaults(&mCompressInfo);

    jpeg_set_quality(&mCompressInfo, quality, TRUE);
    // It may seem weird to set color space here again but this will also set
    // other fields. These fields might be overwritten by jpeg_set_defaults
    jpeg_set_colorspace(&mCompressInfo, JCS_YCbCr);
    mCompressInfo.raw_data_in = TRUE;
    mCompressInfo.dct_method = JDCT_IFAST;
    // Set sampling factors
    mCompressInfo.comp_info[0].h_samp_factor = 2;
    mCompressInfo.comp_info[0].v_samp_factor = 2;
    mCompressInfo.comp_info[1].h_samp_factor = 1;
    mCompressInfo.comp_info[1].v_samp_factor = 1;
    mCompressInfo.comp_info[2].h_samp_factor = 1;
    mCompressInfo.comp_info[2].v_samp_factor = 1;

    mCompressInfo.dest = &mDestManager;

    return true;
}

static void deinterleave(const uint8_t* vuPlanar, std::vector<uint8_t>& uRows,
                         std::vector<uint8_t>& vRows, int rowIndex, int width,
                         int height, int stride) {
    int numRows = (height - rowIndex) / 2;
    if (numRows > 8) numRows = 8;
    for (int row = 0; row < numRows; ++row) {
        int offset = ((rowIndex >> 1) + row) * stride;
        const uint8_t* vu = vuPlanar + offset;
        for (int i = 0; i < (width >> 1); ++i) {
            int index = row * (width >> 1) + i;
            uRows[index] = vu[1];
            vRows[index] = vu[0];
            vu += 2;
        }
    }
}


bool Compressor::compressData(const unsigned char* data, ExifData* exifData) {
    const uint8_t* y[16];
    const uint8_t* cb[8];
    const uint8_t* cr[8];
    const uint8_t** planes[3] = { y, cb, cr };

    int i, offset;
    int width = mCompressInfo.image_width;
    int height = mCompressInfo.image_height;
    const uint8_t* yPlanar = data;
    const uint8_t* vuPlanar = data + (width * height);
    std::vector<uint8_t> uRows(8 * (width >> 1));
    std::vector<uint8_t> vRows(8 * (width >> 1));

    // NOTE! DANGER! Do not construct any non-trivial objects below setjmp!
    // The compiler will not generate code to destroy them during the return
    // below so they will leak. Additionally, do not place any calls to libjpeg
    // that can fail above this line or any error will cause undefined behavior.
    if (setjmp(mErrorManager.mJumpBuffer)) {
        // This is where the error handler will jump in case compression fails
        // The error manager will ALOG an appropriate error message
        return false;
    }

    jpeg_start_compress(&mCompressInfo, TRUE);

    attachExifData(exifData);

    // process 16 lines of Y and 8 lines of U/V each time.
    while (mCompressInfo.next_scanline < mCompressInfo.image_height) {
        //deinterleave u and v
        deinterleave(vuPlanar, uRows, vRows, mCompressInfo.next_scanline,
                     width, height, width);

        // Jpeg library ignores the rows whose indices are greater than height.
        for (i = 0; i < 16; i++) {
            // y row
            y[i] = yPlanar + (mCompressInfo.next_scanline + i) * width;

            // construct u row and v row
            if ((i & 1) == 0) {
                // height and width are both halved because of downsampling
                offset = (i >> 1) * (width >> 1);
                cb[i/2] = &uRows[offset];
                cr[i/2] = &vRows[offset];
            }
          }
        jpeg_write_raw_data(&mCompressInfo, const_cast<JSAMPIMAGE>(planes), 16);
    }

    jpeg_finish_compress(&mCompressInfo);
    jpeg_destroy_compress(&mCompressInfo);

    return true;
}

bool Compressor::attachExifData(ExifData* exifData) {
    if (exifData == nullptr) {
        // This is not an error, we don't require EXIF data
        return true;
    }

    // Save the EXIF data to memory
    unsigned char* rawData = nullptr;
    unsigned int size = 0;
    exif_data_save_data(exifData, &rawData, &size);
    if (rawData == nullptr) {
        ALOGE("Failed to create EXIF data block");
        return false;
    }

    jpeg_write_marker(&mCompressInfo, JPEG_APP0 + 1, rawData, size);
    free(rawData);
    return true;
}

Compressor::ErrorManager::ErrorManager() {
    error_exit = &onJpegError;
}

void Compressor::ErrorManager::onJpegError(j_common_ptr cinfo) {
    // NOTE! Do not construct any non-trivial objects in this method at the top
    // scope. Their destructors will not be called. If you do need such an
    // object create a local scope that does not include the longjmp call,
    // that ensures the object is destroyed before longjmp is called.
    ErrorManager* errorManager = reinterpret_cast<ErrorManager*>(cinfo->err);

    // Format and log error message
    char errorMessage[JMSG_LENGTH_MAX];
    (*errorManager->format_message)(cinfo, errorMessage);
    errorMessage[sizeof(errorMessage) - 1] = '\0';
    ALOGE("JPEG compression error: %s", errorMessage);
    jpeg_destroy(cinfo);

    // And through the looking glass we go
    longjmp(errorManager->mJumpBuffer, 1);
}

Compressor::DestinationManager::DestinationManager() {
    init_destination = &initDestination;
    empty_output_buffer = &emptyOutputBuffer;
    term_destination = &termDestination;
}

void Compressor::DestinationManager::initDestination(j_compress_ptr cinfo) {
    auto manager = reinterpret_cast<DestinationManager*>(cinfo->dest);

    // Start out with some arbitrary but not too large buffer size
    manager->mBuffer.resize(16 * 1024);
    manager->next_output_byte = &manager->mBuffer[0];
    manager->free_in_buffer = manager->mBuffer.size();
}

boolean Compressor::DestinationManager::emptyOutputBuffer(
        j_compress_ptr cinfo) {
    auto manager = reinterpret_cast<DestinationManager*>(cinfo->dest);

    // Keep doubling the size of the buffer for a very low, amortized
    // performance cost of the allocations
    size_t oldSize = manager->mBuffer.size();
    manager->mBuffer.resize(oldSize * 2);
    manager->next_output_byte = &manager->mBuffer[oldSize];
    manager->free_in_buffer = manager->mBuffer.size() - oldSize;
    return manager->free_in_buffer != 0;
}

void Compressor::DestinationManager::termDestination(j_compress_ptr cinfo) {
    auto manager = reinterpret_cast<DestinationManager*>(cinfo->dest);

    // Resize down to the exact size of the output, that is remove as many
    // bytes as there are left in the buffer
    manager->mBuffer.resize(manager->mBuffer.size() - manager->free_in_buffer);
}

