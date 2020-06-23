/*
 * Copyright (C) 2011 The Android Open Source Project
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

/*
 * Contains implementation of a class EmulatedFakeCamera that encapsulates
 * functionality of a fake camera.
 */

#define LOG_NDEBUG 0
#define LOG_TAG "EmulatedCamera_FakeCamera"
#include "EmulatedFakeCamera.h"
#include <log/log.h>
#include <cutils/properties.h>
#undef min
#undef max
#include <algorithm>
#include <sstream>
#include <string>
#include "EmulatedCameraFactory.h"

namespace android {

EmulatedFakeCamera::EmulatedFakeCamera(int cameraId, bool facingBack,
                                       struct hw_module_t* module)
    : EmulatedCamera(cameraId, module),
      mFacingBack(facingBack),
      mFakeCameraDevice(this) {}

EmulatedFakeCamera::~EmulatedFakeCamera() {}

/****************************************************************************
 * Public API overrides
 ***************************************************************************/

status_t EmulatedFakeCamera::Initialize(const cuttlefish::CameraDefinition& params) {
  status_t res = mFakeCameraDevice.Initialize();
  if (res != NO_ERROR) {
    return res;
  }

  const char* facing =
      mFacingBack ? EmulatedCamera::FACING_BACK : EmulatedCamera::FACING_FRONT;

  mParameters.set(EmulatedCamera::FACING_KEY, facing);
  ALOGD("%s: Fake camera is facing %s", __FUNCTION__, facing);

  mParameters.set(EmulatedCamera::ORIENTATION_KEY,
                  EmulatedCameraFactory::Instance().getFakeCameraOrientation());

  res = EmulatedCamera::Initialize(params);
  if (res != NO_ERROR) {
    return res;
  }

  /*
   * Parameters provided by the camera device.
   */

  /* 352x288 and 320x240 frame dimensions are required by the framework for
   * video mode preview and video recording. */
  std::ostringstream resolutions;
  for (size_t index = 0; index < params.resolutions.size(); ++index) {
    if (resolutions.str().size()) {
      resolutions << ",";
    }
    resolutions << params.resolutions[index].width << "x"
                << params.resolutions[index].height;
  }

  mParameters.set(CameraParameters::KEY_SUPPORTED_PICTURE_SIZES,
                  resolutions.str().c_str());
  mParameters.set(CameraParameters::KEY_SUPPORTED_PREVIEW_SIZES,
                  resolutions.str().c_str());
  mParameters.setPreviewSize(640, 480);
  mParameters.setPictureSize(640, 480);

  mParameters.set(CameraParameters::KEY_SUPPORTED_ANTIBANDING,
                  CameraParameters::ANTIBANDING_AUTO);
  mParameters.set(CameraParameters::KEY_ANTIBANDING,
                  CameraParameters::ANTIBANDING_AUTO);
  mParameters.set(CameraParameters::KEY_SUPPORTED_EFFECTS,
                  CameraParameters::EFFECT_NONE);
  mParameters.set(CameraParameters::KEY_EFFECT, CameraParameters::EFFECT_NONE);

  return NO_ERROR;
}

EmulatedCameraDevice* EmulatedFakeCamera::getCameraDevice() {
  return &mFakeCameraDevice;
}

}; /* namespace android */
