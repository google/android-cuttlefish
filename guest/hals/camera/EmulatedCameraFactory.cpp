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
 * Contains implementation of a class EmulatedCameraFactory that manages cameras
 * available for emulation.
 */

#define LOG_NDEBUG 0
#define LOG_TAG "EmulatedCamera_Factory"
#include <log/log.h>
#include <cutils/properties.h>
#include "EmulatedFakeCamera.h"

#include "EmulatedCameraHotplugThread.h"
#include "EmulatedFakeCamera2.h"

#include "EmulatedFakeCamera3.h"

#include "EmulatedCameraFactory.h"

extern camera_module_t HAL_MODULE_INFO_SYM;

namespace android {
EmulatedCameraFactory& EmulatedCameraFactory::Instance() {
  static EmulatedCameraFactory* factory = new EmulatedCameraFactory;
  return *factory;
}

EmulatedCameraFactory::EmulatedCameraFactory()
    : mCallbacks(NULL)
{
  mCameraConfiguration.Init();
  const std::vector<cvd::CameraDefinition>& cameras =
      mCameraConfiguration.cameras();
  for (size_t camera_index = 0; camera_index < cameras.size(); ++camera_index) {
    mCameraDefinitions.push(cameras[camera_index]);
    /* Reserve a spot for camera, but don't create just yet. */
    mEmulatedCameras.push(NULL);
  }

  ALOGV("%zu cameras are being emulated.", getEmulatedCameraNum());

  /* Create hotplug thread */
  {
    mHotplugThread = new EmulatedCameraHotplugThread(getEmulatedCameraNum());
    mHotplugThread->run("EmulatedCameraHotplugThread");
  }
}

EmulatedBaseCamera* EmulatedCameraFactory::getOrCreateFakeCamera(
    size_t cameraId) {
  std::lock_guard lock(mEmulatedCamerasMutex);

  if (cameraId >= getEmulatedCameraNum()) {
    ALOGE("%s: Invalid camera ID: %zu", __FUNCTION__, cameraId);
    return NULL;
  }

  if (mEmulatedCameras[cameraId] != NULL) {
    return mEmulatedCameras[cameraId];
  }

  const cvd::CameraDefinition& definition = mCameraDefinitions[cameraId];
  bool is_back_facing =
      (definition.orientation == cvd::CameraDefinition::kBack);

  EmulatedBaseCamera* camera;
  /* Create, and initialize the fake camera */
  switch (definition.hal_version) {
    case cvd::CameraDefinition::kHalV1:
      camera = new EmulatedFakeCamera(cameraId, is_back_facing,
                                      &HAL_MODULE_INFO_SYM.common);
      break;
    case cvd::CameraDefinition::kHalV2:
      camera = new EmulatedFakeCamera2(cameraId, is_back_facing,
                                       &HAL_MODULE_INFO_SYM.common);
      break;
    case cvd::CameraDefinition::kHalV3:
      camera = new EmulatedFakeCamera3(cameraId, is_back_facing,
                                       &HAL_MODULE_INFO_SYM.common);
      break;
    default:
      ALOGE("%s: Unsupported camera hal version requested: %d", __FUNCTION__,
            definition.hal_version);
      return NULL;
  }

  ALOGI("%s: Camera device %zu hal version is %d", __FUNCTION__, cameraId,
        definition.hal_version);
  int res = camera->Initialize(definition);

  if (res != NO_ERROR) {
    ALOGE("%s: Unable to intialize camera %zu: %s (%d)", __FUNCTION__, cameraId,
          strerror(-res), res);
    delete camera;
    return NULL;
  }

  ALOGI("%s: Inserting camera", __FUNCTION__);
  mEmulatedCameras.replaceAt(camera, cameraId);
  ALOGI("%s: Done", __FUNCTION__);
  return camera;
}

EmulatedCameraFactory::~EmulatedCameraFactory() {
  for (size_t n = 0; n < mEmulatedCameras.size(); n++) {
    if (mEmulatedCameras[n] != NULL) {
      delete mEmulatedCameras[n];
    }
  }

  if (mHotplugThread != NULL) {
    mHotplugThread->requestExit();
    mHotplugThread->join();
  }
}

/****************************************************************************
 * Camera HAL API handlers.
 *
 * Each handler simply verifies existence of an appropriate EmulatedBaseCamera
 * instance, and dispatches the call to that instance.
 *
 ***************************************************************************/

int EmulatedCameraFactory::cameraDeviceOpen(int camera_id,
                                            hw_device_t** device) {
  ALOGV("%s: id = %d", __FUNCTION__, camera_id);

  *device = NULL;

  EmulatedBaseCamera* camera = getOrCreateFakeCamera(camera_id);
  if (camera == NULL) return -EINVAL;

  return camera->connectCamera(device);
}

int EmulatedCameraFactory::getCameraInfo(int camera_id,
                                         struct camera_info* info) {
  ALOGV("%s: id = %d", __FUNCTION__, camera_id);

  EmulatedBaseCamera* camera = getOrCreateFakeCamera(camera_id);
  if (camera == NULL) return -EINVAL;

  return camera->getCameraInfo(info);
}

int EmulatedCameraFactory::setCallbacks(
    const camera_module_callbacks_t* callbacks) {
  ALOGV("%s: callbacks = %p", __FUNCTION__, callbacks);

  mCallbacks = callbacks;

  return OK;
}

void EmulatedCameraFactory::getVendorTagOps(vendor_tag_ops_t* ops) {
  ALOGV("%s: ops = %p", __FUNCTION__, ops);

  // No vendor tags defined for emulator yet, so not touching ops
}

int EmulatedCameraFactory::setTorchMode(const char* camera_id, bool enabled) {
  ALOGV("%s: camera_id = %s, enabled =%d", __FUNCTION__, camera_id, enabled);

  EmulatedBaseCamera* camera = getOrCreateFakeCamera(atoi(camera_id));
  if (camera == NULL) return -EINVAL;

  return camera->setTorchMode(enabled);
}

/****************************************************************************
 * Camera HAL API callbacks.
 ***************************************************************************/

int EmulatedCameraFactory::device_open(const hw_module_t* module,
                                       const char* name, hw_device_t** device) {
  /*
   * Simply verify the parameters, and dispatch the call inside the
   * EmulatedCameraFactory instance.
   */

  if (module != &HAL_MODULE_INFO_SYM.common) {
    ALOGE("%s: Invalid module %p expected %p", __FUNCTION__, module,
          &HAL_MODULE_INFO_SYM.common);
    return -EINVAL;
  }
  if (name == NULL) {
    ALOGE("%s: NULL name is not expected here", __FUNCTION__);
    return -EINVAL;
  }

  return EmulatedCameraFactory::Instance().cameraDeviceOpen(atoi(name), device);
}

int EmulatedCameraFactory::get_number_of_cameras(void) {
  return EmulatedCameraFactory::Instance().getEmulatedCameraNum();
}

int EmulatedCameraFactory::get_camera_info(int camera_id,
                                           struct camera_info* info) {
  return EmulatedCameraFactory::Instance().getCameraInfo(camera_id, info);
}

int EmulatedCameraFactory::set_callbacks(
    const camera_module_callbacks_t* callbacks) {
  return EmulatedCameraFactory::Instance().setCallbacks(callbacks);
}

void EmulatedCameraFactory::get_vendor_tag_ops(vendor_tag_ops_t* ops) {
  EmulatedCameraFactory::Instance().getVendorTagOps(ops);
}

int EmulatedCameraFactory::open_legacy(const struct hw_module_t* /*module*/,
                                       const char* /*id*/,
                                       uint32_t /*halVersion*/,
                                       struct hw_device_t** /*device*/) {
  // Not supporting legacy open
  return -ENOSYS;
}

int EmulatedCameraFactory::set_torch_mode(const char* camera_id, bool enabled) {
  return EmulatedCameraFactory::Instance().setTorchMode(camera_id, enabled);
}

/********************************************************************************
 * Internal API
 *******************************************************************************/

void EmulatedCameraFactory::onStatusChanged(int cameraId, int newStatus) {
  EmulatedBaseCamera* cam = getOrCreateFakeCamera(cameraId);
  if (!cam) {
    ALOGE("%s: Invalid camera ID %d", __FUNCTION__, cameraId);
    return;
  }

  /**
   * (Order is important)
   * Send the callback first to framework, THEN close the camera.
   */

  if (newStatus == cam->getHotplugStatus()) {
    ALOGW("%s: Ignoring transition to the same status", __FUNCTION__);
    return;
  }

  const camera_module_callbacks_t* cb = mCallbacks;
  if (cb != NULL && cb->camera_device_status_change != NULL) {
    cb->camera_device_status_change(cb, cameraId, newStatus);
  }

  if (newStatus == CAMERA_DEVICE_STATUS_NOT_PRESENT) {
    cam->unplugCamera();
  } else if (newStatus == CAMERA_DEVICE_STATUS_PRESENT) {
    cam->plugCamera();
  }
}

void EmulatedCameraFactory::onTorchModeStatusChanged(int cameraId,
                                                     int newStatus) {
  EmulatedBaseCamera* cam = getOrCreateFakeCamera(cameraId);
  if (!cam) {
    ALOGE("%s: Invalid camera ID %d", __FUNCTION__, cameraId);
    return;
  }

  const camera_module_callbacks_t* cb = mCallbacks;
  if (cb != NULL && cb->torch_mode_status_change != NULL) {
    char id[10];
    sprintf(id, "%d", cameraId);
    cb->torch_mode_status_change(cb, id, newStatus);
  }
}

/********************************************************************************
 * Initializer for the static member structure.
 *******************************************************************************/

/* Entry point for camera HAL API. */
struct hw_module_methods_t EmulatedCameraFactory::mCameraModuleMethods = {
    .open = EmulatedCameraFactory::device_open};

}; /* namespace android */
