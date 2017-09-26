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
#ifndef DEVICE_GOOGLE_GCE_GPS_GPS_THREAD_H_
#define DEVICE_GOOGLE_GCE_GPS_GPS_THREAD_H_

#include <errno.h>
#include <fcntl.h>
#include <sys/epoll.h>

#include <hardware/gps.h>

#define LOG_TAG  "GceGps"
#define GPS_DEBUG  0
#define GPS_DATA_BUFFER_MAX_SIZE  256

#define DEFAULT_GPS_LOCATION_FLAG (GPS_LOCATION_HAS_LAT_LONG | \
                                   GPS_LOCATION_HAS_ALTITUDE | \
                                   GPS_LOCATION_HAS_BEARING | \
                                   GPS_LOCATION_HAS_SPEED | \
                                   GPS_LOCATION_HAS_ACCURACY)

#if GPS_DEBUG
#  define D(...) ALOGD(__VA_ARGS__)
#else
#  define D(...) ((void)0)
#endif

// Control commands to GPS thread
enum {
  CMD_QUIT = 0,
  CMD_START = 1,
  CMD_STOP = 2
};

// GPS HAL's state
typedef struct {
    int init;
    int fd;
    int control[2];
    pthread_t thread;
    GpsCallbacks callbacks;
} GpsState;

typedef struct {
  GpsLocation fix;
  gps_location_callback callback;
  char buffer[GPS_DATA_BUFFER_MAX_SIZE+1];
  int index;
} GpsDataReader;

void gps_state_thread(void* arg);


static inline int epoll_register(int epoll_fd, int fd) {
  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);

  struct epoll_event ev;
  ev.events = EPOLLIN;
  ev.data.fd = fd;

  int ret;
  do {
    ret = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev);
  } while (ret < 0 && errno == EINTR);
  return ret;
}


static inline int epoll_deregister(int  epoll_fd, int  fd) {
  int ret;
  do {
    ret = epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
  } while (ret < 0 && errno == EINTR);
  return ret;
}

#endif

