/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include <android-base/logging.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <string>
#include <vector>

#include <dlfcn.h>

const static std::string kBtAudioProviderFactoryFunctionName =
    "createIBluetoothAudioProviderFactory";

const static std::string kBtAudioLibraryName =
    "android.hardware.bluetooth.audio-impl";

static bool registerExternalServiceImplementation(const std::string& libName,
                                                  const std::string& funcName) {
  dlerror();  // clear
  auto libPath = libName + ".so";
  void* handle = dlopen(libPath.c_str(), RTLD_LAZY);
  if (handle == nullptr) {
    const char* error = dlerror();
    LOG(ERROR) << "Failed to dlopen " << libPath << " " << error;
    return false;
  }
  binder_status_t (*factoryFunction)();
  *(void**)(&factoryFunction) = dlsym(handle, funcName.c_str());
  if (!factoryFunction) {
    const char* error = dlerror();
    LOG(ERROR) << "Factory function " << funcName << " not found in libName "
               << libPath << " " << error;
    dlclose(handle);
    return false;
  }
  return ((*factoryFunction)() == STATUS_OK);
}

int main() {
  LOG(INFO) << "Bluetooth HAL starting up";
  if (!ABinderProcess_setThreadPoolMaxThreadCount(1)) {
    LOG(INFO) << "failed to set thread pool max thread count";
    return 1;
  }
  ABinderProcess_startThreadPool();

  if (registerExternalServiceImplementation(
          kBtAudioLibraryName, kBtAudioProviderFactoryFunctionName)) {
    LOG(INFO) << kBtAudioProviderFactoryFunctionName << "() success from "
              << kBtAudioLibraryName;
  } else {
    LOG(WARNING) << kBtAudioProviderFactoryFunctionName << "() failed from "
                 << kBtAudioLibraryName;
  }

  ABinderProcess_joinThreadPool();
  return 0;
}
