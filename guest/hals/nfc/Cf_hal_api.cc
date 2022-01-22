/*
 * Copyright (C) 2021 The Android Open Source Project
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
#include <android-base/properties.h>
#include <android-base/stringprintf.h>
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <string.h>

#include "Cf_hal_api.h"
#include "hardware_nfc.h"

using android::base::StringPrintf;

bool hal_opened = false;
bool dbg_logging = false;
pthread_mutex_t hmutex = PTHREAD_MUTEX_INITIALIZER;
nfc_stack_callback_t* e_cback;
nfc_stack_data_callback_t* d_cback;

static struct aidl_callback_struct {
  pthread_mutex_t mutex;
  pthread_cond_t cond;
  pthread_t thr;
  int event_pending;
  int stop_thread;
  int thread_running;
  nfc_event_t event;
  nfc_status_t event_status;
} aidl_callback_data;

static void* aidl_callback_thread_fct(void* arg) {
  int ret;
  struct aidl_callback_struct* pcb_data = (struct aidl_callback_struct*)arg;

  ret = pthread_mutex_lock(&pcb_data->mutex);
  if (ret != 0) {
    LOG(ERROR) << StringPrintf("%s pthread_mutex_lock failed", __func__);
    goto error;
  }

  do {
    if (pcb_data->event_pending == 0) {
      ret = pthread_cond_wait(&pcb_data->cond, &pcb_data->mutex);
      if (ret != 0) {
        LOG(ERROR) << StringPrintf("%s pthread_cond_wait failed", __func__);
        break;
      }
    }

    if (pcb_data->event_pending) {
      nfc_event_t event = pcb_data->event;
      nfc_status_t event_status = pcb_data->event_status;
      int ending = pcb_data->stop_thread;
      pcb_data->event_pending = 0;
      ret = pthread_cond_signal(&pcb_data->cond);
      if (ret != 0) {
        LOG(ERROR) << StringPrintf("%s pthread_cond_signal failed", __func__);
        break;
      }
      if (ending) {
        pcb_data->thread_running = 0;
      }
      ret = pthread_mutex_unlock(&pcb_data->mutex);
      if (ret != 0) {
        LOG(ERROR) << StringPrintf("%s pthread_mutex_unlock failed", __func__);
      }
      LOG(INFO) << StringPrintf("%s event %hhx status %hhx", __func__, event,
                                event_status);
      e_cback(event, event_status);
      usleep(50000);
      if (ending) {
        return NULL;
      }
      ret = pthread_mutex_lock(&pcb_data->mutex);
      if (ret != 0) {
        LOG(ERROR) << StringPrintf("%s pthread_mutex_lock failed", __func__);
        goto error;
      }
    }
  } while (pcb_data->stop_thread == 0 || pcb_data->event_pending);

  ret = pthread_mutex_unlock(&pcb_data->mutex);
  if (ret != 0) {
    LOG(ERROR) << StringPrintf("%s pthread_mutex_unlock failed", __func__);
  }

error:
  pcb_data->thread_running = 0;
  return NULL;
}

static int aidl_callback_thread_start() {
  int ret;
  LOG(INFO) << StringPrintf("%s", __func__);

  memset(&aidl_callback_data, 0, sizeof(aidl_callback_data));

  ret = pthread_mutex_init(&aidl_callback_data.mutex, NULL);
  if (ret != 0) {
    LOG(ERROR) << StringPrintf("%s pthread_mutex_init failed", __func__);
    return ret;
  }

  ret = pthread_cond_init(&aidl_callback_data.cond, NULL);
  if (ret != 0) {
    LOG(ERROR) << StringPrintf("%s pthread_cond_init failed", __func__);
    return ret;
  }

  aidl_callback_data.thread_running = 1;

  ret = pthread_create(&aidl_callback_data.thr, NULL, aidl_callback_thread_fct,
                       &aidl_callback_data);
  if (ret != 0) {
    LOG(ERROR) << StringPrintf("%s pthread_create failed", __func__);
    aidl_callback_data.thread_running = 0;
    return ret;
  }

  return 0;
}

static int aidl_callback_thread_end() {
  LOG(INFO) << StringPrintf("%s", __func__);
  if (aidl_callback_data.thread_running != 0) {
    int ret;

    ret = pthread_mutex_lock(&aidl_callback_data.mutex);
    if (ret != 0) {
      LOG(ERROR) << StringPrintf("%s pthread_mutex_lock failed", __func__);
      return ret;
    }

    aidl_callback_data.stop_thread = 1;

    // Wait for the thread to have no event pending
    while (aidl_callback_data.thread_running &&
           aidl_callback_data.event_pending) {
      ret = pthread_cond_signal(&aidl_callback_data.cond);
      if (ret != 0) {
        LOG(ERROR) << StringPrintf("%s pthread_cond_signal failed", __func__);
        return ret;
      }
      ret = pthread_cond_wait(&aidl_callback_data.cond,
                              &aidl_callback_data.mutex);
      if (ret != 0) {
        LOG(ERROR) << StringPrintf("%s pthread_cond_wait failed", __func__);
        break;
      }
    }

    ret = pthread_mutex_unlock(&aidl_callback_data.mutex);
    if (ret != 0) {
      LOG(ERROR) << StringPrintf("%s pthread_mutex_unlock failed", __func__);
      return ret;
    }

    ret = pthread_cond_signal(&aidl_callback_data.cond);
    if (ret != 0) {
      LOG(ERROR) << StringPrintf("%s pthread_cond_signal failed", __func__);
      return ret;
    }

    ret = pthread_detach(aidl_callback_data.thr);
    if (ret != 0) {
      LOG(ERROR) << StringPrintf("%s pthread_detach failed", __func__);
      return ret;
    }
  }
  return 0;
}

static void aidl_callback_post(nfc_event_t event, nfc_status_t event_status) {
  int ret;

  if (pthread_equal(pthread_self(), aidl_callback_data.thr)) {
    e_cback(event, event_status);
  }

  ret = pthread_mutex_lock(&aidl_callback_data.mutex);
  if (ret != 0) {
    LOG(ERROR) << StringPrintf("%s pthread_mutex_lock failed", __func__);
    return;
  }

  if (aidl_callback_data.thread_running == 0) {
    (void)pthread_mutex_unlock(&aidl_callback_data.mutex);
    LOG(ERROR) << StringPrintf("%s thread is not running", __func__);
    e_cback(event, event_status);
    return;
  }

  while (aidl_callback_data.event_pending) {
    ret =
        pthread_cond_wait(&aidl_callback_data.cond, &aidl_callback_data.mutex);
    if (ret != 0) {
      LOG(ERROR) << StringPrintf("%s pthread_cond_wait failed", __func__);
      return;
    }
  }

  aidl_callback_data.event_pending = 1;
  aidl_callback_data.event = event;
  aidl_callback_data.event_status = event_status;

  ret = pthread_mutex_unlock(&aidl_callback_data.mutex);
  if (ret != 0) {
    LOG(ERROR) << StringPrintf("%s pthread_mutex_unlock failed", __func__);
    return;
  }

  ret = pthread_cond_signal(&aidl_callback_data.cond);
  if (ret != 0) {
    LOG(ERROR) << StringPrintf("%s pthread_cond_signal failed", __func__);
    return;
  }
}

int Cf_hal_open(nfc_stack_callback_t* p_cback,
                nfc_stack_data_callback_t* p_data_cback) {
  LOG(INFO) << StringPrintf("%s", __func__);
  pthread_mutex_lock(&hmutex);
  if (hal_opened) {
    // already opened, close then open again
    LOG(INFO) << StringPrintf("%s close and open again", __func__);
    if (aidl_callback_data.thread_running && aidl_callback_thread_end() != 0) {
      pthread_mutex_unlock(&hmutex);
      return -1;
    }
    hal_opened = false;
  }
  e_cback = p_cback;
  d_cback = p_data_cback;
  if ((hal_opened || !aidl_callback_data.thread_running) &&
      (aidl_callback_thread_start() != 0)) {
    // status failed
    LOG(INFO) << StringPrintf("%s failed", __func__);
    aidl_callback_post(HAL_NFC_OPEN_CPLT_EVT, HAL_NFC_STATUS_FAILED);
    pthread_mutex_unlock(&hmutex);
    return -1;
  }
  hal_opened = true;
  aidl_callback_post(HAL_NFC_OPEN_CPLT_EVT, HAL_NFC_STATUS_OK);
  pthread_mutex_unlock(&hmutex);
  return 0;
}

int Cf_hal_write(uint16_t data_len, const uint8_t* p_data) {
  if (!hal_opened) return -1;
  // TODO: write NCI state machine
  (void)data_len;
  (void)p_data;
  return 0;
}

int Cf_hal_core_initialized() {
  if (!hal_opened) return -1;
  pthread_mutex_lock(&hmutex);
  aidl_callback_post(HAL_NFC_POST_INIT_CPLT_EVT, HAL_NFC_STATUS_OK);
  pthread_mutex_unlock(&hmutex);
  return 0;
}

int Cf_hal_pre_discover() {
  if (!hal_opened) return -1;
  pthread_mutex_lock(&hmutex);
  aidl_callback_post(HAL_NFC_PRE_DISCOVER_CPLT_EVT, HAL_NFC_STATUS_OK);
  pthread_mutex_unlock(&hmutex);
  return 0;
}

int Cf_hal_close() {
  LOG(INFO) << StringPrintf("%s", __func__);
  if (!hal_opened) return -1;
  pthread_mutex_lock(&hmutex);
  hal_opened = false;
  aidl_callback_post(HAL_NFC_CLOSE_CPLT_EVT, HAL_NFC_STATUS_OK);
  if (aidl_callback_data.thread_running && aidl_callback_thread_end() != 0) {
    LOG(ERROR) << StringPrintf("%s thread end failed", __func__);
    pthread_mutex_unlock(&hmutex);
    return -1;
  }
  pthread_mutex_unlock(&hmutex);
  return 0;
}

int Cf_hal_close_off() {
  LOG(INFO) << StringPrintf("%s", __func__);
  if (!hal_opened) return -1;
  pthread_mutex_lock(&hmutex);
  hal_opened = false;
  aidl_callback_post(HAL_NFC_CLOSE_CPLT_EVT, HAL_NFC_STATUS_OK);
  if (aidl_callback_data.thread_running && aidl_callback_thread_end() != 0) {
    LOG(ERROR) << StringPrintf("%s thread end failed", __func__);
    pthread_mutex_unlock(&hmutex);
    return -1;
  }
  pthread_mutex_unlock(&hmutex);
  return 0;
}

int Cf_hal_power_cycle() {
  if (!hal_opened) return -1;
  pthread_mutex_lock(&hmutex);
  aidl_callback_post(HAL_NFC_OPEN_CPLT_EVT, HAL_NFC_STATUS_OK);
  pthread_mutex_unlock(&hmutex);
  return 0;
}

void Cf_hal_factoryReset() {}
void Cf_hal_getConfig(NfcConfig& config) {
  // TODO: read config from /vendor/etc/libnfc-hal-cf.conf
  memset(&config, 0x00, sizeof(NfcConfig));
  config.nfaPollBailOutMode = 1;
  config.maxIsoDepTransceiveLength = 0xFEFF;
  config.defaultOffHostRoute = 0x81;
  config.defaultOffHostRouteFelica = 0x81;
  config.defaultSystemCodeRoute = 0x00;
  config.defaultSystemCodePowerState = 0x3B;
  config.defaultRoute = 0x00;
  config.offHostRouteUicc.resize(1);
  config.offHostRouteUicc[0] = 0x81;
  config.offHostRouteEse.resize(1);
  config.offHostRouteEse[0] = 0x81;
  config.defaultIsoDepRoute = 0x81;
}

void Cf_hal_setVerboseLogging(bool enable) { dbg_logging = enable; }

bool Cf_hal_getVerboseLogging() { return dbg_logging; }
