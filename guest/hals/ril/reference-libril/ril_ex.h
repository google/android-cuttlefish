/*
* Copyright (C) 2014 The Android Open Source Project
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

#ifndef RIL_EX_H_INCLUDED
#define RIL_EX_H_INCLUDED

#include <guest/hals/ril/libril/ril.h>
#include <telephony/record_stream.h>

#define NUM_ELEMS_SOCKET(a)     (sizeof (a) / sizeof (a)[0])

struct ril_event;

void rilEventAddWakeup_helper(struct ril_event *ev);
int blockingWrite_helper(int fd, void* data, size_t len);

enum SocketWakeType {DONT_WAKE, WAKE_PARTIAL};

typedef enum {
    RIL_TELEPHONY_SOCKET,
    RIL_SAP_SOCKET
} RIL_SOCKET_TYPE;

typedef struct SocketListenParam {
    RIL_SOCKET_ID socket_id;
    int fdListen;
    int fdCommand;
    const char* processName;
    struct ril_event* commands_event;
    struct ril_event* listen_event;
    void (*processCommandsCallback)(int fd, short flags, void *param);
    RecordStream *p_rs;
    RIL_SOCKET_TYPE type;
} SocketListenParam;

#endif
