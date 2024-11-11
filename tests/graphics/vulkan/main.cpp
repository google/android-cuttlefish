// Copyright (C) 2024 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <cstdlib>
#include <memory>

#include <android_native_app_glue.h>
#include <assert.h>

#include "common.h"
#include "sample_base.h"

namespace cuttlefish {

struct AppState {
  bool drawing = false;
  std::unique_ptr<SampleBase> sample;
};

static void OnAppCmd(struct android_app* app, int32_t cmd) {
  auto* state = reinterpret_cast<AppState*>(app->userData);

  switch (cmd) {
    case APP_CMD_START: {
      ALOGD("APP_CMD_START");
      if (app->window != nullptr) {
        state->drawing = true;
        VK_ASSERT(state->sample->SetWindow(app->window));
      }
      break;
    }
    case APP_CMD_INIT_WINDOW: {
      ALOGD("APP_CMD_INIT_WINDOW");
      if (app->window != nullptr) {
        state->drawing = true;
        VK_ASSERT(state->sample->SetWindow(app->window));
      }
      break;
    }
    case APP_CMD_TERM_WINDOW: {
      ALOGD("APP_CMD_TERM_WINDOW");
      state->drawing = false;
      VK_ASSERT(state->sample->SetWindow(nullptr));
      break;
    }
    case APP_CMD_DESTROY: {
      ALOGD("APP_CMD_DESTROY");
      state->drawing = false;
      break;
    }
    default:
      break;
  }
}

void Main(struct android_app* app) {
  AppState state;
  state.sample = VK_ASSERT(BuildVulkanSampleApp());

  app->userData = &state;

  // Invoked from the source->process() below:
  app->onAppCmd = OnAppCmd;

  while (true) {
    int ident;
    android_poll_source* source;
    while ((ident = ALooper_pollOnce(state.drawing ? 0 : -1, nullptr, nullptr,
                                     (void**)&source)) > ALOOPER_POLL_TIMEOUT) {
      if (source != nullptr) {
        source->process(app, source);
      }
      if (app->destroyRequested != 0) {
        break;
      }
    }

    if (app->destroyRequested != 0) {
      ANativeActivity_finish(app->activity);
      break;
    }

    if (state.drawing) {
      VK_ASSERT(state.sample->Render());
    }
  }

  state.sample->CleanUp();
  state.sample.reset();
}

}  // namespace cuttlefish

void android_main(struct android_app* app) { cuttlefish::Main(app); }
