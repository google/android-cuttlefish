/* //guest/hals/ril/ril.cpp
**
** Copyright 2006, The Android Open Source Project
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

#define LOG_TAG "RILC"

#include <hardware_legacy/power.h>
#include <guest/hals/ril/libril/ril.h>
#include <telephony/ril_cdma_sms.h>
#include <cutils/sockets.h>
#include <telephony/record_stream.h>
#include <utils/Log.h>
#include <utils/SystemClock.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/limits.h>
#include <sys/system_properties.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <assert.h>
#include <ctype.h>
#include <sys/un.h>
#include <assert.h>
#include <netinet/in.h>
#include <cutils/properties.h>
#include <RilSapSocket.h>
#include <guest/hals/ril/libril/ril_service.h>
#include <sap_service.h>

extern "C" void
RIL_onRequestComplete(RIL_Token t, RIL_Errno e, void *response, size_t responselen);

extern "C" void
RIL_onRequestAck(RIL_Token t);
namespace android {

#define PHONE_PROCESS "radio"
#define BLUETOOTH_PROCESS "bluetooth"

#define ANDROID_WAKE_LOCK_NAME "radio-interface"

#define ANDROID_WAKE_LOCK_SECS 0
#define ANDROID_WAKE_LOCK_USECS 200000

#define PROPERTY_RIL_IMPL "gsm.version.ril-impl"

// match with constant in RIL.java
#define MAX_COMMAND_BYTES (8 * 1024)

// Basically: memset buffers that the client library
// shouldn't be using anymore in an attempt to find
// memory usage issues sooner.
#define MEMSET_FREED 1

#define NUM_ELEMS(a)     (sizeof (a) / sizeof (a)[0])

/* Negative values for private RIL errno's */
#define RIL_ERRNO_INVALID_RESPONSE (-1)
#define RIL_ERRNO_NO_MEMORY (-12)

// request, response, and unsolicited msg print macro
#define PRINTBUF_SIZE 8096

enum WakeType {DONT_WAKE, WAKE_PARTIAL};

typedef struct {
    int requestNumber;
    int (*responseFunction) (int slotId, int responseType, int token,
            RIL_Errno e, void *response, size_t responselen);
    WakeType wakeType;
} UnsolResponseInfo;

typedef struct UserCallbackInfo {
    RIL_TimedCallback p_callback;
    void *userParam;
    struct ril_event event;
    struct UserCallbackInfo *p_next;
} UserCallbackInfo;

extern "C" const char * failCauseToString(RIL_Errno);
extern "C" const char * callStateToString(RIL_CallState);
extern "C" const char * radioStateToString(RIL_RadioState);
extern "C" const char * rilSocketIdToString(RIL_SOCKET_ID socket_id);

extern "C"
char ril_service_name_base[MAX_SERVICE_NAME_LENGTH] = RIL_SERVICE_NAME_BASE;
extern "C"
char ril_service_name[MAX_SERVICE_NAME_LENGTH] = RIL1_SERVICE_NAME;
/*******************************************************************/

RIL_RadioFunctions s_callbacks = {0, NULL, NULL, NULL, NULL, NULL};
static int s_registerCalled = 0;

static pthread_t s_tid_dispatch;
static int s_started = 0;

static int s_fdWakeupRead;
static int s_fdWakeupWrite;

int s_wakelock_count = 0;

static struct ril_event s_wakeupfd_event;

static pthread_mutex_t s_pendingRequestsMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t s_wakeLockCountMutex = PTHREAD_MUTEX_INITIALIZER;
static RequestInfo *s_pendingRequests = NULL;

#if (SIM_COUNT >= 2)
static pthread_mutex_t s_pendingRequestsMutex_socket2  = PTHREAD_MUTEX_INITIALIZER;
static RequestInfo *s_pendingRequests_socket2          = NULL;
#endif

#if (SIM_COUNT >= 3)
static pthread_mutex_t s_pendingRequestsMutex_socket3  = PTHREAD_MUTEX_INITIALIZER;
static RequestInfo *s_pendingRequests_socket3          = NULL;
#endif

#if (SIM_COUNT >= 4)
static pthread_mutex_t s_pendingRequestsMutex_socket4  = PTHREAD_MUTEX_INITIALIZER;
static RequestInfo *s_pendingRequests_socket4          = NULL;
#endif

static const struct timeval TIMEVAL_WAKE_TIMEOUT = {ANDROID_WAKE_LOCK_SECS,ANDROID_WAKE_LOCK_USECS};


static pthread_mutex_t s_startupMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_startupCond = PTHREAD_COND_INITIALIZER;

static UserCallbackInfo *s_last_wake_timeout_info = NULL;

static void *s_lastNITZTimeData = NULL;
static size_t s_lastNITZTimeDataSize;

#if RILC_LOG
    static char printBuf[PRINTBUF_SIZE];
#endif

/*******************************************************************/
static void grabPartialWakeLock();
void releaseWakeLock();
static void wakeTimeoutCallback(void *);

#ifdef RIL_SHLIB
#if defined(ANDROID_MULTI_SIM)
extern "C" void RIL_onUnsolicitedResponse(int unsolResponse, const void *data,
                                size_t datalen, RIL_SOCKET_ID socket_id);
#else
extern "C" void RIL_onUnsolicitedResponse(int unsolResponse, const void *data,
                                size_t datalen);
#endif
#endif

#if defined(ANDROID_MULTI_SIM)
#define RIL_UNSOL_RESPONSE(a, b, c, d) RIL_onUnsolicitedResponse((a), (b), (c), (d))
#else
#define RIL_UNSOL_RESPONSE(a, b, c, d) RIL_onUnsolicitedResponse((a), (b), (c))
#endif

static UserCallbackInfo * internalRequestTimedCallback
    (RIL_TimedCallback callback, void *param,
        const struct timeval *relativeTime);

/** Index == requestNumber */
static CommandInfo s_commands[] = {
#include "ril_commands.h"
};

static UnsolResponseInfo s_unsolResponses[] = {
#include "ril_unsol_commands.h"
};

char * RIL_getServiceName() {
    return ril_service_name;
}

RequestInfo *
addRequestToList(int serial, int slotId, int request) {
    RequestInfo *pRI;
    int ret;
    RIL_SOCKET_ID socket_id = (RIL_SOCKET_ID) slotId;
    /* Hook for current context */
    /* pendingRequestsMutextHook refer to &s_pendingRequestsMutex */
    pthread_mutex_t* pendingRequestsMutexHook = &s_pendingRequestsMutex;
    /* pendingRequestsHook refer to &s_pendingRequests */
    RequestInfo**    pendingRequestsHook = &s_pendingRequests;

#if (SIM_COUNT >= 2)
    if (socket_id == RIL_SOCKET_2) {
        pendingRequestsMutexHook = &s_pendingRequestsMutex_socket2;
        pendingRequestsHook = &s_pendingRequests_socket2;
    }
#if (SIM_COUNT >= 3)
    else if (socket_id == RIL_SOCKET_3) {
        pendingRequestsMutexHook = &s_pendingRequestsMutex_socket3;
        pendingRequestsHook = &s_pendingRequests_socket3;
    }
#endif
#if (SIM_COUNT >= 4)
    else if (socket_id == RIL_SOCKET_4) {
        pendingRequestsMutexHook = &s_pendingRequestsMutex_socket4;
        pendingRequestsHook = &s_pendingRequests_socket4;
    }
#endif
#endif

    pRI = (RequestInfo *)calloc(1, sizeof(RequestInfo));
    if (pRI == NULL) {
        RLOGE("Memory allocation failed for request %s", requestToString(request));
        return NULL;
    }

    pRI->token = serial;
    pRI->pCI = &(s_commands[request]);
    pRI->socket_id = socket_id;

    ret = pthread_mutex_lock(pendingRequestsMutexHook);
    assert (ret == 0);

    pRI->p_next = *pendingRequestsHook;
    *pendingRequestsHook = pRI;

    ret = pthread_mutex_unlock(pendingRequestsMutexHook);
    assert (ret == 0);

    return pRI;
}

static void triggerEvLoop() {
    int ret;
    if (!pthread_equal(pthread_self(), s_tid_dispatch)) {
        /* trigger event loop to wakeup. No reason to do this,
         * if we're in the event loop thread */
         do {
            ret = write (s_fdWakeupWrite, " ", 1);
         } while (ret < 0 && errno == EINTR);
    }
}

static void rilEventAddWakeup(struct ril_event *ev) {
    ril_event_add(ev);
    triggerEvLoop();
}

/**
 * A write on the wakeup fd is done just to pop us out of select()
 * We empty the buffer here and then ril_event will reset the timers on the
 * way back down
 */
static void processWakeupCallback(int fd, short flags, void *param) {
    char buff[16];
    int ret;

    RLOGV("processWakeupCallback");

    /* empty our wakeup socket out */
    do {
        ret = read(s_fdWakeupRead, &buff, sizeof(buff));
    } while (ret > 0 || (ret < 0 && errno == EINTR));
}

static void resendLastNITZTimeData(RIL_SOCKET_ID socket_id) {
    if (s_lastNITZTimeData != NULL) {
        int responseType = (s_callbacks.version >= 13)
                           ? RESPONSE_UNSOLICITED_ACK_EXP
                           : RESPONSE_UNSOLICITED;
        // acquire read lock for the service before calling nitzTimeReceivedInd() since it reads
        // nitzTimeReceived in ril_service
        pthread_rwlock_t *radioServiceRwlockPtr = radio_1_5::getRadioServiceRwlock(
                (int) socket_id);
        int rwlockRet = pthread_rwlock_rdlock(radioServiceRwlockPtr);
        assert(rwlockRet == 0);

        int ret = radio_1_5::nitzTimeReceivedInd(
            (int)socket_id, responseType, 0,
            RIL_E_SUCCESS, s_lastNITZTimeData, s_lastNITZTimeDataSize);
        if (ret == 0) {
            free(s_lastNITZTimeData);
            s_lastNITZTimeData = NULL;
        }

        rwlockRet = pthread_rwlock_unlock(radioServiceRwlockPtr);
        assert(rwlockRet == 0);
    }
}

void onNewCommandConnect(RIL_SOCKET_ID socket_id) {
    // Inform we are connected and the ril version
    int rilVer = s_callbacks.version;
    RIL_UNSOL_RESPONSE(RIL_UNSOL_RIL_CONNECTED,
                                    &rilVer, sizeof(rilVer), socket_id);

    // implicit radio state changed
    RIL_UNSOL_RESPONSE(RIL_UNSOL_RESPONSE_RADIO_STATE_CHANGED,
                                    NULL, 0, socket_id);

    // Send last NITZ time data, in case it was missed
    if (s_lastNITZTimeData != NULL) {
        resendLastNITZTimeData(socket_id);
    }

    // Get version string
    if (s_callbacks.getVersion != NULL) {
        const char *version;
        version = s_callbacks.getVersion();
        RLOGI("RIL Daemon version: %s\n", version);

        property_set(PROPERTY_RIL_IMPL, version);
    } else {
        RLOGI("RIL Daemon version: unavailable\n");
        property_set(PROPERTY_RIL_IMPL, "unavailable");
    }

}

static void userTimerCallback (int fd, short flags, void *param) {
    UserCallbackInfo *p_info;

    p_info = (UserCallbackInfo *)param;

    p_info->p_callback(p_info->userParam);


    // FIXME generalize this...there should be a cancel mechanism
    if (s_last_wake_timeout_info != NULL && s_last_wake_timeout_info == p_info) {
        s_last_wake_timeout_info = NULL;
    }

    free(p_info);
}


static void *
eventLoop(void *param) {
    int ret;
    int filedes[2];

    ril_event_init();

    pthread_mutex_lock(&s_startupMutex);

    s_started = 1;
    pthread_cond_broadcast(&s_startupCond);

    pthread_mutex_unlock(&s_startupMutex);

    ret = pipe(filedes);

    if (ret < 0) {
        RLOGE("Error in pipe() errno:%d", errno);
        return NULL;
    }

    s_fdWakeupRead = filedes[0];
    s_fdWakeupWrite = filedes[1];

    fcntl(s_fdWakeupRead, F_SETFL, O_NONBLOCK);

    ril_event_set (&s_wakeupfd_event, s_fdWakeupRead, true,
                processWakeupCallback, NULL);

    rilEventAddWakeup (&s_wakeupfd_event);

    // Only returns on error
    ril_event_loop();
    RLOGE ("error in event_loop_base errno:%d", errno);
    // kill self to restart on error
    kill(0, SIGKILL);

    return NULL;
}

extern "C" void
RIL_startEventLoop(void) {
    /* spin up eventLoop thread and wait for it to get started */
    s_started = 0;
    pthread_mutex_lock(&s_startupMutex);

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    int result = pthread_create(&s_tid_dispatch, &attr, eventLoop, NULL);
    if (result != 0) {
        RLOGE("Failed to create dispatch thread: %s", strerror(result));
        goto done;
    }

    while (s_started == 0) {
        pthread_cond_wait(&s_startupCond, &s_startupMutex);
    }

done:
    pthread_mutex_unlock(&s_startupMutex);
}

// Used for testing purpose only.
extern "C" void RIL_setcallbacks (const RIL_RadioFunctions *callbacks) {
    memcpy(&s_callbacks, callbacks, sizeof (RIL_RadioFunctions));
}

extern "C" void
RIL_register (const RIL_RadioFunctions *callbacks) {
    RLOGI("SIM_COUNT: %d", SIM_COUNT);

    if (callbacks == NULL) {
        RLOGE("RIL_register: RIL_RadioFunctions * null");
        return;
    }
    if (callbacks->version < RIL_VERSION_MIN) {
        RLOGE("RIL_register: version %d is to old, min version is %d",
             callbacks->version, RIL_VERSION_MIN);
        return;
    }

    RLOGD("RIL_register: Vsoc RIL version %d", callbacks->version);

    if (s_registerCalled > 0) {
        RLOGE("RIL_register has been called more than once. "
                "Subsequent call ignored");
        return;
    }

    memcpy(&s_callbacks, callbacks, sizeof (RIL_RadioFunctions));

    s_registerCalled = 1;

    RLOGI("s_registerCalled flag set, %d", s_started);
    // Little self-check

    for (int i = 0; i < (int)NUM_ELEMS(s_commands); i++) {
        assert(i == s_commands[i].requestNumber);
    }

    for (int i = 0; i < (int)NUM_ELEMS(s_unsolResponses); i++) {
        assert(i + RIL_UNSOL_RESPONSE_BASE
                == s_unsolResponses[i].requestNumber);
    }

    radio_1_5::registerService(&s_callbacks, s_commands);
    RLOGI("RILHIDL called registerService");

}

extern "C" void
RIL_register_socket (const RIL_RadioFunctions *(*Init)(const struct RIL_Env *, int, char **),
        RIL_SOCKET_TYPE socketType, int argc, char **argv) {

    const RIL_RadioFunctions* UimFuncs = NULL;

    if(Init) {
        UimFuncs = Init(&RilSapSocket::uimRilEnv, argc, argv);

        switch(socketType) {
            case RIL_SAP_SOCKET:
                RilSapSocket::initSapSocket(RIL1_SERVICE_NAME, UimFuncs);

#if (SIM_COUNT >= 2)
                RilSapSocket::initSapSocket(RIL2_SERVICE_NAME, UimFuncs);
#endif

#if (SIM_COUNT >= 3)
                RilSapSocket::initSapSocket(RIL3_SERVICE_NAME, UimFuncs);
#endif

#if (SIM_COUNT >= 4)
                RilSapSocket::initSapSocket(RIL4_SERVICE_NAME, UimFuncs);
#endif
                break;
            default:;
        }

        RLOGI("RIL_register_socket: calling registerService");
        sap::registerService(UimFuncs);
    }
}

// Check and remove RequestInfo if its a response and not just ack sent back
static int
checkAndDequeueRequestInfoIfAck(struct RequestInfo *pRI, bool isAck) {
    int ret = 0;
    /* Hook for current context
       pendingRequestsMutextHook refer to &s_pendingRequestsMutex */
    pthread_mutex_t* pendingRequestsMutexHook = &s_pendingRequestsMutex;
    /* pendingRequestsHook refer to &s_pendingRequests */
    RequestInfo ** pendingRequestsHook = &s_pendingRequests;

    if (pRI == NULL) {
        return 0;
    }

#if (SIM_COUNT >= 2)
    if (pRI->socket_id == RIL_SOCKET_2) {
        pendingRequestsMutexHook = &s_pendingRequestsMutex_socket2;
        pendingRequestsHook = &s_pendingRequests_socket2;
    }
#if (SIM_COUNT >= 3)
        if (pRI->socket_id == RIL_SOCKET_3) {
            pendingRequestsMutexHook = &s_pendingRequestsMutex_socket3;
            pendingRequestsHook = &s_pendingRequests_socket3;
        }
#endif
#if (SIM_COUNT >= 4)
    if (pRI->socket_id == RIL_SOCKET_4) {
        pendingRequestsMutexHook = &s_pendingRequestsMutex_socket4;
        pendingRequestsHook = &s_pendingRequests_socket4;
    }
#endif
#endif
    pthread_mutex_lock(pendingRequestsMutexHook);

    for(RequestInfo **ppCur = pendingRequestsHook
        ; *ppCur != NULL
        ; ppCur = &((*ppCur)->p_next)
    ) {
        if (pRI == *ppCur) {
            ret = 1;
            if (isAck) { // Async ack
                if (pRI->wasAckSent == 1) {
                    RLOGD("Ack was already sent for %s", requestToString(pRI->pCI->requestNumber));
                } else {
                    pRI->wasAckSent = 1;
                }
            } else {
                *ppCur = (*ppCur)->p_next;
            }
            break;
        }
    }

    pthread_mutex_unlock(pendingRequestsMutexHook);

    return ret;
}

extern "C" void
RIL_onRequestAck(RIL_Token t) {
    RequestInfo *pRI;

    RIL_SOCKET_ID socket_id = RIL_SOCKET_1;

    pRI = (RequestInfo *)t;

    if (!checkAndDequeueRequestInfoIfAck(pRI, true)) {
        RLOGE ("RIL_onRequestAck: invalid RIL_Token");
        return;
    }

    socket_id = pRI->socket_id;

#if VDBG
    RLOGD("Request Ack, %s", rilSocketIdToString(socket_id));
#endif

    appendPrintBuf("Ack [%04d]< %s", pRI->token, requestToString(pRI->pCI->requestNumber));

    if (pRI->cancelled == 0) {
        pthread_rwlock_t *radioServiceRwlockPtr = radio_1_5::getRadioServiceRwlock(
                (int) socket_id);
        int rwlockRet = pthread_rwlock_rdlock(radioServiceRwlockPtr);
        assert(rwlockRet == 0);

        radio_1_5::acknowledgeRequest((int) socket_id, pRI->token);

        rwlockRet = pthread_rwlock_unlock(radioServiceRwlockPtr);
        assert(rwlockRet == 0);
    }
}
extern "C" void
RIL_onRequestComplete(RIL_Token t, RIL_Errno e, void *response, size_t responselen) {
    RequestInfo *pRI;
    int ret;
    RIL_SOCKET_ID socket_id = RIL_SOCKET_1;

    pRI = (RequestInfo *)t;

    if (!checkAndDequeueRequestInfoIfAck(pRI, false)) {
        RLOGE ("RIL_onRequestComplete: invalid RIL_Token");
        return;
    }

    socket_id = pRI->socket_id;

#if VDBG
    RLOGD("RequestComplete, %s", rilSocketIdToString(socket_id));
#endif

    if (pRI->local > 0) {
        // Locally issued command...void only!
        // response does not go back up the command socket
        RLOGD("C[locl]< %s", requestToString(pRI->pCI->requestNumber));

        free(pRI);
        return;
    }

    appendPrintBuf("[%04d]< %s",
        pRI->token, requestToString(pRI->pCI->requestNumber));

    if (pRI->cancelled == 0) {
        int responseType;
        if (s_callbacks.version >= 13 && pRI->wasAckSent == 1) {
            // If ack was already sent, then this call is an asynchronous response. So we need to
            // send id indicating that we expect an ack from RIL.java as we acquire wakelock here.
            responseType = RESPONSE_SOLICITED_ACK_EXP;
            grabPartialWakeLock();
        } else {
            responseType = RESPONSE_SOLICITED;
        }

        // there is a response payload, no matter success or not.
#if VDBG
        RLOGE ("Calling responseFunction() for token %d", pRI->token);
#endif

        pthread_rwlock_t *radioServiceRwlockPtr = radio_1_5::getRadioServiceRwlock((int) socket_id);
        int rwlockRet = pthread_rwlock_rdlock(radioServiceRwlockPtr);
        assert(rwlockRet == 0);

        ret = pRI->pCI->responseFunction((int) socket_id,
                responseType, pRI->token, e, response, responselen);

        rwlockRet = pthread_rwlock_unlock(radioServiceRwlockPtr);
        assert(rwlockRet == 0);
    }
    free(pRI);
}

static void
grabPartialWakeLock() {
    if (s_callbacks.version >= 13) {
        int ret;
        ret = pthread_mutex_lock(&s_wakeLockCountMutex);
        assert(ret == 0);
        acquire_wake_lock(PARTIAL_WAKE_LOCK, ANDROID_WAKE_LOCK_NAME);

        UserCallbackInfo *p_info =
                internalRequestTimedCallback(wakeTimeoutCallback, NULL, &TIMEVAL_WAKE_TIMEOUT);
        if (p_info == NULL) {
            release_wake_lock(ANDROID_WAKE_LOCK_NAME);
        } else {
            s_wakelock_count++;
            if (s_last_wake_timeout_info != NULL) {
                s_last_wake_timeout_info->userParam = (void *)1;
            }
            s_last_wake_timeout_info = p_info;
        }
        ret = pthread_mutex_unlock(&s_wakeLockCountMutex);
        assert(ret == 0);
    } else {
        acquire_wake_lock(PARTIAL_WAKE_LOCK, ANDROID_WAKE_LOCK_NAME);
    }
}

void
releaseWakeLock() {
    if (s_callbacks.version >= 13) {
        int ret;
        ret = pthread_mutex_lock(&s_wakeLockCountMutex);
        assert(ret == 0);

        if (s_wakelock_count > 1) {
            s_wakelock_count--;
        } else {
            s_wakelock_count = 0;
            release_wake_lock(ANDROID_WAKE_LOCK_NAME);
            if (s_last_wake_timeout_info != NULL) {
                s_last_wake_timeout_info->userParam = (void *)1;
            }
        }

        ret = pthread_mutex_unlock(&s_wakeLockCountMutex);
        assert(ret == 0);
    } else {
        release_wake_lock(ANDROID_WAKE_LOCK_NAME);
    }
}

/**
 * Timer callback to put us back to sleep before the default timeout
 */
static void
wakeTimeoutCallback (void *param) {
    // We're using "param != NULL" as a cancellation mechanism
    if (s_callbacks.version >= 13) {
        if (param == NULL) {
            int ret;
            ret = pthread_mutex_lock(&s_wakeLockCountMutex);
            assert(ret == 0);
            s_wakelock_count = 0;
            release_wake_lock(ANDROID_WAKE_LOCK_NAME);
            ret = pthread_mutex_unlock(&s_wakeLockCountMutex);
            assert(ret == 0);
        }
    } else {
        if (param == NULL) {
            releaseWakeLock();
        }
    }
}

#if defined(ANDROID_MULTI_SIM)
extern "C"
void RIL_onUnsolicitedResponse(int unsolResponse, const void *data,
                                size_t datalen, RIL_SOCKET_ID socket_id)
#else
extern "C"
void RIL_onUnsolicitedResponse(int unsolResponse, const void *data,
                                size_t datalen)
#endif
{
    int unsolResponseIndex;
    int ret;
    bool shouldScheduleTimeout = false;
    RIL_SOCKET_ID soc_id = RIL_SOCKET_1;

#if defined(ANDROID_MULTI_SIM)
    soc_id = socket_id;
#endif


    if (s_registerCalled == 0) {
        // Ignore RIL_onUnsolicitedResponse before RIL_register
        RLOGW("RIL_onUnsolicitedResponse called before RIL_register");
        return;
    }

    unsolResponseIndex = unsolResponse - RIL_UNSOL_RESPONSE_BASE;

    if ((unsolResponseIndex < 0)
        || (unsolResponseIndex >= (int32_t)NUM_ELEMS(s_unsolResponses))) {
        RLOGE("unsupported unsolicited response code %d", unsolResponse);
        return;
    }

    // Grab a wake lock if needed for this reponse,
    // as we exit we'll either release it immediately
    // or set a timer to release it later.
    switch (s_unsolResponses[unsolResponseIndex].wakeType) {
        case WAKE_PARTIAL:
            grabPartialWakeLock();
            shouldScheduleTimeout = true;
        break;

        case DONT_WAKE:
        default:
            // No wake lock is grabed so don't set timeout
            shouldScheduleTimeout = false;
            break;
    }

    appendPrintBuf("[UNSL]< %s", requestToString(unsolResponse));

    int responseType;
    if (s_callbacks.version >= 13
                && s_unsolResponses[unsolResponseIndex].wakeType == WAKE_PARTIAL) {
        responseType = RESPONSE_UNSOLICITED_ACK_EXP;
    } else {
        responseType = RESPONSE_UNSOLICITED;
    }

    pthread_rwlock_t *radioServiceRwlockPtr = radio_1_5::getRadioServiceRwlock((int) soc_id);
    int rwlockRet;

    if (unsolResponse == RIL_UNSOL_NITZ_TIME_RECEIVED) {
        // get a write lock in caes of NITZ since setNitzTimeReceived() is called
        rwlockRet = pthread_rwlock_wrlock(radioServiceRwlockPtr);
        assert(rwlockRet == 0);
        radio_1_5::setNitzTimeReceived((int) soc_id, android::elapsedRealtime());
    } else {
        rwlockRet = pthread_rwlock_rdlock(radioServiceRwlockPtr);
        assert(rwlockRet == 0);
    }

    if (s_unsolResponses[unsolResponseIndex].responseFunction) {
        RLOGD("calling UNSOLICITED responseFunction for index %d", unsolResponseIndex);
        ret = s_unsolResponses[unsolResponseIndex].responseFunction(
                (int) soc_id, responseType, 0, RIL_E_SUCCESS, const_cast<void*>(data),
                datalen);
    } else {
        RLOGW("No call responseFunction defined for UNSOLICITED");
    }

    rwlockRet = pthread_rwlock_unlock(radioServiceRwlockPtr);
    assert(rwlockRet == 0);

    if (s_callbacks.version < 13) {
        if (shouldScheduleTimeout) {
            UserCallbackInfo *p_info = internalRequestTimedCallback(wakeTimeoutCallback, NULL,
                    &TIMEVAL_WAKE_TIMEOUT);

            if (p_info == NULL) {
                goto error_exit;
            } else {
                // Cancel the previous request
                if (s_last_wake_timeout_info != NULL) {
                    s_last_wake_timeout_info->userParam = (void *)1;
                }
                s_last_wake_timeout_info = p_info;
            }
        }
    }

#if VDBG
    RLOGI("%s UNSOLICITED: %s length:%zu", rilSocketIdToString(soc_id),
            requestToString(unsolResponse), datalen);
#endif

    if (ret != 0 && unsolResponse == RIL_UNSOL_NITZ_TIME_RECEIVED) {
        // Unfortunately, NITZ time is not poll/update like everything
        // else in the system. So, if the upstream client isn't connected,
        // keep a copy of the last NITZ response (with receive time noted
        // above) around so we can deliver it when it is connected

        if (s_lastNITZTimeData != NULL) {
            free(s_lastNITZTimeData);
            s_lastNITZTimeData = NULL;
        }

        s_lastNITZTimeData = calloc(datalen, 1);
        if (s_lastNITZTimeData == NULL) {
            RLOGE("Memory allocation failed in RIL_onUnsolicitedResponse");
            goto error_exit;
        }
        s_lastNITZTimeDataSize = datalen;
        memcpy(s_lastNITZTimeData, data, datalen);
    }

    // Normal exit
    return;

error_exit:
    if (shouldScheduleTimeout) {
        releaseWakeLock();
    }
}

/** FIXME generalize this if you track UserCAllbackInfo, clear it
    when the callback occurs
*/
static UserCallbackInfo *
internalRequestTimedCallback (RIL_TimedCallback callback, void *param,
                                const struct timeval *relativeTime)
{
    struct timeval myRelativeTime;
    UserCallbackInfo *p_info;

    p_info = (UserCallbackInfo *) calloc(1, sizeof(UserCallbackInfo));
    if (p_info == NULL) {
        RLOGE("Memory allocation failed in internalRequestTimedCallback");
        return p_info;

    }

    p_info->p_callback = callback;
    p_info->userParam = param;

    if (relativeTime == NULL) {
        /* treat null parameter as a 0 relative time */
        memset (&myRelativeTime, 0, sizeof(myRelativeTime));
    } else {
        /* FIXME I think event_add's tv param is really const anyway */
        memcpy (&myRelativeTime, relativeTime, sizeof(myRelativeTime));
    }

    ril_event_set(&(p_info->event), -1, false, userTimerCallback, p_info);

    ril_timer_add(&(p_info->event), &myRelativeTime);

    triggerEvLoop();
    return p_info;
}


extern "C" void
RIL_requestTimedCallback (RIL_TimedCallback callback, void *param,
                                const struct timeval *relativeTime) {
    internalRequestTimedCallback (callback, param, relativeTime);
}

const char *
failCauseToString(RIL_Errno e) {
    switch(e) {
        case RIL_E_SUCCESS: return "E_SUCCESS";
        case RIL_E_RADIO_NOT_AVAILABLE: return "E_RADIO_NOT_AVAILABLE";
        case RIL_E_GENERIC_FAILURE: return "E_GENERIC_FAILURE";
        case RIL_E_PASSWORD_INCORRECT: return "E_PASSWORD_INCORRECT";
        case RIL_E_SIM_PIN2: return "E_SIM_PIN2";
        case RIL_E_SIM_PUK2: return "E_SIM_PUK2";
        case RIL_E_REQUEST_NOT_SUPPORTED: return "E_REQUEST_NOT_SUPPORTED";
        case RIL_E_CANCELLED: return "E_CANCELLED";
        case RIL_E_OP_NOT_ALLOWED_DURING_VOICE_CALL: return "E_OP_NOT_ALLOWED_DURING_VOICE_CALL";
        case RIL_E_OP_NOT_ALLOWED_BEFORE_REG_TO_NW: return "E_OP_NOT_ALLOWED_BEFORE_REG_TO_NW";
        case RIL_E_SMS_SEND_FAIL_RETRY: return "E_SMS_SEND_FAIL_RETRY";
        case RIL_E_SIM_ABSENT:return "E_SIM_ABSENT";
        case RIL_E_ILLEGAL_SIM_OR_ME:return "E_ILLEGAL_SIM_OR_ME";
#ifdef FEATURE_MULTIMODE_ANDROID
        case RIL_E_SUBSCRIPTION_NOT_AVAILABLE:return "E_SUBSCRIPTION_NOT_AVAILABLE";
        case RIL_E_MODE_NOT_SUPPORTED:return "E_MODE_NOT_SUPPORTED";
#endif
        case RIL_E_FDN_CHECK_FAILURE: return "E_FDN_CHECK_FAILURE";
        case RIL_E_MISSING_RESOURCE: return "E_MISSING_RESOURCE";
        case RIL_E_NO_SUCH_ELEMENT: return "E_NO_SUCH_ELEMENT";
        case RIL_E_DIAL_MODIFIED_TO_USSD: return "E_DIAL_MODIFIED_TO_USSD";
        case RIL_E_DIAL_MODIFIED_TO_SS: return "E_DIAL_MODIFIED_TO_SS";
        case RIL_E_DIAL_MODIFIED_TO_DIAL: return "E_DIAL_MODIFIED_TO_DIAL";
        case RIL_E_USSD_MODIFIED_TO_DIAL: return "E_USSD_MODIFIED_TO_DIAL";
        case RIL_E_USSD_MODIFIED_TO_SS: return "E_USSD_MODIFIED_TO_SS";
        case RIL_E_USSD_MODIFIED_TO_USSD: return "E_USSD_MODIFIED_TO_USSD";
        case RIL_E_SS_MODIFIED_TO_DIAL: return "E_SS_MODIFIED_TO_DIAL";
        case RIL_E_SS_MODIFIED_TO_USSD: return "E_SS_MODIFIED_TO_USSD";
        case RIL_E_SUBSCRIPTION_NOT_SUPPORTED: return "E_SUBSCRIPTION_NOT_SUPPORTED";
        case RIL_E_SS_MODIFIED_TO_SS: return "E_SS_MODIFIED_TO_SS";
        case RIL_E_LCE_NOT_SUPPORTED: return "E_LCE_NOT_SUPPORTED";
        case RIL_E_NO_MEMORY: return "E_NO_MEMORY";
        case RIL_E_INTERNAL_ERR: return "E_INTERNAL_ERR";
        case RIL_E_SYSTEM_ERR: return "E_SYSTEM_ERR";
        case RIL_E_MODEM_ERR: return "E_MODEM_ERR";
        case RIL_E_INVALID_STATE: return "E_INVALID_STATE";
        case RIL_E_NO_RESOURCES: return "E_NO_RESOURCES";
        case RIL_E_SIM_ERR: return "E_SIM_ERR";
        case RIL_E_INVALID_ARGUMENTS: return "E_INVALID_ARGUMENTS";
        case RIL_E_INVALID_SIM_STATE: return "E_INVALID_SIM_STATE";
        case RIL_E_INVALID_MODEM_STATE: return "E_INVALID_MODEM_STATE";
        case RIL_E_INVALID_CALL_ID: return "E_INVALID_CALL_ID";
        case RIL_E_NO_SMS_TO_ACK: return "E_NO_SMS_TO_ACK";
        case RIL_E_NETWORK_ERR: return "E_NETWORK_ERR";
        case RIL_E_REQUEST_RATE_LIMITED: return "E_REQUEST_RATE_LIMITED";
        case RIL_E_SIM_BUSY: return "E_SIM_BUSY";
        case RIL_E_SIM_FULL: return "E_SIM_FULL";
        case RIL_E_NETWORK_REJECT: return "E_NETWORK_REJECT";
        case RIL_E_OPERATION_NOT_ALLOWED: return "E_OPERATION_NOT_ALLOWED";
        case RIL_E_EMPTY_RECORD: return "E_EMPTY_RECORD";
        case RIL_E_INVALID_SMS_FORMAT: return "E_INVALID_SMS_FORMAT";
        case RIL_E_ENCODING_ERR: return "E_ENCODING_ERR";
        case RIL_E_INVALID_SMSC_ADDRESS: return "E_INVALID_SMSC_ADDRESS";
        case RIL_E_NO_SUCH_ENTRY: return "E_NO_SUCH_ENTRY";
        case RIL_E_NETWORK_NOT_READY: return "E_NETWORK_NOT_READY";
        case RIL_E_NOT_PROVISIONED: return "E_NOT_PROVISIONED";
        case RIL_E_NO_SUBSCRIPTION: return "E_NO_SUBSCRIPTION";
        case RIL_E_NO_NETWORK_FOUND: return "E_NO_NETWORK_FOUND";
        case RIL_E_DEVICE_IN_USE: return "E_DEVICE_IN_USE";
        case RIL_E_ABORTED: return "E_ABORTED";
        case RIL_E_INVALID_RESPONSE: return "INVALID_RESPONSE";
        case RIL_E_OEM_ERROR_1: return "E_OEM_ERROR_1";
        case RIL_E_OEM_ERROR_2: return "E_OEM_ERROR_2";
        case RIL_E_OEM_ERROR_3: return "E_OEM_ERROR_3";
        case RIL_E_OEM_ERROR_4: return "E_OEM_ERROR_4";
        case RIL_E_OEM_ERROR_5: return "E_OEM_ERROR_5";
        case RIL_E_OEM_ERROR_6: return "E_OEM_ERROR_6";
        case RIL_E_OEM_ERROR_7: return "E_OEM_ERROR_7";
        case RIL_E_OEM_ERROR_8: return "E_OEM_ERROR_8";
        case RIL_E_OEM_ERROR_9: return "E_OEM_ERROR_9";
        case RIL_E_OEM_ERROR_10: return "E_OEM_ERROR_10";
        case RIL_E_OEM_ERROR_11: return "E_OEM_ERROR_11";
        case RIL_E_OEM_ERROR_12: return "E_OEM_ERROR_12";
        case RIL_E_OEM_ERROR_13: return "E_OEM_ERROR_13";
        case RIL_E_OEM_ERROR_14: return "E_OEM_ERROR_14";
        case RIL_E_OEM_ERROR_15: return "E_OEM_ERROR_15";
        case RIL_E_OEM_ERROR_16: return "E_OEM_ERROR_16";
        case RIL_E_OEM_ERROR_17: return "E_OEM_ERROR_17";
        case RIL_E_OEM_ERROR_18: return "E_OEM_ERROR_18";
        case RIL_E_OEM_ERROR_19: return "E_OEM_ERROR_19";
        case RIL_E_OEM_ERROR_20: return "E_OEM_ERROR_20";
        case RIL_E_OEM_ERROR_21: return "E_OEM_ERROR_21";
        case RIL_E_OEM_ERROR_22: return "E_OEM_ERROR_22";
        case RIL_E_OEM_ERROR_23: return "E_OEM_ERROR_23";
        case RIL_E_OEM_ERROR_24: return "E_OEM_ERROR_24";
        case RIL_E_OEM_ERROR_25: return "E_OEM_ERROR_25";
        default: return "<unknown error>";
    }
}

const char *
radioStateToString(RIL_RadioState s) {
    switch(s) {
        case RADIO_STATE_OFF: return "RADIO_OFF";
        case RADIO_STATE_UNAVAILABLE: return "RADIO_UNAVAILABLE";
        case RADIO_STATE_ON:return"RADIO_ON";
        default: return "<unknown state>";
    }
}

const char *
callStateToString(RIL_CallState s) {
    switch(s) {
        case RIL_CALL_ACTIVE : return "ACTIVE";
        case RIL_CALL_HOLDING: return "HOLDING";
        case RIL_CALL_DIALING: return "DIALING";
        case RIL_CALL_ALERTING: return "ALERTING";
        case RIL_CALL_INCOMING: return "INCOMING";
        case RIL_CALL_WAITING: return "WAITING";
        default: return "<unknown state>";
    }
}

const char *
requestToString(int request) {
/*
 cat guest/hals/ril/libril/ril_commands.h \
 | egrep "^ *{RIL_" \
 | sed -re 's/\{RIL_([^,]+),[^,]+,([^}]+).+/case RIL_\1: return "\1";/'


 cat guest/hals/ril/libril/ril_unsol_commands.h \
 | egrep "^ *{RIL_" \
 | sed -re 's/\{RIL_([^,]+),([^}]+).+/case RIL_\1: return "\1";/'

*/
    switch(request) {
        case RIL_REQUEST_GET_SIM_STATUS: return "GET_SIM_STATUS";
        case RIL_REQUEST_ENTER_SIM_PIN: return "ENTER_SIM_PIN";
        case RIL_REQUEST_ENTER_SIM_PUK: return "ENTER_SIM_PUK";
        case RIL_REQUEST_ENTER_SIM_PIN2: return "ENTER_SIM_PIN2";
        case RIL_REQUEST_ENTER_SIM_PUK2: return "ENTER_SIM_PUK2";
        case RIL_REQUEST_CHANGE_SIM_PIN: return "CHANGE_SIM_PIN";
        case RIL_REQUEST_CHANGE_SIM_PIN2: return "CHANGE_SIM_PIN2";
        case RIL_REQUEST_ENTER_NETWORK_DEPERSONALIZATION: return "ENTER_NETWORK_DEPERSONALIZATION";
        case RIL_REQUEST_GET_CURRENT_CALLS: return "GET_CURRENT_CALLS";
        case RIL_REQUEST_DIAL: return "DIAL";
        case RIL_REQUEST_GET_IMSI: return "GET_IMSI";
        case RIL_REQUEST_HANGUP: return "HANGUP";
        case RIL_REQUEST_HANGUP_WAITING_OR_BACKGROUND: return "HANGUP_WAITING_OR_BACKGROUND";
        case RIL_REQUEST_HANGUP_FOREGROUND_RESUME_BACKGROUND: return "HANGUP_FOREGROUND_RESUME_BACKGROUND";
        case RIL_REQUEST_SWITCH_WAITING_OR_HOLDING_AND_ACTIVE: return "SWITCH_WAITING_OR_HOLDING_AND_ACTIVE";
        case RIL_REQUEST_CONFERENCE: return "CONFERENCE";
        case RIL_REQUEST_UDUB: return "UDUB";
        case RIL_REQUEST_LAST_CALL_FAIL_CAUSE: return "LAST_CALL_FAIL_CAUSE";
        case RIL_REQUEST_SIGNAL_STRENGTH: return "SIGNAL_STRENGTH";
        case RIL_REQUEST_VOICE_REGISTRATION_STATE: return "VOICE_REGISTRATION_STATE";
        case RIL_REQUEST_DATA_REGISTRATION_STATE: return "DATA_REGISTRATION_STATE";
        case RIL_REQUEST_OPERATOR: return "OPERATOR";
        case RIL_REQUEST_RADIO_POWER: return "RADIO_POWER";
        case RIL_REQUEST_DTMF: return "DTMF";
        case RIL_REQUEST_SEND_SMS: return "SEND_SMS";
        case RIL_REQUEST_SEND_SMS_EXPECT_MORE: return "SEND_SMS_EXPECT_MORE";
        case RIL_REQUEST_SETUP_DATA_CALL: return "SETUP_DATA_CALL";
        case RIL_REQUEST_SIM_IO: return "SIM_IO";
        case RIL_REQUEST_SEND_USSD: return "SEND_USSD";
        case RIL_REQUEST_CANCEL_USSD: return "CANCEL_USSD";
        case RIL_REQUEST_GET_CLIR: return "GET_CLIR";
        case RIL_REQUEST_SET_CLIR: return "SET_CLIR";
        case RIL_REQUEST_QUERY_CALL_FORWARD_STATUS: return "QUERY_CALL_FORWARD_STATUS";
        case RIL_REQUEST_SET_CALL_FORWARD: return "SET_CALL_FORWARD";
        case RIL_REQUEST_QUERY_CALL_WAITING: return "QUERY_CALL_WAITING";
        case RIL_REQUEST_SET_CALL_WAITING: return "SET_CALL_WAITING";
        case RIL_REQUEST_SMS_ACKNOWLEDGE: return "SMS_ACKNOWLEDGE";
        case RIL_REQUEST_GET_IMEI: return "GET_IMEI";
        case RIL_REQUEST_GET_IMEISV: return "GET_IMEISV";
        case RIL_REQUEST_ANSWER: return "ANSWER";
        case RIL_REQUEST_DEACTIVATE_DATA_CALL: return "DEACTIVATE_DATA_CALL";
        case RIL_REQUEST_QUERY_FACILITY_LOCK: return "QUERY_FACILITY_LOCK";
        case RIL_REQUEST_SET_FACILITY_LOCK: return "SET_FACILITY_LOCK";
        case RIL_REQUEST_CHANGE_BARRING_PASSWORD: return "CHANGE_BARRING_PASSWORD";
        case RIL_REQUEST_QUERY_NETWORK_SELECTION_MODE: return "QUERY_NETWORK_SELECTION_MODE";
        case RIL_REQUEST_SET_SYSTEM_SELECTION_CHANNELS: return "RIL_REQUEST_SET_SYSTEM_SELECTION_CHANNELS";
        case RIL_REQUEST_START_NETWORK_SCAN: return "RIL_REQUEST_START_NETWORK_SCAN";
        case RIL_REQUEST_SET_NETWORK_SELECTION_AUTOMATIC: return "SET_NETWORK_SELECTION_AUTOMATIC";
        case RIL_REQUEST_SET_NETWORK_SELECTION_MANUAL: return "SET_NETWORK_SELECTION_MANUAL";
        case RIL_REQUEST_QUERY_AVAILABLE_NETWORKS: return "QUERY_AVAILABLE_NETWORKS";
        case RIL_REQUEST_DTMF_START: return "DTMF_START";
        case RIL_REQUEST_DTMF_STOP: return "DTMF_STOP";
        case RIL_REQUEST_BASEBAND_VERSION: return "BASEBAND_VERSION";
        case RIL_REQUEST_SEPARATE_CONNECTION: return "SEPARATE_CONNECTION";
        case RIL_REQUEST_SET_MUTE: return "SET_MUTE";
        case RIL_REQUEST_GET_MUTE: return "GET_MUTE";
        case RIL_REQUEST_QUERY_CLIP: return "QUERY_CLIP";
        case RIL_REQUEST_LAST_DATA_CALL_FAIL_CAUSE: return "LAST_DATA_CALL_FAIL_CAUSE";
        case RIL_REQUEST_DATA_CALL_LIST: return "DATA_CALL_LIST";
        case RIL_REQUEST_RESET_RADIO: return "RESET_RADIO";
        case RIL_REQUEST_OEM_HOOK_RAW: return "OEM_HOOK_RAW";
        case RIL_REQUEST_OEM_HOOK_STRINGS: return "OEM_HOOK_STRINGS";
        case RIL_REQUEST_SCREEN_STATE: return "SCREEN_STATE";
        case RIL_REQUEST_SET_SUPP_SVC_NOTIFICATION: return "SET_SUPP_SVC_NOTIFICATION";
        case RIL_REQUEST_WRITE_SMS_TO_SIM: return "WRITE_SMS_TO_SIM";
        case RIL_REQUEST_DELETE_SMS_ON_SIM: return "DELETE_SMS_ON_SIM";
        case RIL_REQUEST_SET_BAND_MODE: return "SET_BAND_MODE";
        case RIL_REQUEST_QUERY_AVAILABLE_BAND_MODE: return "QUERY_AVAILABLE_BAND_MODE";
        case RIL_REQUEST_STK_GET_PROFILE: return "STK_GET_PROFILE";
        case RIL_REQUEST_STK_SET_PROFILE: return "STK_SET_PROFILE";
        case RIL_REQUEST_STK_SEND_ENVELOPE_COMMAND: return "STK_SEND_ENVELOPE_COMMAND";
        case RIL_REQUEST_STK_SEND_TERMINAL_RESPONSE: return "STK_SEND_TERMINAL_RESPONSE";
        case RIL_REQUEST_STK_HANDLE_CALL_SETUP_REQUESTED_FROM_SIM: return "STK_HANDLE_CALL_SETUP_REQUESTED_FROM_SIM";
        case RIL_REQUEST_EXPLICIT_CALL_TRANSFER: return "EXPLICIT_CALL_TRANSFER";
        case RIL_REQUEST_SET_PREFERRED_NETWORK_TYPE: return "SET_PREFERRED_NETWORK_TYPE";
        case RIL_REQUEST_GET_PREFERRED_NETWORK_TYPE: return "GET_PREFERRED_NETWORK_TYPE";
        case RIL_REQUEST_GET_NEIGHBORING_CELL_IDS: return "GET_NEIGHBORING_CELL_IDS";
        case RIL_REQUEST_SET_LOCATION_UPDATES: return "SET_LOCATION_UPDATES";
        case RIL_REQUEST_CDMA_SET_SUBSCRIPTION_SOURCE: return "CDMA_SET_SUBSCRIPTION_SOURCE";
        case RIL_REQUEST_CDMA_SET_ROAMING_PREFERENCE: return "CDMA_SET_ROAMING_PREFERENCE";
        case RIL_REQUEST_CDMA_QUERY_ROAMING_PREFERENCE: return "CDMA_QUERY_ROAMING_PREFERENCE";
        case RIL_REQUEST_SET_TTY_MODE: return "SET_TTY_MODE";
        case RIL_REQUEST_QUERY_TTY_MODE: return "QUERY_TTY_MODE";
        case RIL_REQUEST_CDMA_SET_PREFERRED_VOICE_PRIVACY_MODE: return "CDMA_SET_PREFERRED_VOICE_PRIVACY_MODE";
        case RIL_REQUEST_CDMA_QUERY_PREFERRED_VOICE_PRIVACY_MODE: return "CDMA_QUERY_PREFERRED_VOICE_PRIVACY_MODE";
        case RIL_REQUEST_CDMA_FLASH: return "CDMA_FLASH";
        case RIL_REQUEST_CDMA_BURST_DTMF: return "CDMA_BURST_DTMF";
        case RIL_REQUEST_CDMA_VALIDATE_AND_WRITE_AKEY: return "CDMA_VALIDATE_AND_WRITE_AKEY";
        case RIL_REQUEST_CDMA_SEND_SMS: return "CDMA_SEND_SMS";
        case RIL_REQUEST_CDMA_SMS_ACKNOWLEDGE: return "CDMA_SMS_ACKNOWLEDGE";
        case RIL_REQUEST_GSM_GET_BROADCAST_SMS_CONFIG: return "GSM_GET_BROADCAST_SMS_CONFIG";
        case RIL_REQUEST_GSM_SET_BROADCAST_SMS_CONFIG: return "GSM_SET_BROADCAST_SMS_CONFIG";
        case RIL_REQUEST_GSM_SMS_BROADCAST_ACTIVATION: return "GSM_SMS_BROADCAST_ACTIVATION";
        case RIL_REQUEST_CDMA_GET_BROADCAST_SMS_CONFIG: return "CDMA_GET_BROADCAST_SMS_CONFIG";
        case RIL_REQUEST_CDMA_SET_BROADCAST_SMS_CONFIG: return "CDMA_SET_BROADCAST_SMS_CONFIG";
        case RIL_REQUEST_CDMA_SMS_BROADCAST_ACTIVATION: return "CDMA_SMS_BROADCAST_ACTIVATION";
        case RIL_REQUEST_CDMA_SUBSCRIPTION: return "CDMA_SUBSCRIPTION";
        case RIL_REQUEST_CDMA_WRITE_SMS_TO_RUIM: return "CDMA_WRITE_SMS_TO_RUIM";
        case RIL_REQUEST_CDMA_DELETE_SMS_ON_RUIM: return "CDMA_DELETE_SMS_ON_RUIM";
        case RIL_REQUEST_DEVICE_IDENTITY: return "DEVICE_IDENTITY";
        case RIL_REQUEST_EXIT_EMERGENCY_CALLBACK_MODE: return "EXIT_EMERGENCY_CALLBACK_MODE";
        case RIL_REQUEST_GET_SMSC_ADDRESS: return "GET_SMSC_ADDRESS";
        case RIL_REQUEST_SET_SMSC_ADDRESS: return "SET_SMSC_ADDRESS";
        case RIL_REQUEST_REPORT_SMS_MEMORY_STATUS: return "REPORT_SMS_MEMORY_STATUS";
        case RIL_REQUEST_REPORT_STK_SERVICE_IS_RUNNING: return "REPORT_STK_SERVICE_IS_RUNNING";
        case RIL_REQUEST_CDMA_GET_SUBSCRIPTION_SOURCE: return "CDMA_GET_SUBSCRIPTION_SOURCE";
        case RIL_REQUEST_ISIM_AUTHENTICATION: return "ISIM_AUTHENTICATION";
        case RIL_REQUEST_ACKNOWLEDGE_INCOMING_GSM_SMS_WITH_PDU: return "ACKNOWLEDGE_INCOMING_GSM_SMS_WITH_PDU";
        case RIL_REQUEST_STK_SEND_ENVELOPE_WITH_STATUS: return "STK_SEND_ENVELOPE_WITH_STATUS";
        case RIL_REQUEST_VOICE_RADIO_TECH: return "VOICE_RADIO_TECH";
        case RIL_REQUEST_GET_CELL_INFO_LIST: return "GET_CELL_INFO_LIST";
        case RIL_REQUEST_SET_UNSOL_CELL_INFO_LIST_RATE: return "SET_UNSOL_CELL_INFO_LIST_RATE";
        case RIL_REQUEST_SET_INITIAL_ATTACH_APN: return "SET_INITIAL_ATTACH_APN";
        case RIL_REQUEST_IMS_REGISTRATION_STATE: return "IMS_REGISTRATION_STATE";
        case RIL_REQUEST_IMS_SEND_SMS: return "IMS_SEND_SMS";
        case RIL_REQUEST_SIM_TRANSMIT_APDU_BASIC: return "SIM_TRANSMIT_APDU_BASIC";
        case RIL_REQUEST_SIM_OPEN_CHANNEL: return "SIM_OPEN_CHANNEL";
        case RIL_REQUEST_SIM_CLOSE_CHANNEL: return "SIM_CLOSE_CHANNEL";
        case RIL_REQUEST_SIM_TRANSMIT_APDU_CHANNEL: return "SIM_TRANSMIT_APDU_CHANNEL";
        case RIL_REQUEST_NV_READ_ITEM: return "NV_READ_ITEM";
        case RIL_REQUEST_NV_WRITE_ITEM: return "NV_WRITE_ITEM";
        case RIL_REQUEST_NV_WRITE_CDMA_PRL: return "NV_WRITE_CDMA_PRL";
        case RIL_REQUEST_NV_RESET_CONFIG: return "NV_RESET_CONFIG";
        case RIL_REQUEST_SET_UICC_SUBSCRIPTION: return "SET_UICC_SUBSCRIPTION";
        case RIL_REQUEST_ALLOW_DATA: return "ALLOW_DATA";
        case RIL_REQUEST_GET_HARDWARE_CONFIG: return "GET_HARDWARE_CONFIG";
        case RIL_REQUEST_SIM_AUTHENTICATION: return "SIM_AUTHENTICATION";
        case RIL_REQUEST_GET_DC_RT_INFO: return "GET_DC_RT_INFO";
        case RIL_REQUEST_SET_DC_RT_INFO_RATE: return "SET_DC_RT_INFO_RATE";
        case RIL_REQUEST_SET_DATA_PROFILE: return "SET_DATA_PROFILE";
        case RIL_REQUEST_SHUTDOWN: return "SHUTDOWN";
        case RIL_REQUEST_GET_RADIO_CAPABILITY: return "GET_RADIO_CAPABILITY";
        case RIL_REQUEST_SET_RADIO_CAPABILITY: return "SET_RADIO_CAPABILITY";
        case RIL_REQUEST_START_LCE: return "START_LCE";
        case RIL_REQUEST_STOP_LCE: return "STOP_LCE";
        case RIL_REQUEST_PULL_LCEDATA: return "PULL_LCEDATA";
        case RIL_REQUEST_GET_ACTIVITY_INFO: return "GET_ACTIVITY_INFO";
        case RIL_REQUEST_SET_CARRIER_RESTRICTIONS: return "SET_CARRIER_RESTRICTIONS";
        case RIL_REQUEST_GET_CARRIER_RESTRICTIONS: return "GET_CARRIER_RESTRICTIONS";
        case RIL_REQUEST_SET_CARRIER_INFO_IMSI_ENCRYPTION: return "SET_CARRIER_INFO_IMSI_ENCRYPTION";
        case RIL_REQUEST_SET_SIGNAL_STRENGTH_REPORTING_CRITERIA: return "SET_SIGNAL_STRENGTH_REPORTING_CRITERIA";
        case RIL_REQUEST_SET_LINK_CAPACITY_REPORTING_CRITERIA: return "SET_LINK_CAPACITY_REPORTING_CRITERIA";
        case RIL_REQUEST_ENTER_SIM_DEPERSONALIZATION: return "ENTER_SIM_DEPERSONALIZATION";
        case RIL_REQUEST_CDMA_SEND_SMS_EXPECT_MORE: return "CDMA_SEND_SMS_EXPECT_MORE";
        case RIL_RESPONSE_ACKNOWLEDGEMENT: return "RESPONSE_ACKNOWLEDGEMENT";
        case RIL_UNSOL_RESPONSE_RADIO_STATE_CHANGED: return "UNSOL_RESPONSE_RADIO_STATE_CHANGED";
        case RIL_UNSOL_RESPONSE_CALL_STATE_CHANGED: return "UNSOL_RESPONSE_CALL_STATE_CHANGED";
        case RIL_UNSOL_RESPONSE_VOICE_NETWORK_STATE_CHANGED: return "UNSOL_RESPONSE_VOICE_NETWORK_STATE_CHANGED";
        case RIL_UNSOL_RESPONSE_NEW_SMS: return "UNSOL_RESPONSE_NEW_SMS";
        case RIL_UNSOL_RESPONSE_NEW_SMS_STATUS_REPORT: return "UNSOL_RESPONSE_NEW_SMS_STATUS_REPORT";
        case RIL_UNSOL_RESPONSE_NEW_SMS_ON_SIM: return "UNSOL_RESPONSE_NEW_SMS_ON_SIM";
        case RIL_UNSOL_ON_USSD: return "UNSOL_ON_USSD";
        case RIL_UNSOL_ON_USSD_REQUEST: return "UNSOL_ON_USSD_REQUEST";
        case RIL_UNSOL_NITZ_TIME_RECEIVED: return "UNSOL_NITZ_TIME_RECEIVED";
        case RIL_UNSOL_SIGNAL_STRENGTH: return "UNSOL_SIGNAL_STRENGTH";
        case RIL_UNSOL_DATA_CALL_LIST_CHANGED: return "UNSOL_DATA_CALL_LIST_CHANGED";
        case RIL_UNSOL_SUPP_SVC_NOTIFICATION: return "UNSOL_SUPP_SVC_NOTIFICATION";
        case RIL_UNSOL_STK_SESSION_END: return "UNSOL_STK_SESSION_END";
        case RIL_UNSOL_STK_PROACTIVE_COMMAND: return "UNSOL_STK_PROACTIVE_COMMAND";
        case RIL_UNSOL_STK_EVENT_NOTIFY: return "UNSOL_STK_EVENT_NOTIFY";
        case RIL_UNSOL_STK_CALL_SETUP: return "UNSOL_STK_CALL_SETUP";
        case RIL_UNSOL_SIM_SMS_STORAGE_FULL: return "UNSOL_SIM_SMS_STORAGE_FULL";
        case RIL_UNSOL_SIM_REFRESH: return "UNSOL_SIM_REFRESH";
        case RIL_UNSOL_CALL_RING: return "UNSOL_CALL_RING";
        case RIL_UNSOL_RESPONSE_SIM_STATUS_CHANGED: return "UNSOL_RESPONSE_SIM_STATUS_CHANGED";
        case RIL_UNSOL_RESPONSE_CDMA_NEW_SMS: return "UNSOL_RESPONSE_CDMA_NEW_SMS";
        case RIL_UNSOL_RESPONSE_NEW_BROADCAST_SMS: return "UNSOL_RESPONSE_NEW_BROADCAST_SMS";
        case RIL_UNSOL_CDMA_RUIM_SMS_STORAGE_FULL: return "UNSOL_CDMA_RUIM_SMS_STORAGE_FULL";
        case RIL_UNSOL_RESTRICTED_STATE_CHANGED: return "UNSOL_RESTRICTED_STATE_CHANGED";
        case RIL_UNSOL_ENTER_EMERGENCY_CALLBACK_MODE: return "UNSOL_ENTER_EMERGENCY_CALLBACK_MODE";
        case RIL_UNSOL_CDMA_CALL_WAITING: return "UNSOL_CDMA_CALL_WAITING";
        case RIL_UNSOL_CDMA_OTA_PROVISION_STATUS: return "UNSOL_CDMA_OTA_PROVISION_STATUS";
        case RIL_UNSOL_CDMA_INFO_REC: return "UNSOL_CDMA_INFO_REC";
        case RIL_UNSOL_OEM_HOOK_RAW: return "UNSOL_OEM_HOOK_RAW";
        case RIL_UNSOL_RINGBACK_TONE: return "UNSOL_RINGBACK_TONE";
        case RIL_UNSOL_RESEND_INCALL_MUTE: return "UNSOL_RESEND_INCALL_MUTE";
        case RIL_UNSOL_CDMA_SUBSCRIPTION_SOURCE_CHANGED: return "UNSOL_CDMA_SUBSCRIPTION_SOURCE_CHANGED";
        case RIL_UNSOL_CDMA_PRL_CHANGED: return "UNSOL_CDMA_PRL_CHANGED";
        case RIL_UNSOL_EXIT_EMERGENCY_CALLBACK_MODE: return "UNSOL_EXIT_EMERGENCY_CALLBACK_MODE";
        case RIL_UNSOL_RIL_CONNECTED: return "UNSOL_RIL_CONNECTED";
        case RIL_UNSOL_VOICE_RADIO_TECH_CHANGED: return "UNSOL_VOICE_RADIO_TECH_CHANGED";
        case RIL_UNSOL_CELL_INFO_LIST: return "UNSOL_CELL_INFO_LIST";
        case RIL_UNSOL_RESPONSE_IMS_NETWORK_STATE_CHANGED: return "UNSOL_RESPONSE_IMS_NETWORK_STATE_CHANGED";
        case RIL_UNSOL_UICC_SUBSCRIPTION_STATUS_CHANGED: return "UNSOL_UICC_SUBSCRIPTION_STATUS_CHANGED";
        case RIL_UNSOL_SRVCC_STATE_NOTIFY: return "UNSOL_SRVCC_STATE_NOTIFY";
        case RIL_UNSOL_HARDWARE_CONFIG_CHANGED: return "UNSOL_HARDWARE_CONFIG_CHANGED";
        case RIL_UNSOL_DC_RT_INFO_CHANGED: return "UNSOL_DC_RT_INFO_CHANGED";
        case RIL_UNSOL_RADIO_CAPABILITY: return "UNSOL_RADIO_CAPABILITY";
        case RIL_UNSOL_MODEM_RESTART: return "UNSOL_MODEM_RESTART";
        case RIL_UNSOL_CARRIER_INFO_IMSI_ENCRYPTION: return "UNSOL_CARRIER_INFO_IMSI_ENCRYPTION";
        case RIL_UNSOL_ON_SS: return "UNSOL_ON_SS";
        case RIL_UNSOL_STK_CC_ALPHA_NOTIFY: return "UNSOL_STK_CC_ALPHA_NOTIFY";
        case RIL_UNSOL_LCEDATA_RECV: return "UNSOL_LCEDATA_RECV";
        case RIL_UNSOL_PCO_DATA: return "UNSOL_PCO_DATA";
        default: return "<unknown request>";
    }
}

const char *
rilSocketIdToString(RIL_SOCKET_ID socket_id)
{
    switch(socket_id) {
        case RIL_SOCKET_1:
            return "RIL_SOCKET_1";
#if (SIM_COUNT >= 2)
        case RIL_SOCKET_2:
            return "RIL_SOCKET_2";
#endif
#if (SIM_COUNT >= 3)
        case RIL_SOCKET_3:
            return "RIL_SOCKET_3";
#endif
#if (SIM_COUNT >= 4)
        case RIL_SOCKET_4:
            return "RIL_SOCKET_4";
#endif
        default:
            return "not a valid RIL";
    }
}

} /* namespace android */
