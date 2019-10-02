
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

#ifndef CUTTLEFISH_CAMERA_EXIF_H
#define CUTTLEFISH_CAMERA_EXIF_H

#include <CameraParameters.h>

using ::android::hardware::camera::common::V1_0::helper::CameraParameters;

struct _ExifData;
typedef struct _ExifData ExifData;

namespace android {

/* Create an EXIF data structure based on camera parameters. This includes
 * things like GPS information that has been set by the camera client.
 */
ExifData* createExifData(const CameraParameters& parameters);

/* Free EXIF data created in the createExifData call */
void freeExifData(ExifData* exifData);

}  // namespace android

#endif  // CUTTLEFISH_CAMERA_EXIF_H

