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
#pragma once

#include <vector>

#include "common/libs/threads/cuttlefish_thread.h"
#include "common/libs/fs/shared_fd.h"
#include "guest/hals/sensors/sensors.h"
#include "guest/hals/sensors/sensors_hal.h"

namespace cvd {

// Used for sending control messages to the receiver thread.
// The sensor_handle field may be left unused if it is not needed.
enum ControlMessageType {
  THREAD_STOP,
  SENSOR_STATE_UPDATE
};
typedef struct {
  ControlMessageType message_type;
  uint8_t sensor_handle;
} SensorControlMessage;

#if VSOC_SENSORS_DEVICE_API_VERSION_ATLEAST(1_0)
// Last updated to HAL 1.4
// Version history:
//   Before jb, jb-mr1 SENSORS_DEVICE_API_VERSION_0_1 (no version in sensors.h)
//   jb-mr2: SENSORS_DEVICE_API_VERSION_1_0
//   k: SENSORS_DEVICE_API_VERSION_1_1
//   l, l-mr1: SENSORS_DEVICE_API_VERSION_1_3
//   m, n, n-mr1: SENSORS_DEVICE_API_VERSION_1_4
#else
// Pre-1.0 sensors do not define the sensors_poll_device_1 type.
typedef sensors_poll_device_t sensors_poll_device_1;
#endif

class GceSensors : public sensors_poll_device_1 {
 public:
  GceSensors();
  ~GceSensors();

  /**
   ** SENSOR HAL API FUNCTIONS FOR MODULE
   **/

  // Gets a list of all supported sensors and stores in list.
  // Returns the number of supported sensors.
  static int GetSensorsList(struct sensors_module_t* module,
    struct sensor_t const** list);

  // Place the module in a specific mode. The following modes are defined
  //
  //  0 - Normal operation. Default state of the module.
  //  1 - Loopback mode. Data is injected for the supported
  //      sensors by the sensor service in this mode.
  // @return 0 on success
  //         -EINVAL if requested mode is not supported
  //         -EPERM if operation is not allowed
  static int SetOperationMode(unsigned int mode);


  /**
   ** SENSOR HAL API FUNCTIONS FOR DEVICE
   **/
  // Opens the device.
  static int Open(const struct hw_module_t* module, const char* name,
    struct hw_device_t** device);

  // Closes the device, closing all sensors.
  int Close();

  // Activate (or deactivate) the sensor with the given handle.
  //
  // One-shot sensors deactivate themselves automatically upon receiving an
  // event, and they must still accept to be deactivated through a call to
  // activate(..., enabled=0).
  // Non-wake-up sensors never prevent the SoC from going into suspend mode;
  // that is, the HAL shall not hold a partial wake-lock on behalf of
  // applications.
  //
  // If enabled is 1 and the sensor is already activated, this function is a
  // no-op and succeeds.
  //
  // If enabled is 0 and the sensor is already deactivated, this function is a
  // no-op and succeeds.
  //
  // This function returns 0 on success and a negative error number otherwise.
  int Activate(int handle, int enabled);

  // Sets the delay (in ns) for the sensor with the given handle.
  // Deprecated as of HAL 1.1
  // Called after activate()
  int SetDelay(int handle, int64_t sampling_period_ns);

  // Returns an array of sensor data by filling the data argument.
  // This function must block until events are available. It will return
  // the number of events read on success, or a negative number in case of
  // an error.
  int Poll(sensors_event_t* data, int count);

#if VSOC_SENSORS_DEVICE_API_VERSION_ATLEAST(1_0)
  // Sets a sensor’s parameters, including sampling frequency and maximum
  // report latency. This function can be called while the sensor is
  // activated, in which case it must not cause any sensor measurements to
  // be lost: transitioning from one sampling rate to the other cannot cause
  // lost events, nor can transitioning from a high maximum report latency to
  // a low maximum report latency.
  //
  // Before SENSORS_DEVICE_API_VERSION_1_3, flags included:
  //   SENSORS_BATCH_DRY_RUN
  //   SENSORS_BATCH_WAKE_UPON_FIFO_FULL
  //
  // After SENSORS_DEVICE_API_VERSION_1_3 see WAKE_UPON_FIFO_FULL
  // in sensor_t.flags
  int Batch(int sensor_handle, int flags, int64_t sampling_period_ns,
            int64_t max_report_latency_ns) {
    // TODO: Add support for maximum report latency with max_report_latency_ns.
    return SetDelay(sensor_handle, sampling_period_ns);
  }
#endif

#if VSOC_SENSORS_DEVICE_API_VERSION_ATLEAST(1_1)
  // Adds a META_DATA_FLUSH_COMPLETE event (sensors_event_meta_data_t)
  // to the end of the "batch mode" FIFO for the specified sensor and flushes
  // the FIFO.
  //
  // If the FIFO is empty or if the sensor doesn't support batching (FIFO
  // size zero), it should return SUCCESS along with a trivial
  // META_DATA_FLUSH_COMPLETE event added to the event stream. This applies to
  // all sensors other than one-shot sensors.
  //
  // If the sensor is a one-shot sensor, flush must return -EINVAL and not
  // generate any flush complete metadata.
  //
  // If the sensor is not active at the time flush() is called, flush() should
  // return -EINVAL.
  int Flush(int sensor_handle) {
    return -EINVAL;
  }
#endif

#if VSOC_SENSORS_DEVICE_API_VERSION_ATLEAST(1_4)
  // Inject a single sensor sample to be to this device.
  // data points to the sensor event to be injected
  // @return 0 on success
  //  -EPERM if operation is not allowed
  //  -EINVAL if sensor event cannot be injected
  int InjectSensorData(const sensors_event_t *data) {
    return -EINVAL;
  }
#endif

 private:
  typedef std::vector<SensorState*> SensorStateVector;
  typedef std::vector<sensors_event_t> FifoType;
  // Total number of sensors supported by this HAL.
  static int total_sensor_count_;
  // Vector of static sensor information for sensors supported by this HAL.
  // Indexed by the handle. Length must always be equal to total_sensor_count_.
  static SensorInfo* sensor_infos_;
  // Vector of sensor state information, indexed by the handle.
  // Assumption here is that the sensor handles will start at 0 and be
  // contiguous up to the number of supported sensors.
  SensorStateVector sensor_states_;
  // Keep track of the time when the thread in Poll() is scheduled to wake.
  cvd::time::MonotonicTimePoint current_deadline_;

  // Ordered set of sensor values.
  // TODO(ghartman): Simulate FIFO overflow.
  FifoType fifo_;
  // Thread to handle new connections.
  pthread_t receiver_thread_;
  // Socket to receive sensor events on.
  cvd::SharedFD sensor_listener_socket_;
  // Socket for listener thread to receive control messages.
  cvd::SharedFD control_receiver_socket_;
  // Socket to send control messages to listener thread.
  cvd::SharedFD control_sender_socket_;

  // Lock to protect shared state, including
  // sensor_states_ and next_deadline_.
  // Associated with deadline_change_ condition variable.
  cvd::Mutex sensor_state_lock_;
  // Condition variable to signal changes in the deadline.
  cvd::ConditionVariable deadline_change_;

  // When events are arriving from a client, we report only
  // when they arrive, rather than at a fixed cycle. After not
  // receiving a real event for both a given number of periods
  // and a given time period, we will give up and resume
  // sending mock events.
  const static int kInjectedEventWaitPeriods;
  const static cvd::time::Nanoseconds kInjectedEventWaitTime;

  /**
   ** UTILITY FUNCTIONS
   **/

  // Receive data from remoter.
  void* Receiver();

  // Notifies the remoter that the HAL is awake and ready.
  inline bool NotifyRemoter();

  // Looks through all active sensor deadlines, and finds the one that
  // is coming up next. If this is not next_deadline_, then the deadline
  // has changed. Update it and signal the Poll thread.
  // This should be called anytime the next deadline may have changed.
  // Can only be called while holding sensor_state_lock_.
  // Returns true if the deadline has changed.
  cvd::time::MonotonicTimePoint UpdateDeadline();

  // Sends an update for the sensor with the given handle to the remoter.
  // Update will be enqueued for receiver, not send immediately.
  inline bool UpdateRemoterState(int handle);

  // Sends a control event to the listener.
  inline bool SendControlMessage(SensorControlMessage msg);

  // Populates the list of static sensor info. Returns the number
  // of sensors supported. Should only be called once.
  static inline int RegisterSensors();

};

} //namespace cvd

