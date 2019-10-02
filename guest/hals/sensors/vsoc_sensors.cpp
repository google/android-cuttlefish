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
#include <cstdint>

#include <cutils/properties.h>
#include <cutils/sockets.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/system_properties.h>
#include <unistd.h>

#include <algorithm>

#include "common/libs/fs/shared_select.h"
#include "common/libs/threads/thunkers.h"
#include "guest/hals/sensors/sensors_hal.h"
#include "guest/hals/sensors/vsoc_sensors.h"
#include "guest/hals/sensors/vsoc_sensors_message.h"
#include "guest/libs/remoter/remoter_framework_pkt.h"

using cvd::LockGuard;
using cvd::Mutex;
using cvd::time::Milliseconds;
using cvd::time::MonotonicTimePoint;
using cvd::time::Nanoseconds;

namespace cvd {

int GceSensors::total_sensor_count_ = -1;
SensorInfo* GceSensors::sensor_infos_ = NULL;
const int GceSensors::kInjectedEventWaitPeriods = 3;
const Nanoseconds GceSensors::kInjectedEventWaitTime =
    Nanoseconds(Milliseconds(20));

GceSensors::GceSensors()
  : sensors_poll_device_1(), deadline_change_(&sensor_state_lock_) {
  if (total_sensor_count_ == -1) {
    RegisterSensors();
  }

  // Create a pair of FDs that would be used to control the
  // receiver thread.
  if (control_sender_socket_->IsOpen() || control_receiver_socket_->IsOpen()) {
    ALOGE("%s: Receiver control FDs are opened", __FUNCTION__);
  }
  if (!cvd::SharedFD::Pipe(&control_receiver_socket_,
                           &control_sender_socket_)) {
    ALOGE("%s: Unable to create thread control FDs: %d -> %s", __FUNCTION__,
          errno, strerror(errno));
  }

  // Create the correct number of holding buffers for this client.
  sensor_states_.resize(total_sensor_count_);
  int i;
  for (i = 0; i < total_sensor_count_; i++) {
    sensor_states_[i] = new SensorState(sensor_infos_[i]);
  }
}

GceSensors::~GceSensors() {
  int i;
  for (i = 0; i < total_sensor_count_; i++) {
    delete sensor_states_[i];
  }
}

int GceSensors::GetSensorsList(struct sensors_module_t* /*module*/,
                               struct sensor_t const** list) {
  *list = sensor_infos_;
  return total_sensor_count_;
}

int GceSensors::SetOperationMode(unsigned int /* is_loopback_mode */) {
  return -EINVAL;
}

int GceSensors::Open(const struct hw_module_t* module, const char* name,
                     struct hw_device_t** device) {
  int status = -EINVAL;

  if (!strcmp(name, SENSORS_HARDWARE_POLL)) {
    // Create a new GceSensors object and set all the fields/functions
    // to their default values.
    GceSensors* rval = new GceSensors;

    rval->common.tag = HARDWARE_DEVICE_TAG;
    rval->common.version = VSOC_SENSOR_DEVICE_VERSION;
    rval->common.module = (struct hw_module_t*)module;
    rval->common.close = cvd::thunk<hw_device_t, &GceSensors::Close>;

    rval->poll = cvd::thunk<sensors_poll_device_t, &GceSensors::Poll>;
    rval->activate = cvd::thunk<sensors_poll_device_t, &GceSensors::Activate>;
    rval->setDelay = cvd::thunk<sensors_poll_device_t, &GceSensors::SetDelay>;

    rval->batch = cvd::thunk<sensors_poll_device_1, &GceSensors::Batch>;
    rval->flush = cvd::thunk<sensors_poll_device_1, &GceSensors::Flush>;
    rval->inject_sensor_data =
        cvd::thunk<sensors_poll_device_1, &GceSensors::InjectSensorData>;

    // Spawn a thread to listen for incoming data from the remoter.
    int err = pthread_create(
        &rval->receiver_thread_, NULL,
        cvd::thunk<void, &GceSensors::Receiver>,
        rval);
    if (err) {
      ALOGE("GceSensors::%s: Unable to start receiver thread (%s)",
            __FUNCTION__, strerror(err));
    }

    *device = &rval->common;
    status = 0;
  }
  return status;
}

int GceSensors::Close() {
  // Make certain the receiver thread wakes up.
  SensorControlMessage msg;
  msg.message_type = THREAD_STOP;
  SendControlMessage(msg);
  pthread_join(receiver_thread_, NULL);
  delete this;
  return 0;
}

int GceSensors::Activate(int handle, int enabled) {
  if (handle < 0 || handle >= total_sensor_count_) {
    ALOGE("GceSensors::%s: Bad handle %d", __FUNCTION__, handle);
    return -1;
  }

  {
    LockGuard<Mutex> guard(sensor_state_lock_);
    // Update the report deadline, if changed.
    if (enabled && !sensor_states_[handle]->enabled_) {
      sensor_states_[handle]->deadline_ =
          MonotonicTimePoint::Now() + sensor_states_[handle]->sampling_period_;
    } else if (!enabled && sensor_states_[handle]->enabled_) {
      sensor_states_[handle]->deadline_ = SensorState::kInfinity;
    }
    sensor_states_[handle]->enabled_ = enabled;
    UpdateDeadline();
  }

  D("sensor_activate(): handle %d, enabled %d", handle, enabled);
  if (!UpdateRemoterState(handle)) {
    ALOGE("Failed to notify remoter about new sensor enable/disable.");
  }
  return 0;
}

int GceSensors::SetDelay(int handle, int64_t sampling_period_ns) {
  if (handle < 0 || handle >= total_sensor_count_) {
    ALOGE("GceSensors::%s: Bad handle %d", __FUNCTION__, handle);
    return -1;
  }
  int64_t min_delay_ns = sensor_infos_[handle].minDelay * 1000;
  if (sampling_period_ns < min_delay_ns) {
    sampling_period_ns = min_delay_ns;
  }

  {
    LockGuard<Mutex> guard(sensor_state_lock_);
    sensor_states_[handle]->deadline_ -=
        sensor_states_[handle]->sampling_period_;
    sensor_states_[handle]->sampling_period_ = Nanoseconds(sampling_period_ns);
    sensor_states_[handle]->deadline_ +=
        sensor_states_[handle]->sampling_period_;
    // If our sampling period has decreased, our deadline
    // could have already passed. If so, report immediately, but not in the
    // past.
    MonotonicTimePoint now = MonotonicTimePoint::Now();
    if (sensor_states_[handle]->deadline_ < now) {
      sensor_states_[handle]->deadline_ = now;
    }
    UpdateDeadline();
  }

  D("sensor_set_delay(): handle %d, delay (ms) %" PRId64, handle,
    Milliseconds(Nanoseconds(sampling_period_ns)).count());
  if (!UpdateRemoterState(handle)) {
    ALOGE("Failed to notify remoter about new sensor delay.");
  }
  return 0;
}

int GceSensors::Poll(sensors_event_t* data, int count_unsafe) {
  if (count_unsafe <= 0) {
    ALOGE("Framework polled with bad count (%d)", count_unsafe);
    return -1;
  }
  size_t count = size_t(count_unsafe);

  // Poll will block until 1 of 2 things happens:
  //    1. The next deadline for some active sensor
  //        occurs.
  //    2. The next deadline changes (either because
  //        a sensor was activated/deactivated or its
  //        delay changed).
  // In both cases, any sensors whose report deadlines
  // have passed will report their data (or mock data),
  // and poll will either return (if at least one deadline
  // has passed), or repeat by blocking until the next deadline.
  LockGuard<Mutex> guard(sensor_state_lock_);
  current_deadline_ = UpdateDeadline();
  // Sleep until we have something to report
  while (!fifo_.size()) {
    deadline_change_.WaitUntil(current_deadline_);
    current_deadline_ = UpdateDeadline();
  }
  // Copy the events from the buffer
  int num_copied = std::min(fifo_.size(), count);
  FifoType::iterator first_uncopied = fifo_.begin() + num_copied;
  std::copy(fifo_.begin(), first_uncopied, data);
  fifo_.erase(fifo_.begin(), first_uncopied);
  D("Reported %d sensor events. First: %d %f %f %f", num_copied, data->sensor,
    data->data[0], data->data[1], data->data[2]);
  return num_copied;
}


void *GceSensors::Receiver() {
  // Initialize the server.
  sensor_listener_socket_ = cvd::SharedFD::SocketSeqPacketServer(
      gce_sensors_message::kSensorsHALSocketName, 0777);
  if (!sensor_listener_socket_->IsOpen()) {
    ALOGE("GceSensors::%s: Could not listen for sensor connections. (%s).",
          __FUNCTION__, sensor_listener_socket_->StrError());
    return NULL;
  }
  D("GceSensors::%s: Listening for sensor connections at %s", __FUNCTION__,
    gce_sensors_message::kSensorsHALSocketName);
  // Announce that we are ready for the remoter to connect.
  if (!NotifyRemoter()) {
    ALOGI("Failed to notify remoter that HAL is ready.");
  } else {
    ALOGI("Notified remoter that HAL is ready.");
  }

  typedef std::vector<cvd::SharedFD> FDVec;
  FDVec connected;
  // Listen for incoming sensor data and control messages
  // from the HAL.
  while (true) {
    cvd::SharedFDSet fds;
    for (FDVec::iterator it = connected.begin(); it != connected.end(); ++it) {
      fds.Set(*it);
    }
    fds.Set(control_receiver_socket_);
    // fds.Set(sensor_listener_socket_);
    int res = cvd::Select(&fds, NULL, NULL, NULL);
    if (res == -1) {
      ALOGE("%s: select returned %d and failed %d -> %s", __FUNCTION__, res,
            errno, strerror(errno));
      break;
    } else if (res == 0) {
      ALOGE("%s: select timed out", __FUNCTION__);
      break;
    } else if (fds.IsSet(sensor_listener_socket_)) {
      connected.push_back(cvd::SharedFD::Accept(*sensor_listener_socket_));
      ALOGI("GceSensors::%s: new client connected", __FUNCTION__);
    } else if (fds.IsSet(control_receiver_socket_)) {
      // We received a control message.
      SensorControlMessage msg;
      int res =
          control_receiver_socket_->Read(&msg, sizeof(SensorControlMessage));
      if (res == -1) {
        ALOGE("GceSensors::%s: Failed to receive control message.",
              __FUNCTION__);
      } else if (res == 0) {
        ALOGE("GceSensors::%s: Control connection closed.", __FUNCTION__);
      }
      if (msg.message_type == SENSOR_STATE_UPDATE) {
        // Forward the update to the remoter.
        remoter_request_packet packet;
        remoter_request_packet_init(&packet, kRemoterSensorState, 0);
        {
          LockGuard<Mutex> guard(sensor_state_lock_);
          packet.params.sensor_state_params.type =
              sensor_infos_[msg.sensor_handle].type;
          packet.params.sensor_state_params.enabled =
              sensor_states_[msg.sensor_handle]->enabled_;
          packet.params.sensor_state_params.delay_ns =
              sensor_states_[msg.sensor_handle]->sampling_period_.count();
          packet.params.sensor_state_params.handle = msg.sensor_handle;
        }
        struct msghdr msg;
        iovec msg_iov[1];
        msg_iov[0].iov_base = &packet;
        msg_iov[0].iov_len = sizeof(remoter_request_packet);
        msg.msg_name = NULL;
        msg.msg_namelen = 0;
        msg.msg_iov = msg_iov;
        msg.msg_iovlen = arraysize(msg_iov);
        msg.msg_control = NULL;
        msg.msg_controllen = 0;
        msg.msg_flags = 0;

        for (FDVec::iterator it = connected.begin(); it != connected.end();
             ++it) {
          cvd::SharedFD &fd = *it;
          if (fd->SendMsg(&msg, 0) == -1) {
            ALOGE("GceSensors::%s. Could not send sensor state (%s).",
                  __FUNCTION__, fd->StrError());
          }
        }
      }
      if (msg.message_type == THREAD_STOP) {
        D("Received terminate control message.");
        return NULL;
      }
    } else {
      for (FDVec::iterator it = connected.begin(); it != connected.end();
           ++it) {
        cvd::SharedFD &fd = *it;
        if (fds.IsSet(fd)) {
          // We received a sensor update from remoter.
          sensors_event_t event;
          struct msghdr msg;
          iovec msg_iov[1];
          msg_iov[0].iov_base = &event;
          msg_iov[0].iov_len = sizeof(event);
          msg.msg_name = NULL;
          msg.msg_namelen = 0;
          msg.msg_iov = msg_iov;
          msg.msg_iovlen = arraysize(msg_iov);
          msg.msg_control = NULL;
          msg.msg_controllen = 0;
          msg.msg_flags = 0;
          int res = fd->RecvMsg(&msg, 0);
          if (res <= 0) {
            if (res == 0) {
              ALOGE("GceSensors::%s: Sensors HAL connection closed.",
                    __FUNCTION__);
            } else {
              ALOGE("GceSensors::%s: Failed to receive sensor message",
                    __FUNCTION__);
            }
            connected.erase(std::find(connected.begin(), connected.end(), fd));
            break;
          }

          // We received an event from the remoter.
          if (event.sensor < 0 || event.sensor >= total_sensor_count_) {
            ALOGE("Remoter sent us an invalid sensor event! (handle %d)",
                  event.sensor);
            connected.erase(std::find(connected.begin(), connected.end(), fd));
            break;
          }

          D("Received sensor event: %d %f %f %f", event.sensor, event.data[0],
            event.data[1], event.data[2]);

          {
            LockGuard<Mutex> guard(sensor_state_lock_);
            // Increase the delay so that the HAL knows
            // it shouldn't report on its own for a while.
            SensorState *holding_buffer = sensor_states_[event.sensor];
            int wait_periods =
                std::max(kInjectedEventWaitPeriods,
                         (int)(kInjectedEventWaitTime.count() /
                               holding_buffer->sampling_period_.count()));
            holding_buffer->deadline_ =
                MonotonicTimePoint::Now() +
                holding_buffer->sampling_period_ * wait_periods;
            holding_buffer->event_.data[0] = event.data[0];
            holding_buffer->event_.data[1] = event.data[1];
            holding_buffer->event_.data[2] = event.data[2];
            // Signal the HAL to report the newly arrived event.
            fifo_.push_back(event);
            deadline_change_.NotifyOne();
          }
        }
      }
    }
  }
  return NULL;
}

bool GceSensors::NotifyRemoter() {
  remoter_request_packet packet;
  remoter_request_packet_init(&packet, kRemoterHALReady, 0);
  packet.send_response = 0;
  strncpy(packet.params.hal_ready_params.unix_socket,
          gce_sensors_message::kSensorsHALSocketName,
          sizeof(packet.params.hal_ready_params.unix_socket));
  AutoCloseFileDescriptor remoter_socket(remoter_connect());
  if (remoter_socket.IsError()) {
    D("GceSensors::%s: Could not connect to remoter to notify ready (%s).",
      __FUNCTION__, strerror(errno));
    return false;
  }
  int err =
      remoter_do_single_request_with_socket(remoter_socket, &packet, NULL);
  if (err == -1) {
    D("GceSensors::%s: Notify remoter ready: Failed after connect (%s).",
      __FUNCTION__, strerror(errno));
    return false;
  }
  D("GceSensors::%s: Notify remoter ready Succeeded.", __FUNCTION__);
  return true;
}

static bool CompareTimestamps(const sensors_event_t& a,
                              const sensors_event_t& b) {
  return a.timestamp < b.timestamp;
}

MonotonicTimePoint GceSensors::UpdateDeadline() {
  // Get the minimum of all the current deadlines.
  MonotonicTimePoint now = MonotonicTimePoint::Now();
  MonotonicTimePoint min = SensorState::kInfinity;
  int i = 0;
  bool sort_fifo = false;

  for (i = 0; i < total_sensor_count_; i++) {
    SensorState* holding_buffer = sensor_states_[i];
    // Ignore disabled sensors.
    if (!holding_buffer->enabled_) {
      continue;
    }
    while (holding_buffer->deadline_ < now) {
      sensors_event_t data = holding_buffer->event_;
      data.timestamp = holding_buffer->deadline_.SinceEpoch().count();
      fifo_.push_back(data);
      holding_buffer->deadline_ += holding_buffer->sampling_period_;
      sort_fifo = true;
    }
    // Now check if we should update the wake time based on the next event
    // from this sensor.
    if (sensor_states_[i]->deadline_ < min) {
      min = sensor_states_[i]->deadline_;
    }
  }
  // We added one or more sensor readings, so do a sort.
  // This is likely to be cheaper than a traditional priority queue because
  // a priority queue would try to keep its state correct for each addition.
  if (sort_fifo) {
    std::sort(fifo_.begin(), fifo_.end(), CompareTimestamps);
  }
  // If we added events or the deadline is lower notify the thread in Poll().
  // If the deadline went up, don't do anything.
  if (fifo_.size() || (min < current_deadline_)) {
    deadline_change_.NotifyOne();
  }
  return min;
}

bool GceSensors::UpdateRemoterState(int handle) {
  SensorControlMessage msg;
  msg.message_type = SENSOR_STATE_UPDATE;
  msg.sensor_handle = handle;
  return SendControlMessage(msg);
}

bool GceSensors::SendControlMessage(SensorControlMessage msg) {
  if (!control_sender_socket_->IsOpen()) {
    ALOGE("%s: Can't send control message %d, control socket not open.",
          __FUNCTION__, msg.message_type);
    return false;
  }
  if (control_sender_socket_->Write(&msg, sizeof(SensorControlMessage)) == -1) {
    ALOGE("GceSensors::%s. Could not send control message %d (%s).",
          __FUNCTION__, msg.message_type, control_sender_socket_->StrError());
    return false;
  }
  return true;
}

int GceSensors::RegisterSensors() {
  if (total_sensor_count_ != -1) {
    return -1;
  }
  total_sensor_count_ = 9;
  sensor_infos_ = new SensorInfo[total_sensor_count_];
  sensor_infos_[sensors_constants::kAccelerometerHandle] =
      AccelerometerSensor();
  sensor_infos_[sensors_constants::kGyroscopeHandle] = GyroscopeSensor();
  sensor_infos_[sensors_constants::kLightHandle] = LightSensor();
  sensor_infos_[sensors_constants::kMagneticFieldHandle] =
      MagneticFieldSensor();
  sensor_infos_[sensors_constants::kPressureHandle] = PressureSensor();
  sensor_infos_[sensors_constants::kProximityHandle] = ProximitySensor();
  sensor_infos_[sensors_constants::kAmbientTempHandle] = AmbientTempSensor();
  sensor_infos_[sensors_constants::kDeviceTempHandle] = DeviceTempSensor();
  sensor_infos_[sensors_constants::kRelativeHumidityHandle] =
      RelativeHumiditySensor();
  int i;
  for (i = 0; i < total_sensor_count_; i++) {
    D("Found sensor %s with handle %d", sensor_infos_[i].name,
      sensor_infos_[i].handle);
  }
  return total_sensor_count_;
}

}  // namespace cvd
