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

#define LOG_NDEBUG 0
#define LOG_TAG "EmulatedCamera_JPEGStub"
#include <errno.h>
#include <cutils/log.h>
#include <libyuv.h>
#include <YuvToJpegEncoder.h>

#include "ExifMetadataBuilder.h"
#include "JpegStub.h"

namespace {
bool GenerateThumbnail(
    const uint8_t* source_nv21, int source_width, int source_height,
    int thumbnail_width, int thumbnail_height, SkDynamicMemoryWStream* target) {
  // We need to convert the image to Y'UV420SP to I420, which seems to be the
  // only scalable format by the LibYUV.
  // These formats are similar in their memory occupancy (both use about 3/2 of
  // the total pixels).
  int temp_y_size = source_width * source_height;
  int temp_uv_size = temp_y_size / 4;
  uint8_t* temp_y = (uint8_t*)malloc(temp_y_size + temp_uv_size + temp_uv_size);
  uint8_t* temp_u = temp_y + temp_y_size;
  uint8_t* temp_v = temp_u + temp_uv_size;

  libyuv::NV12ToI420(
      source_nv21, source_width,
      source_nv21 + temp_y_size, source_width,
      temp_y, source_width,
      temp_u, source_width / 2,
      temp_v, source_width / 2,
      source_width, source_height);

  // Compute and allocate memory for thumbnail I420.
  int thumb_y_size = thumbnail_width * thumbnail_height;
  int thumb_uv_size = thumb_y_size / 4;
  uint8_t* thumb_y = (uint8_t*)malloc(thumb_y_size + thumb_uv_size + thumb_uv_size);
  uint8_t* thumb_u = thumb_y + thumb_y_size;
  uint8_t* thumb_v = thumb_u + thumb_uv_size;

  libyuv::I420Scale(
      temp_y, source_width,
      temp_u, source_width / 2,
      temp_v, source_width / 2,
      source_width, source_height,
      thumb_y, thumbnail_width,
      thumb_u, thumbnail_width / 2,
      thumb_v, thumbnail_width / 2,
      thumbnail_width, thumbnail_height,
      libyuv::kFilterBilinear);

  // Combine U and V components back to NV21 format.
  // We can re-use temp_y buffer for our needs at this point.
  for (int pix = 0; pix < thumb_uv_size; ++pix) {
    temp_y[2 * pix] = thumb_v[pix];
    temp_y[2 * pix + 1] = thumb_u[pix];
  }

  // Put the memory back. After this, the thumb_y points to beginning of NV21
  // image which we can compress.
  memcpy(thumb_u, temp_y, thumb_uv_size * 2);

  // Compress image.
  int strides[2] = { thumbnail_width, thumbnail_width };
  int offsets[2] = { 0, thumb_y_size };
  Yuv420SpToJpegEncoder* encoder = new Yuv420SpToJpegEncoder(strides);

  bool result = encoder->encode(
      target, thumb_y, thumbnail_width, thumbnail_height, offsets, 90);

  if (!result) {
    ALOGE("%s: Thumbnail compression failed", __FUNCTION__);
  }

  delete(encoder);
  free(thumb_y);
  free(temp_y);

  return result;
}
}  // namespace

extern "C" void JpegStub_init(JpegStub* stub, int* strides) {
    stub->mInternalEncoder = (void*) new Yuv420SpToJpegEncoder(strides);
    stub->mInternalStream = (void*)new SkDynamicMemoryWStream();
    stub->mExifBuilder = (void*)new android::ExifMetadataBuilder();
}

extern "C" void JpegStub_cleanup(JpegStub* stub) {
    delete((Yuv420SpToJpegEncoder*)stub->mInternalEncoder);
    delete((SkDynamicMemoryWStream*)stub->mInternalStream);
    delete((android::ExifMetadataBuilder*)stub->mExifBuilder);
}

extern "C" int JpegStub_compress(JpegStub* stub, const void* image,
                                 int quality, const ImageMetadata* meta)
{
    void* pY = const_cast<void*>(image);

    int offsets[2];
    offsets[0] = 0;
    offsets[1] = meta->mWidth * meta->mHeight;

    Yuv420SpToJpegEncoder* encoder =
        (Yuv420SpToJpegEncoder*)stub->mInternalEncoder;
    SkDynamicMemoryWStream* stream =
        (SkDynamicMemoryWStream*)stub->mInternalStream;
    android::ExifMetadataBuilder* exif =
        (android::ExifMetadataBuilder*)stub->mExifBuilder;

    exif->SetWidth(meta->mWidth);
    exif->SetHeight(meta->mHeight);
    exif->SetDateTime(time(NULL));
    if (meta->mLensFocalLength != -1)
      exif->SetLensFocalLength(meta->mLensFocalLength);
    if (meta->mGpsTimestamp != -1) {
      exif->SetGpsLatitude(meta->mGpsLatitude);
      exif->SetGpsLongitude(meta->mGpsLongitude);
      exif->SetGpsAltitude(meta->mGpsAltitude);
      exif->SetGpsDateTime(meta->mGpsTimestamp);
      exif->SetGpsProcessingMethod(meta->mGpsProcessingMethod);
    }

    ALOGV("%s: Requested thumbnail size: %dx%d",
          __FUNCTION__, meta->mThumbnailWidth, meta->mThumbnailHeight);

    // Thumbnail requested?
    if (meta->mThumbnailWidth > 0 && meta->mThumbnailHeight > 0) {
      exif->SetThumbnailWidth(meta->mThumbnailWidth);
      exif->SetThumbnailHeight(meta->mThumbnailHeight);
      SkDynamicMemoryWStream* thumbnail = new SkDynamicMemoryWStream();
      GenerateThumbnail(
          (uint8_t*)pY,
          meta->mWidth, meta->mHeight,
          meta->mThumbnailWidth, meta->mThumbnailHeight,
          thumbnail);

      int thumbnail_size = thumbnail->bytesWritten();
      void* thumbnail_data = malloc(thumbnail_size);
      thumbnail->read(thumbnail_data, 0, thumbnail_size);
      // Pass ownership to EXIF builder.
      exif->SetThumbnail(thumbnail_data, thumbnail_size);
      delete thumbnail;
    }

    exif->Build();

    if (encoder->encode(stream, pY, meta->mWidth, meta->mHeight,
                        offsets, quality)) {
        ALOGI("%s: Compressed JPEG: %d[%dx%d] -> %zu bytes",
              __FUNCTION__, (meta->mWidth * meta->mHeight * 12) / 8,
              meta->mWidth, meta->mHeight, stream->bytesWritten());
        return 0;
    } else {
        ALOGE("%s: JPEG compression failed", __FUNCTION__);
        return errno ? errno: EINVAL;
    }
}

extern "C" void JpegStub_getCompressedImage(JpegStub* stub, void* buff) {
    SkDynamicMemoryWStream* stream =
        (SkDynamicMemoryWStream*)stub->mInternalStream;
    android::ExifMetadataBuilder* exif =
        (android::ExifMetadataBuilder*)stub->mExifBuilder;
    char* target = (char*)buff;
    memcpy(buff, exif->Buffer().data(), exif->Buffer().size());
    target += exif->Buffer().size();

    // Skip 0xFFD8 marker. This marker has already been included in Metadata.
    stream->read(target, 2, stream->bytesWritten() - 2);
}

extern "C" size_t JpegStub_getCompressedSize(JpegStub* stub) {
    SkDynamicMemoryWStream* stream =
        (SkDynamicMemoryWStream*)stub->mInternalStream;
    android::ExifMetadataBuilder* exif =
        (android::ExifMetadataBuilder*)stub->mExifBuilder;
    return stream->bytesWritten() + exif->Buffer().size() - 2;
}
