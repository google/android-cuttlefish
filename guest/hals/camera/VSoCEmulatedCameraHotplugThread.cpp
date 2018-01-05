/*
 * Copyright (C) 2017 The Android Open Source Project
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
#define LOG_TAG "EmulatedCamera_HotplugThread"
#include <cutils/log.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "EmulatedCameraFactory.h"
#include "EmulatedCameraHotplugThread.h"

#define SubscriberInfo EmulatedCameraHotplugThread::SubscriberInfo

namespace android {

EmulatedCameraHotplugThread::EmulatedCameraHotplugThread(
    size_t totalCameraCount)
    : Thread(/*canCallJava*/ false) {}

EmulatedCameraHotplugThread::~EmulatedCameraHotplugThread() {}

status_t EmulatedCameraHotplugThread::requestExitAndWait() {
  ALOGE("%s: Not implemented. Use requestExit + join instead", __FUNCTION__);
  return INVALID_OPERATION;
}

void EmulatedCameraHotplugThread::requestExit() {
  ALOGV("%s: Requesting thread exit", __FUNCTION__);
  mRunning = false;
}

status_t EmulatedCameraHotplugThread::readyToRun() { return OK; }

bool EmulatedCameraHotplugThread::threadLoop() {
  // Thread is irrelevant right now; hoplug is not supported.
  return false;
}

String8 EmulatedCameraHotplugThread::getFilePath(int cameraId) const {
  return String8();
}

bool EmulatedCameraHotplugThread::createFileIfNotExists(int cameraId) const {
  return true;
}

int EmulatedCameraHotplugThread::getCameraId(String8 filePath) const {
  // Not used anywhere.
  return NAME_NOT_FOUND;
}

int EmulatedCameraHotplugThread::getCameraId(int wd) const {
  // Not used anywhere.
  return NAME_NOT_FOUND;
}

SubscriberInfo* EmulatedCameraHotplugThread::getSubscriberInfo(int cameraId) {
  // Not used anywhere.
  return NULL;
}

bool EmulatedCameraHotplugThread::addWatch(int cameraId) { return true; }

bool EmulatedCameraHotplugThread::removeWatch(int cameraId) { return true; }

int EmulatedCameraHotplugThread::readFile(String8 filePath) const { return 1; }

}  // namespace android
