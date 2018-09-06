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
#include "guest/hals/gps/gps_thread.h"

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <sys/epoll.h>
#include <time.h>
#include <unistd.h>

#include <log/log.h>
#include <cutils/sockets.h>
#include <hardware/gps.h>

// Calls an callback function to pass received and parsed GPS data to Android.
static void reader_call_callback(GpsDataReader* r) {
  if (!r) {
    ALOGW("%s: called with r=NULL", __FUNCTION__);
    return;
  }
  if (!r->callback) {
    ALOGW("%s: no callback registered; keeping the data to send later",
          __FUNCTION__);
    return;
  }
  if (!r->fix.flags) {
    ALOGW("%s: no GPS fix", __FUNCTION__);
    return;
  }
  // Always uses current time converted to UTC time in milliseconds.
  time_t secs = time(NULL);  // seconds from 01/01/1970.
  r->fix.timestamp = (long long)secs * 1000;

#if GPS_DEBUG
  D("* Parsed GPS Data");
  if (r->fix.flags & GPS_LOCATION_HAS_LAT_LONG) {
    D(" - latitude = %g", r->fix.latitude);
    D(" - longitude = %g", r->fix.longitude);
  }
  if (r->fix.flags & GPS_LOCATION_HAS_ALTITUDE)
    D(" - altitude = %g", r->fix.altitude);
  if (r->fix.flags & GPS_LOCATION_HAS_SPEED) D(" - speed = %g", r->fix.speed);
  if (r->fix.flags & GPS_LOCATION_HAS_BEARING)
    D(" - bearing = %g", r->fix.bearing);
  if (r->fix.flags & GPS_LOCATION_HAS_ACCURACY)
    D(" - accuracy = %g", r->fix.accuracy);
  long long utc_secs = r->fix.timestamp / 1000;
  struct tm utc;
  gmtime_r((time_t*)&utc_secs, &utc);
  D(" - time = %s", asctime(&utc));
#endif

  D("Sending fix to callback %p", r->callback);
  r->callback(&r->fix);
}

// Parses data received so far and calls reader_call_callback().
static void reader_parse_message(GpsDataReader* r) {
  D("Received: '%s'", r->buffer);

  int num_read = sscanf(r->buffer, "%lf,%lf,%lf,%f,%f,%f", &r->fix.longitude,
                        &r->fix.latitude, &r->fix.altitude, &r->fix.bearing,
                        &r->fix.speed, &r->fix.accuracy);
  if (num_read != 6) {
    ALOGE("Couldn't find 6 values from the received message %s.", r->buffer);
    return;
  }
  r->fix.flags = DEFAULT_GPS_LOCATION_FLAG;
  reader_call_callback(r);
}

// Accepts a newly received string & calls reader_parse_message if '\n' is seen.
static void reader_accept_string(GpsDataReader* r, char* const str,
                                 const int len) {
  int index;
  for (index = 0; index < len; index++) {
    if (r->index >= (int)sizeof(r->buffer) - 1) {
      if (str[index] == '\n') {
        ALOGW("Message longer than buffer; new byte (%d) skipped.", str[index]);
        r->index = 0;
      }
    } else {
      r->buffer[r->index++] = str[index];
      if (str[index] == '\n') {
        r->buffer[r->index] = '\0';
        reader_parse_message(r);
        r->index = 0;
      }
    }
  }
}

// GPS state threads which communicates with control and data sockets.
void gps_state_thread(void* arg) {
  GpsState* state = (GpsState*)arg;
  GpsDataReader reader;
  int epoll_fd = epoll_create(2);
  int started = -1;
  int gps_fd = state->fd;
  int control_fd = state->control[1];

  memset(&reader, 0, sizeof(reader));
  reader.fix.size = sizeof(reader.fix);

  epoll_register(epoll_fd, control_fd);
  epoll_register(epoll_fd, gps_fd);

  while (1) {
    struct epoll_event events[2];
    int nevents, event_index;

    nevents = epoll_wait(epoll_fd, events, 2, 500);
    D("Thread received %d events", nevents);
    if (nevents < 0) {
      if (errno != EINTR)
        ALOGE("epoll_wait() unexpected error: %s", strerror(errno));
      continue;
    } else if (nevents == 0) {
      if (started == 1) {
        reader_call_callback(&reader);
      }
      continue;
    }

    for (event_index = 0; event_index < nevents; event_index++) {
      if ((events[event_index].events & (EPOLLERR | EPOLLHUP)) != 0) {
        ALOGE("EPOLLERR or EPOLLHUP after epoll_wait() !?");
        goto Exit;
      }

      if ((events[event_index].events & EPOLLIN) != 0) {
        int fd = events[event_index].data.fd;
        if (fd == control_fd) {
          unsigned char cmd = 255;
          int ret;
          do {
            ret = read(fd, &cmd, 1);
          } while (ret < 0 && errno == EINTR);

          if (cmd == CMD_STOP || cmd == CMD_QUIT) {
            if (started == 1) {
              D("Thread stopping");
              started = 0;
              reader.callback = NULL;
            }
            if (cmd == CMD_QUIT) {
              D("Thread quitting");
              goto Exit;
            }
          } else if (cmd == CMD_START) {
            if (started != 1) {
              reader.callback = state->callbacks.location_cb;
              D("Thread starting callback=%p", reader.callback);
              reader_call_callback(&reader);
              started = 1;
            }
          } else {
            ALOGE("unknown control command %d", cmd);
          }
        } else if (fd == gps_fd) {
          char buff[256];
          int ret;
          for (;;) {
            ret = read(fd, buff, sizeof(buff));
            if (ret < 0) {
              if (errno == EINTR) continue;
              if (errno != EWOULDBLOCK)
                ALOGE("error while reading from gps daemon socket: %s:",
                      strerror(errno));
              break;
            }
            D("Thread received %d bytes: %.*s", ret, ret, buff);
            reader_accept_string(&reader, buff, ret);
          }
        } else {
          ALOGE("epoll_wait() returned unknown fd %d.", fd);
        }
      }
    }
  }

Exit:
  epoll_deregister(epoll_fd, control_fd);
  epoll_deregister(epoll_fd, gps_fd);
}
