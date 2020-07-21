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

#define __STDC_LIMIT_MACROS
#include <stdint.h>
#define RIL_SHLIB
#include "RilSapSocket.h"
#include "pb_decode.h"
#include "pb_encode.h"
#undef LOG_TAG
#define LOG_TAG "RIL_UIM_SOCKET"
#include <utils/Log.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sap_service.h>
#include <guest/hals/ril/libril/ril.h>

static RilSapSocket::RilSapSocketList *head = NULL;

extern "C" void
RIL_requestTimedCallback (RIL_TimedCallback callback, void *param,
        const struct timeval *relativeTime);

struct RIL_Env RilSapSocket::uimRilEnv = {
        .OnRequestComplete = RilSapSocket::sOnRequestComplete,
        .OnUnsolicitedResponse = RilSapSocket::sOnUnsolicitedResponse,
        .RequestTimedCallback = RIL_requestTimedCallback
};

void RilSapSocket::sOnRequestComplete (RIL_Token t,
        RIL_Errno e,
        void *response,
        size_t responselen) {
    RilSapSocket *sap_socket;
    SapSocketRequest *request = (SapSocketRequest*) t;

    RLOGD("Socket id:%d", request->socketId);

    sap_socket = getSocketById(request->socketId);

    if (sap_socket) {
        sap_socket->onRequestComplete(t,e,response,responselen);
    } else {
        RLOGE("Invalid socket id");
        if (request->curr) {
            free(request->curr);
        }
        free(request);
    }
}

#if defined(ANDROID_MULTI_SIM)
void RilSapSocket::sOnUnsolicitedResponse(int unsolResponse,
        const void *data,
        size_t datalen,
        RIL_SOCKET_ID socketId) {
    RilSapSocket *sap_socket = getSocketById(socketId);
    if (sap_socket) {
        sap_socket->onUnsolicitedResponse(unsolResponse, (void *)data, datalen);
    }
}
#else
void RilSapSocket::sOnUnsolicitedResponse(int unsolResponse,
       const void *data,
       size_t datalen) {
    RilSapSocket *sap_socket = getSocketById(RIL_SOCKET_1);
    if(sap_socket){
        sap_socket->onUnsolicitedResponse(unsolResponse, (void *)data, datalen);
    }
}
#endif

void RilSapSocket::printList() {
    RilSapSocketList *current = head;
    RLOGD("Printing socket list");
    while(NULL != current) {
        RLOGD("SocketName:%s",current->socket->name);
        RLOGD("Socket id:%d",current->socket->id);
        current = current->next;
    }
}

RilSapSocket *RilSapSocket::getSocketById(RIL_SOCKET_ID socketId) {
    RilSapSocket *sap_socket;
    RilSapSocketList *current = head;

    RLOGD("Entered getSocketById");
    printList();

    while(NULL != current) {
        if(socketId == current->socket->id) {
            sap_socket = current->socket;
            return sap_socket;
        }
        current = current->next;
    }
    return NULL;
}

void RilSapSocket::initSapSocket(const char *socketName,
        const RIL_RadioFunctions *uimFuncs) {

    if (strcmp(socketName, RIL1_SERVICE_NAME) == 0) {
        if(!SocketExists(socketName)) {
            addSocketToList(socketName, RIL_SOCKET_1, uimFuncs);
        }
    }

#if (SIM_COUNT >= 2)
    if (strcmp(socketName, RIL2_SERVICE_NAME) == 0) {
        if(!SocketExists(socketName)) {
            addSocketToList(socketName, RIL_SOCKET_2, uimFuncs);
        }
    }
#endif

#if (SIM_COUNT >= 3)
    if (strcmp(socketName, RIL3_SERVICE_NAME) == 0) {
        if(!SocketExists(socketName)) {
            addSocketToList(socketName, RIL_SOCKET_3, uimFuncs);
        }
    }
#endif

#if (SIM_COUNT >= 4)
    if (strcmp(socketName, RIL4_SERVICE_NAME) == 0) {
        if(!SocketExists(socketName)) {
            addSocketToList(socketName, RIL_SOCKET_4, uimFuncs);
        }
    }
#endif
}

void RilSapSocket::addSocketToList(const char *socketName, RIL_SOCKET_ID socketid,
        const RIL_RadioFunctions *uimFuncs) {
    RilSapSocket* socket = NULL;
    RilSapSocketList *current;

    if(!SocketExists(socketName)) {
        socket = new RilSapSocket(socketName, socketid, uimFuncs);
        RilSapSocketList* listItem = (RilSapSocketList*)malloc(sizeof(RilSapSocketList));
        if (!listItem) {
            RLOGE("addSocketToList: OOM");
            delete socket;
            return;
        }
        listItem->socket = socket;
        listItem->next = NULL;

        RLOGD("Adding socket with id: %d", socket->id);

        if(NULL == head) {
            head = listItem;
            head->next = NULL;
        }
        else {
            current = head;
            while(NULL != current->next) {
                current = current->next;
            }
            current->next = listItem;
        }
    }
}

bool RilSapSocket::SocketExists(const char *socketName) {
    RilSapSocketList* current = head;

    while(NULL != current) {
        if(strcmp(current->socket->name, socketName) == 0) {
            return true;
        }
        current = current->next;
    }
    return false;
}

RilSapSocket::RilSapSocket(const char *socketName,
        RIL_SOCKET_ID socketId,
        const RIL_RadioFunctions *inputUimFuncs):
        RilSocket(socketName, socketId) {
    if (inputUimFuncs) {
        uimFuncs = inputUimFuncs;
    }
}

void RilSapSocket::dispatchRequest(MsgHeader *req) {
    // SapSocketRequest will be deallocated in onRequestComplete()
    SapSocketRequest* currRequest=(SapSocketRequest*)malloc(sizeof(SapSocketRequest));
    if (!currRequest) {
        RLOGE("dispatchRequest: OOM");
        // Free MsgHeader allocated in pushRecord()
        free(req);
        return;
    }
    currRequest->token = req->token;
    currRequest->curr = req;
    currRequest->p_next = NULL;
    currRequest->socketId = id;

    pendingResponseQueue.enqueue(currRequest);

    if (uimFuncs) {
        RLOGI("RilSapSocket::dispatchRequest [%d] > SAP REQUEST type: %d. id: %d. error: %d, \
                token 0x%p",
                req->token,
                req->type,
                req->id,
                req->error,
                currRequest );

#if defined(ANDROID_MULTI_SIM)
        uimFuncs->onRequest(req->id, req->payload->bytes, req->payload->size, currRequest, id);
#else
        uimFuncs->onRequest(req->id, req->payload->bytes, req->payload->size, currRequest);
#endif
    }
}

void RilSapSocket::onRequestComplete(RIL_Token t, RIL_Errno e, void *response,
        size_t response_len) {
    SapSocketRequest* request= (SapSocketRequest*)t;

    if (!request || !request->curr) {
        RLOGE("RilSapSocket::onRequestComplete: request/request->curr is NULL");
        return;
    }

    MsgHeader *hdr = request->curr;

    MsgHeader rsp;
    rsp.token = request->curr->token;
    rsp.type = MsgType_RESPONSE;
    rsp.id = request->curr->id;
    rsp.error = (Error)e;
    rsp.payload = (pb_bytes_array_t *)calloc(1, sizeof(pb_bytes_array_t) + response_len);
    if (!rsp.payload) {
        RLOGE("onRequestComplete: OOM");
    } else {
        if (response && response_len > 0) {
            memcpy(rsp.payload->bytes, response, response_len);
            rsp.payload->size = response_len;
        } else {
            rsp.payload->size = 0;
        }

        RLOGE("RilSapSocket::onRequestComplete: Token:%d, MessageId:%d ril token 0x%p",
                hdr->token, hdr->id, t);

        sap::processResponse(&rsp, this);
        free(rsp.payload);
    }

    // Deallocate SapSocketRequest
    if(!pendingResponseQueue.checkAndDequeue(hdr->id, hdr->token)) {
        RLOGE("Token:%d, MessageId:%d", hdr->token, hdr->id);
        RLOGE ("RilSapSocket::onRequestComplete: invalid Token or Message Id");
    }

    // Deallocate MsgHeader
    free(hdr);
}

void RilSapSocket::onUnsolicitedResponse(int unsolResponse, void *data, size_t datalen) {
    if (data && datalen > 0) {
        pb_bytes_array_t *payload = (pb_bytes_array_t *)calloc(1,
                sizeof(pb_bytes_array_t) + datalen);
        if (!payload) {
            RLOGE("onUnsolicitedResponse: OOM");
            return;
        }
        memcpy(payload->bytes, data, datalen);
        payload->size = datalen;
        MsgHeader rsp;
        rsp.payload = payload;
        rsp.type = MsgType_UNSOL_RESPONSE;
        rsp.id = (MsgId)unsolResponse;
        rsp.error = Error_RIL_E_SUCCESS;
        sap::processUnsolResponse(&rsp, this);
        free(payload);
    }
}
