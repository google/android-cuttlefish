/*
 * Copyright (c) 2016 The Android Open Source Project
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

#define LOG_TAG "RIL_SAP"

#include <android/hardware/radio/1.1/ISap.h>

#include <hwbinder/IPCThreadState.h>
#include <hwbinder/ProcessState.h>
#include <sap_service.h>
#include "pb_decode.h"
#include "pb_encode.h"

using namespace android::hardware::radio::V1_0;
using ::android::hardware::Return;
using ::android::hardware::hidl_vec;
using ::android::hardware::hidl_array;
using ::android::hardware::Void;
using android::CommandInfo;
using android::RequestInfo;
using android::requestToString;
using android::sp;

struct SapImpl;

#if (SIM_COUNT >= 2)
sp<SapImpl> sapService[SIM_COUNT];
#else
sp<SapImpl> sapService[1];
#endif

struct SapImpl : public android::hardware::radio::V1_1::ISap {
    int32_t slotId;
    sp<ISapCallback> sapCallback;
    RIL_SOCKET_ID rilSocketId;

    Return<void> setCallback(const ::android::sp<ISapCallback>& sapCallbackParam);

    Return<void> connectReq(int32_t token, int32_t maxMsgSize);

    Return<void> disconnectReq(int32_t token);

    Return<void> apduReq(int32_t token, SapApduType type, const hidl_vec<uint8_t>& command);

    Return<void> transferAtrReq(int32_t token);

    Return<void> powerReq(int32_t token, bool state);

    Return<void> resetSimReq(int32_t token);

    Return<void> transferCardReaderStatusReq(int32_t token);

    Return<void> setTransferProtocolReq(int32_t token, SapTransferProtocol transferProtocol);

    MsgHeader* createMsgHeader(MsgId msgId, int32_t token);

    Return<void> addPayloadAndDispatchRequest(MsgHeader *msg, uint16_t reqLen, uint8_t *reqPtr);

    void sendFailedResponse(MsgId msgId, int32_t token, int numPointers, ...);

    void checkReturnStatus(Return<void>& ret);
};

void SapImpl::checkReturnStatus(Return<void>& ret) {
    if (ret.isOk() == false) {
        RLOGE("checkReturnStatus: unable to call response/indication callback: %s",
                ret.description().c_str());
        // Remote process (SapRilReceiver.java) hosting the callback must be dead. Reset the
        // callback object; there's no other recovery to be done here. When the client process is
        // back up, it will call setCallback()
        sapCallback = NULL;
    }
}

Return<void> SapImpl::setCallback(const ::android::sp<ISapCallback>& sapCallbackParam) {
    RLOGD("SapImpl::setCallback for slotId %d", slotId);
    sapCallback = sapCallbackParam;
    return Void();
}

MsgHeader* SapImpl::createMsgHeader(MsgId msgId, int32_t token) {
    // Memory for msg will be freed by RilSapSocket::onRequestComplete()
    MsgHeader *msg = (MsgHeader *)calloc(1, sizeof(MsgHeader));
    if (msg == NULL) {
        return NULL;
    }
    msg->token = token;
    msg->type = MsgType_REQUEST;
    msg->id = msgId;
    msg->error = Error_RIL_E_SUCCESS;
    return msg;
}

Return<void> SapImpl::addPayloadAndDispatchRequest(MsgHeader *msg, uint16_t reqLen,
        uint8_t *reqPtr) {
    pb_bytes_array_t *payload = (pb_bytes_array_t *) malloc(sizeof(pb_bytes_array_t) - 1 + reqLen);
    if (payload == NULL) {
        sendFailedResponse(msg->id, msg->token, 2, reqPtr, msg);
        return Void();
    }

    msg->payload = payload;
    msg->payload->size = reqLen;
    memcpy(msg->payload->bytes, reqPtr, reqLen);

    RilSapSocket *sapSocket = RilSapSocket::getSocketById(rilSocketId);
    if (sapSocket) {
        RLOGD("SapImpl::addPayloadAndDispatchRequest: calling dispatchRequest");
        sapSocket->dispatchRequest(msg);
    } else {
        RLOGE("SapImpl::addPayloadAndDispatchRequest: sapSocket is null");
        sendFailedResponse(msg->id, msg->token, 3, payload, reqPtr, msg);
        return Void();
    }
    free(msg->payload);
    free(reqPtr);
    return Void();
}

void SapImpl::sendFailedResponse(MsgId msgId, int32_t token, int numPointers, ...) {
    va_list ap;
    va_start(ap, numPointers);
    for (int i = 0; i < numPointers; i++) {
        void *ptr = va_arg(ap, void *);
        if (ptr) free(ptr);
    }
    va_end(ap);
    Return<void> retStatus;
    switch(msgId) {
        case MsgId_RIL_SIM_SAP_CONNECT:
            retStatus = sapCallback->connectResponse(token, SapConnectRsp::CONNECT_FAILURE, 0);
            break;

        case MsgId_RIL_SIM_SAP_DISCONNECT:
            retStatus = sapCallback->disconnectResponse(token);
            break;

        case MsgId_RIL_SIM_SAP_APDU: {
            hidl_vec<uint8_t> apduRsp;
            retStatus = sapCallback->apduResponse(token, SapResultCode::GENERIC_FAILURE, apduRsp);
            break;
        }

        case MsgId_RIL_SIM_SAP_TRANSFER_ATR: {
            hidl_vec<uint8_t> atr;
            retStatus = sapCallback->transferAtrResponse(token, SapResultCode::GENERIC_FAILURE,
                    atr);
            break;
        }

        case MsgId_RIL_SIM_SAP_POWER:
            retStatus = sapCallback->powerResponse(token, SapResultCode::GENERIC_FAILURE);
            break;

        case MsgId_RIL_SIM_SAP_RESET_SIM:
            retStatus = sapCallback->resetSimResponse(token, SapResultCode::GENERIC_FAILURE);
            break;

        case MsgId_RIL_SIM_SAP_TRANSFER_CARD_READER_STATUS:
            retStatus = sapCallback->transferCardReaderStatusResponse(token,
                    SapResultCode::GENERIC_FAILURE, 0);
            break;

        case MsgId_RIL_SIM_SAP_SET_TRANSFER_PROTOCOL:
            retStatus = sapCallback->transferProtocolResponse(token, SapResultCode::NOT_SUPPORTED);
            break;

        default:
            return;
    }
    sapService[slotId]->checkReturnStatus(retStatus);
}

Return<void> SapImpl::connectReq(int32_t token, int32_t maxMsgSize) {
    RLOGD("SapImpl::connectReq");
    MsgHeader *msg = createMsgHeader(MsgId_RIL_SIM_SAP_CONNECT, token);
    if (msg == NULL) {
        RLOGE("SapImpl::connectReq: Error allocating memory for msg");
        sendFailedResponse(MsgId_RIL_SIM_SAP_CONNECT, token, 0);
        return Void();
    }

    /***** Encode RIL_SIM_SAP_CONNECT_REQ *****/
    RIL_SIM_SAP_CONNECT_REQ req;
    memset(&req, 0, sizeof(RIL_SIM_SAP_CONNECT_REQ));
    req.max_message_size = maxMsgSize;

    size_t encodedSize = 0;
    if (!pb_get_encoded_size(&encodedSize, RIL_SIM_SAP_CONNECT_REQ_fields, &req)) {
        RLOGE("SapImpl::connectReq: Error getting encoded size for RIL_SIM_SAP_CONNECT_REQ");
        sendFailedResponse(MsgId_RIL_SIM_SAP_CONNECT, token, 1, msg);
        return Void();
    }

    uint8_t *buffer = (uint8_t *)calloc(1, encodedSize);
    if (buffer == NULL) {
        RLOGE("SapImpl::connectReq: Error allocating memory for buffer");
        sendFailedResponse(MsgId_RIL_SIM_SAP_CONNECT, token, 1, msg);
        return Void();
    }
    pb_ostream_t stream = pb_ostream_from_buffer(buffer, encodedSize);

    RLOGD("SapImpl::connectReq calling pb_encode");
    if (!pb_encode(&stream, RIL_SIM_SAP_CONNECT_REQ_fields, &req)) {
        RLOGE("SapImpl::connectReq: Error encoding RIL_SIM_SAP_CONNECT_REQ");
        sendFailedResponse(MsgId_RIL_SIM_SAP_CONNECT, token, 2, buffer, msg);
        return Void();
    }
    /***** Encode RIL_SIM_SAP_CONNECT_REQ done *****/

    /* encoded req is payload */
    return addPayloadAndDispatchRequest(msg, stream.bytes_written, buffer);
}

Return<void> SapImpl::disconnectReq(int32_t token) {
    RLOGD("SapImpl::disconnectReq");
    MsgHeader *msg = createMsgHeader(MsgId_RIL_SIM_SAP_DISCONNECT, token);
    if (msg == NULL) {
        RLOGE("SapImpl::disconnectReq: Error allocating memory for msg");
        sendFailedResponse(MsgId_RIL_SIM_SAP_DISCONNECT, token, 0);
        return Void();
    }

    /***** Encode RIL_SIM_SAP_DISCONNECT_REQ *****/
    RIL_SIM_SAP_DISCONNECT_REQ req;
    memset(&req, 0, sizeof(RIL_SIM_SAP_DISCONNECT_REQ));

    size_t encodedSize = 0;
    if (!pb_get_encoded_size(&encodedSize, RIL_SIM_SAP_DISCONNECT_REQ_fields, &req)) {
        RLOGE("SapImpl::disconnectReq: Error getting encoded size for RIL_SIM_SAP_DISCONNECT_REQ");
        sendFailedResponse(MsgId_RIL_SIM_SAP_DISCONNECT, token, 1, msg);
        return Void();
    }

    uint8_t *buffer = (uint8_t *)calloc(1, encodedSize);
    if (buffer == NULL) {
        RLOGE("SapImpl::disconnectReq: Error allocating memory for buffer");
        sendFailedResponse(MsgId_RIL_SIM_SAP_DISCONNECT, token, 1, msg);
        return Void();
    }

    pb_ostream_t stream = pb_ostream_from_buffer(buffer, encodedSize);

    RLOGD("SapImpl::disconnectReq calling pb_encode");
    if (!pb_encode(&stream, RIL_SIM_SAP_DISCONNECT_REQ_fields, &req)) {
        RLOGE("SapImpl::disconnectReq: Error encoding RIL_SIM_SAP_DISCONNECT_REQ");
        sendFailedResponse(MsgId_RIL_SIM_SAP_DISCONNECT, token, 2, buffer, msg);
        return Void();
    }
    /***** Encode RIL_SIM_SAP_DISCONNECT_REQ done *****/

    /* encoded req is payload */
    return addPayloadAndDispatchRequest(msg, stream.bytes_written, buffer);
}

Return<void> SapImpl::apduReq(int32_t token, SapApduType type, const hidl_vec<uint8_t>& command) {
    RLOGD("SapImpl::apduReq");
    MsgHeader *msg = createMsgHeader(MsgId_RIL_SIM_SAP_APDU, token);
    if (msg == NULL) {
        RLOGE("SapImpl::apduReq: Error allocating memory for msg");
        sendFailedResponse(MsgId_RIL_SIM_SAP_APDU, token, 0);
        return Void();
    }

    /***** Encode RIL_SIM_SAP_APDU_REQ *****/
    RIL_SIM_SAP_APDU_REQ req;
    memset(&req, 0, sizeof(RIL_SIM_SAP_APDU_REQ));
    req.type = (RIL_SIM_SAP_APDU_REQ_Type)type;

    if (command.size() > 0) {
        req.command = (pb_bytes_array_t *)malloc(sizeof(pb_bytes_array_t) - 1 + command.size());
        if (req.command == NULL) {
            RLOGE("SapImpl::apduReq: Error allocating memory for req.command");
            sendFailedResponse(MsgId_RIL_SIM_SAP_APDU, token, 1, msg);
            return Void();
        }
        req.command->size = command.size();
        memcpy(req.command->bytes, command.data(), command.size());
    }

    size_t encodedSize = 0;
    if (!pb_get_encoded_size(&encodedSize, RIL_SIM_SAP_APDU_REQ_fields, &req)) {
        RLOGE("SapImpl::apduReq: Error getting encoded size for RIL_SIM_SAP_APDU_REQ");
        sendFailedResponse(MsgId_RIL_SIM_SAP_APDU, token, 2, req.command, msg);
        return Void();
    }

    uint8_t *buffer = (uint8_t *)calloc(1, encodedSize);
    if (buffer == NULL) {
        RLOGE("SapImpl::apduReq: Error allocating memory for buffer");
        sendFailedResponse(MsgId_RIL_SIM_SAP_APDU, token, 2, req.command, msg);
        return Void();
    }

    pb_ostream_t stream = pb_ostream_from_buffer(buffer, encodedSize);

    RLOGD("SapImpl::apduReq calling pb_encode");
    if (!pb_encode(&stream, RIL_SIM_SAP_APDU_REQ_fields, &req)) {
        RLOGE("SapImpl::apduReq: Error encoding RIL_SIM_SAP_APDU_REQ");
        sendFailedResponse(MsgId_RIL_SIM_SAP_APDU, token, 3, req.command, buffer, msg);
        return Void();
    }
    /***** Encode RIL_SIM_SAP_APDU_REQ done *****/

    /* encoded req is payload */
    return addPayloadAndDispatchRequest(msg, stream.bytes_written, buffer);
}

Return<void> SapImpl::transferAtrReq(int32_t token) {
    RLOGD("SapImpl::transferAtrReq");
    MsgHeader *msg = createMsgHeader(MsgId_RIL_SIM_SAP_TRANSFER_ATR, token);
    if (msg == NULL) {
        RLOGE("SapImpl::transferAtrReq: Error allocating memory for msg");
        sendFailedResponse(MsgId_RIL_SIM_SAP_TRANSFER_ATR, token, 0);
        return Void();
    }

    /***** Encode RIL_SIM_SAP_TRANSFER_ATR_REQ *****/
    RIL_SIM_SAP_TRANSFER_ATR_REQ req;
    memset(&req, 0, sizeof(RIL_SIM_SAP_TRANSFER_ATR_REQ));

    size_t encodedSize = 0;
    if (!pb_get_encoded_size(&encodedSize, RIL_SIM_SAP_TRANSFER_ATR_REQ_fields, &req)) {
        RLOGE("SapImpl::transferAtrReq: Error getting encoded size for "
                "RIL_SIM_SAP_TRANSFER_ATR_REQ");
        sendFailedResponse(MsgId_RIL_SIM_SAP_TRANSFER_ATR, token, 1, msg);
        return Void();
    }

    uint8_t *buffer = (uint8_t *)calloc(1, encodedSize);
    if (buffer == NULL) {
        RLOGE("SapImpl::transferAtrReq: Error allocating memory for buffer");
        sendFailedResponse(MsgId_RIL_SIM_SAP_TRANSFER_ATR, token, 1, msg);
        return Void();
    }

    pb_ostream_t stream = pb_ostream_from_buffer(buffer, encodedSize);

    RLOGD("SapImpl::transferAtrReq calling pb_encode");
    if (!pb_encode(&stream, RIL_SIM_SAP_TRANSFER_ATR_REQ_fields, &req)) {
        RLOGE("SapImpl::transferAtrReq: Error encoding RIL_SIM_SAP_TRANSFER_ATR_REQ");
        sendFailedResponse(MsgId_RIL_SIM_SAP_TRANSFER_ATR, token, 2, buffer, msg);
        return Void();
    }
    /***** Encode RIL_SIM_SAP_TRANSFER_ATR_REQ done *****/

    /* encoded req is payload */
    return addPayloadAndDispatchRequest(msg, stream.bytes_written, buffer);
}

Return<void> SapImpl::powerReq(int32_t token, bool state) {
    RLOGD("SapImpl::powerReq");
    MsgHeader *msg = createMsgHeader(MsgId_RIL_SIM_SAP_POWER, token);
    if (msg == NULL) {
        RLOGE("SapImpl::powerReq: Error allocating memory for msg");
        sendFailedResponse(MsgId_RIL_SIM_SAP_POWER, token, 0);
        return Void();
    }

    /***** Encode RIL_SIM_SAP_POWER_REQ *****/
    RIL_SIM_SAP_POWER_REQ req;
    memset(&req, 0, sizeof(RIL_SIM_SAP_POWER_REQ));
    req.state = state;

    size_t encodedSize = 0;
    if (!pb_get_encoded_size(&encodedSize, RIL_SIM_SAP_POWER_REQ_fields, &req)) {
        RLOGE("SapImpl::powerReq: Error getting encoded size for RIL_SIM_SAP_POWER_REQ");
        sendFailedResponse(MsgId_RIL_SIM_SAP_POWER, token, 1, msg);
        return Void();
    }

    uint8_t *buffer = (uint8_t *)calloc(1, encodedSize);
    if (buffer == NULL) {
        RLOGE("SapImpl::powerReq: Error allocating memory for buffer");
        sendFailedResponse(MsgId_RIL_SIM_SAP_POWER, token, 1, msg);
        return Void();
    }

    pb_ostream_t stream = pb_ostream_from_buffer(buffer, encodedSize);

    RLOGD("SapImpl::powerReq calling pb_encode");
    if (!pb_encode(&stream, RIL_SIM_SAP_POWER_REQ_fields, &req)) {
        RLOGE("SapImpl::powerReq: Error encoding RIL_SIM_SAP_POWER_REQ");
        sendFailedResponse(MsgId_RIL_SIM_SAP_POWER, token, 2, buffer, msg);
        return Void();
    }
    /***** Encode RIL_SIM_SAP_POWER_REQ done *****/

    /* encoded req is payload */
    return addPayloadAndDispatchRequest(msg, stream.bytes_written, buffer);
}

Return<void> SapImpl::resetSimReq(int32_t token) {
    RLOGD("SapImpl::resetSimReq");
    MsgHeader *msg = createMsgHeader(MsgId_RIL_SIM_SAP_RESET_SIM, token);
    if (msg == NULL) {
        RLOGE("SapImpl::resetSimReq: Error allocating memory for msg");
        sendFailedResponse(MsgId_RIL_SIM_SAP_RESET_SIM, token, 0);
        return Void();
    }

    /***** Encode RIL_SIM_SAP_RESET_SIM_REQ *****/
    RIL_SIM_SAP_RESET_SIM_REQ req;
    memset(&req, 0, sizeof(RIL_SIM_SAP_RESET_SIM_REQ));

    size_t encodedSize = 0;
    if (!pb_get_encoded_size(&encodedSize, RIL_SIM_SAP_RESET_SIM_REQ_fields, &req)) {
        RLOGE("SapImpl::resetSimReq: Error getting encoded size for RIL_SIM_SAP_RESET_SIM_REQ");
        sendFailedResponse(MsgId_RIL_SIM_SAP_RESET_SIM, token, 1, msg);
        return Void();
    }

    uint8_t *buffer = (uint8_t *)calloc(1, encodedSize);
    if (buffer == NULL) {
        RLOGE("SapImpl::resetSimReq: Error allocating memory for buffer");
        sendFailedResponse(MsgId_RIL_SIM_SAP_RESET_SIM, token, 1, msg);
        return Void();
    }

    pb_ostream_t stream = pb_ostream_from_buffer(buffer, encodedSize);

    RLOGD("SapImpl::resetSimReq calling pb_encode");
    if (!pb_encode(&stream, RIL_SIM_SAP_RESET_SIM_REQ_fields, &req)) {
        RLOGE("SapImpl::resetSimReq: Error encoding RIL_SIM_SAP_RESET_SIM_REQ");
        sendFailedResponse(MsgId_RIL_SIM_SAP_RESET_SIM, token, 2, buffer, msg);
        return Void();
    }
    /***** Encode RIL_SIM_SAP_RESET_SIM_REQ done *****/

    /* encoded req is payload */
    return addPayloadAndDispatchRequest(msg, stream.bytes_written, buffer);
}

Return<void> SapImpl::transferCardReaderStatusReq(int32_t token) {
    RLOGD("SapImpl::transferCardReaderStatusReq");
    MsgHeader *msg = createMsgHeader(MsgId_RIL_SIM_SAP_TRANSFER_CARD_READER_STATUS, token);
    if (msg == NULL) {
        RLOGE("SapImpl::transferCardReaderStatusReq: Error allocating memory for msg");
        sendFailedResponse(MsgId_RIL_SIM_SAP_TRANSFER_CARD_READER_STATUS, token, 0);
        return Void();
    }

    /***** Encode RIL_SIM_SAP_TRANSFER_CARD_READER_STATUS_REQ *****/
    RIL_SIM_SAP_TRANSFER_CARD_READER_STATUS_REQ req;
    memset(&req, 0, sizeof(RIL_SIM_SAP_TRANSFER_CARD_READER_STATUS_REQ));

    size_t encodedSize = 0;
    if (!pb_get_encoded_size(&encodedSize, RIL_SIM_SAP_TRANSFER_CARD_READER_STATUS_REQ_fields,
            &req)) {
        RLOGE("SapImpl::transferCardReaderStatusReq: Error getting encoded size for "
                "RIL_SIM_SAP_TRANSFER_CARD_READER_STATUS_REQ");
        sendFailedResponse(MsgId_RIL_SIM_SAP_TRANSFER_CARD_READER_STATUS, token, 1, msg);
        return Void();
    }

    uint8_t *buffer = (uint8_t *)calloc(1, encodedSize);
    if (buffer == NULL) {
        RLOGE("SapImpl::transferCardReaderStatusReq: Error allocating memory for buffer");
        sendFailedResponse(MsgId_RIL_SIM_SAP_TRANSFER_CARD_READER_STATUS, token, 1, msg);
        return Void();
    }

    pb_ostream_t stream = pb_ostream_from_buffer(buffer, encodedSize);

    RLOGD("SapImpl::transferCardReaderStatusReq calling pb_encode");
    if (!pb_encode(&stream, RIL_SIM_SAP_TRANSFER_CARD_READER_STATUS_REQ_fields, &req)) {
        RLOGE("SapImpl::transferCardReaderStatusReq: Error encoding "
                "RIL_SIM_SAP_TRANSFER_CARD_READER_STATUS_REQ");
        sendFailedResponse(MsgId_RIL_SIM_SAP_TRANSFER_CARD_READER_STATUS, token, 2, buffer, msg);
        return Void();
    }
    /***** Encode RIL_SIM_SAP_TRANSFER_CARD_READER_STATUS_REQ done *****/

    /* encoded req is payload */
    return addPayloadAndDispatchRequest(msg, stream.bytes_written, buffer);
}

Return<void> SapImpl::setTransferProtocolReq(int32_t token, SapTransferProtocol transferProtocol) {
    RLOGD("SapImpl::setTransferProtocolReq");
    MsgHeader *msg = createMsgHeader(MsgId_RIL_SIM_SAP_SET_TRANSFER_PROTOCOL, token);
    if (msg == NULL) {
        RLOGE("SapImpl::setTransferProtocolReq: Error allocating memory for msg");
        sendFailedResponse(MsgId_RIL_SIM_SAP_SET_TRANSFER_PROTOCOL, token, 0);
        return Void();
    }

    /***** Encode RIL_SIM_SAP_SET_TRANSFER_PROTOCOL_REQ *****/
    RIL_SIM_SAP_SET_TRANSFER_PROTOCOL_REQ req;
    memset(&req, 0, sizeof(RIL_SIM_SAP_SET_TRANSFER_PROTOCOL_REQ));
    req.protocol = (RIL_SIM_SAP_SET_TRANSFER_PROTOCOL_REQ_Protocol)transferProtocol;

    size_t encodedSize = 0;
    if (!pb_get_encoded_size(&encodedSize, RIL_SIM_SAP_SET_TRANSFER_PROTOCOL_REQ_fields, &req)) {
        RLOGE("SapImpl::setTransferProtocolReq: Error getting encoded size for "
                "RIL_SIM_SAP_SET_TRANSFER_PROTOCOL_REQ");
        sendFailedResponse(MsgId_RIL_SIM_SAP_SET_TRANSFER_PROTOCOL, token, 1, msg);
        return Void();
    }

    uint8_t *buffer = (uint8_t *)calloc(1, encodedSize);
    if (buffer == NULL) {
        RLOGE("SapImpl::setTransferProtocolReq: Error allocating memory for buffer");
        sendFailedResponse(MsgId_RIL_SIM_SAP_SET_TRANSFER_PROTOCOL, token, 1, msg);
        return Void();
    }

    pb_ostream_t stream = pb_ostream_from_buffer(buffer, encodedSize);

    RLOGD("SapImpl::setTransferProtocolReq calling pb_encode");
    if (!pb_encode(&stream, RIL_SIM_SAP_SET_TRANSFER_PROTOCOL_REQ_fields, &req)) {
        RLOGE("SapImpl::setTransferProtocolReq: Error encoding "
                "RIL_SIM_SAP_SET_TRANSFER_PROTOCOL_REQ");
        sendFailedResponse(MsgId_RIL_SIM_SAP_SET_TRANSFER_PROTOCOL, token, 2, buffer, msg);
        return Void();
    }
    /***** Encode RIL_SIM_SAP_SET_TRANSFER_PROTOCOL_REQ done *****/

    /* encoded req is payload */
    return addPayloadAndDispatchRequest(msg, stream.bytes_written, buffer);
}

void *sapDecodeMessage(MsgId msgId, MsgType msgType, uint8_t *payloadPtr, size_t payloadLen) {
    void *responsePtr = NULL;
    pb_istream_t stream;

    /* Create the stream */
    stream = pb_istream_from_buffer((uint8_t *)payloadPtr, payloadLen);

    /* Decode based on the message id */
    switch (msgId)
    {
        case MsgId_RIL_SIM_SAP_CONNECT:
            responsePtr = malloc(sizeof(RIL_SIM_SAP_CONNECT_RSP));
            if (responsePtr) {
                if (!pb_decode(&stream, RIL_SIM_SAP_CONNECT_RSP_fields, responsePtr)) {
                    RLOGE("Error decoding RIL_SIM_SAP_CONNECT_RSP");
                    return NULL;
                }
            }
            break;

        case MsgId_RIL_SIM_SAP_DISCONNECT:
            if (msgType == MsgType_RESPONSE) {
                responsePtr = malloc(sizeof(RIL_SIM_SAP_DISCONNECT_RSP));
                if (responsePtr) {
                    if (!pb_decode(&stream, RIL_SIM_SAP_DISCONNECT_RSP_fields, responsePtr)) {
                        RLOGE("Error decoding RIL_SIM_SAP_DISCONNECT_RSP");
                        return NULL;
                    }
                }
            } else {
                responsePtr = malloc(sizeof(RIL_SIM_SAP_DISCONNECT_IND));
                if (responsePtr) {
                    if (!pb_decode(&stream, RIL_SIM_SAP_DISCONNECT_IND_fields, responsePtr)) {
                        RLOGE("Error decoding RIL_SIM_SAP_DISCONNECT_IND");
                        return NULL;
                    }
                }
            }
            break;

        case MsgId_RIL_SIM_SAP_APDU:
            responsePtr = malloc(sizeof(RIL_SIM_SAP_APDU_RSP));
            if (responsePtr) {
                if (!pb_decode(&stream, RIL_SIM_SAP_APDU_RSP_fields, responsePtr)) {
                    RLOGE("Error decoding RIL_SIM_SAP_APDU_RSP");
                    return NULL;
                }
            }
            break;

        case MsgId_RIL_SIM_SAP_TRANSFER_ATR:
            responsePtr = malloc(sizeof(RIL_SIM_SAP_TRANSFER_ATR_RSP));
            if (responsePtr) {
                if (!pb_decode(&stream, RIL_SIM_SAP_TRANSFER_ATR_RSP_fields, responsePtr)) {
                    RLOGE("Error decoding RIL_SIM_SAP_TRANSFER_ATR_RSP");
                    return NULL;
                }
            }
            break;

        case MsgId_RIL_SIM_SAP_POWER:
            responsePtr = malloc(sizeof(RIL_SIM_SAP_POWER_RSP));
            if (responsePtr) {
                if (!pb_decode(&stream, RIL_SIM_SAP_POWER_RSP_fields, responsePtr)) {
                    RLOGE("Error decoding RIL_SIM_SAP_POWER_RSP");
                    return NULL;
                }
            }
            break;

        case MsgId_RIL_SIM_SAP_RESET_SIM:
            responsePtr = malloc(sizeof(RIL_SIM_SAP_RESET_SIM_RSP));
            if (responsePtr) {
                if (!pb_decode(&stream, RIL_SIM_SAP_RESET_SIM_RSP_fields, responsePtr)) {
                    RLOGE("Error decoding RIL_SIM_SAP_RESET_SIM_RSP");
                    return NULL;
                }
            }
            break;

        case MsgId_RIL_SIM_SAP_STATUS:
            responsePtr = malloc(sizeof(RIL_SIM_SAP_STATUS_IND));
            if (responsePtr) {
                if (!pb_decode(&stream, RIL_SIM_SAP_STATUS_IND_fields, responsePtr)) {
                    RLOGE("Error decoding RIL_SIM_SAP_STATUS_IND");
                    return NULL;
                }
            }
            break;

        case MsgId_RIL_SIM_SAP_TRANSFER_CARD_READER_STATUS:
            responsePtr = malloc(sizeof(RIL_SIM_SAP_TRANSFER_CARD_READER_STATUS_RSP));
            if (responsePtr) {
                if (!pb_decode(&stream, RIL_SIM_SAP_TRANSFER_CARD_READER_STATUS_RSP_fields,
                        responsePtr)) {
                    RLOGE("Error decoding RIL_SIM_SAP_TRANSFER_CARD_READER_STATUS_RSP");
                    return NULL;
                }
            }
            break;

        case MsgId_RIL_SIM_SAP_ERROR_RESP:
            responsePtr = malloc(sizeof(RIL_SIM_SAP_ERROR_RSP));
            if (responsePtr) {
                if (!pb_decode(&stream, RIL_SIM_SAP_ERROR_RSP_fields, responsePtr)) {
                    RLOGE("Error decoding RIL_SIM_SAP_ERROR_RSP");
                    return NULL;
                }
            }
            break;

        case MsgId_RIL_SIM_SAP_SET_TRANSFER_PROTOCOL:
            responsePtr = malloc(sizeof(RIL_SIM_SAP_SET_TRANSFER_PROTOCOL_RSP));
            if (responsePtr) {
                if (!pb_decode(&stream, RIL_SIM_SAP_SET_TRANSFER_PROTOCOL_RSP_fields,
                        responsePtr)) {
                    RLOGE("Error decoding RIL_SIM_SAP_SET_TRANSFER_PROTOCOL_RSP");
                    return NULL;
                }
            }
            break;

        default:
            break;
    }
    return responsePtr;
} /* sapDecodeMessage */

sp<SapImpl> getSapImpl(RilSapSocket *sapSocket) {
    switch (sapSocket->getSocketId()) {
        case RIL_SOCKET_1:
            RLOGD("getSapImpl: returning sapService[0]");
            return sapService[0];
        #if (SIM_COUNT >= 2)
        case RIL_SOCKET_2:
            return sapService[1];
        #if (SIM_COUNT >= 3)
        case RIL_SOCKET_3:
            return sapService[2];
        #if (SIM_COUNT >= 4)
        case RIL_SOCKET_4:
            return sapService[3];
        #endif
        #endif
        #endif
        default:
            return NULL;
    }
}

SapResultCode convertApduResponseProtoToHal(RIL_SIM_SAP_APDU_RSP_Response responseProto) {
    switch(responseProto) {
        case RIL_SIM_SAP_APDU_RSP_Response_RIL_E_SUCCESS:
            return SapResultCode::SUCCESS;
        case RIL_SIM_SAP_APDU_RSP_Response_RIL_E_GENERIC_FAILURE:
            return SapResultCode::GENERIC_FAILURE;
        case RIL_SIM_SAP_APDU_RSP_Response_RIL_E_SIM_NOT_READY:
            return SapResultCode::CARD_NOT_ACCESSSIBLE;
        case RIL_SIM_SAP_APDU_RSP_Response_RIL_E_SIM_ALREADY_POWERED_OFF:
            return SapResultCode::CARD_ALREADY_POWERED_OFF;
        case RIL_SIM_SAP_APDU_RSP_Response_RIL_E_SIM_ABSENT:
            return SapResultCode::CARD_REMOVED;
        default:
            return SapResultCode::GENERIC_FAILURE;
    }
}

SapResultCode convertTransferAtrResponseProtoToHal(
        RIL_SIM_SAP_TRANSFER_ATR_RSP_Response responseProto) {
    switch(responseProto) {
        case RIL_SIM_SAP_TRANSFER_ATR_RSP_Response_RIL_E_SUCCESS:
            return SapResultCode::SUCCESS;
        case RIL_SIM_SAP_TRANSFER_ATR_RSP_Response_RIL_E_GENERIC_FAILURE:
            return SapResultCode::GENERIC_FAILURE;
        case RIL_SIM_SAP_TRANSFER_ATR_RSP_Response_RIL_E_SIM_ALREADY_POWERED_OFF:
            return SapResultCode::CARD_ALREADY_POWERED_OFF;
        case RIL_SIM_SAP_TRANSFER_ATR_RSP_Response_RIL_E_SIM_ABSENT:
            return SapResultCode::CARD_REMOVED;
        case RIL_SIM_SAP_TRANSFER_ATR_RSP_Response_RIL_E_SIM_DATA_NOT_AVAILABLE:
            return SapResultCode::DATA_NOT_AVAILABLE;
        default:
            return SapResultCode::GENERIC_FAILURE;
    }
}

SapResultCode convertPowerResponseProtoToHal(RIL_SIM_SAP_POWER_RSP_Response responseProto) {
    switch(responseProto) {
        case RIL_SIM_SAP_POWER_RSP_Response_RIL_E_SUCCESS:
            return SapResultCode::SUCCESS;
        case RIL_SIM_SAP_POWER_RSP_Response_RIL_E_GENERIC_FAILURE:
            return SapResultCode::GENERIC_FAILURE;
        case RIL_SIM_SAP_POWER_RSP_Response_RIL_E_SIM_ABSENT:
            return SapResultCode::CARD_REMOVED;
        case RIL_SIM_SAP_POWER_RSP_Response_RIL_E_SIM_ALREADY_POWERED_OFF:
            return SapResultCode::CARD_ALREADY_POWERED_OFF;
        case RIL_SIM_SAP_POWER_RSP_Response_RIL_E_SIM_ALREADY_POWERED_ON:
            return SapResultCode::CARD_ALREADY_POWERED_ON;
        default:
            return SapResultCode::GENERIC_FAILURE;
    }
}

SapResultCode convertResetSimResponseProtoToHal(RIL_SIM_SAP_RESET_SIM_RSP_Response responseProto) {
    switch(responseProto) {
        case RIL_SIM_SAP_RESET_SIM_RSP_Response_RIL_E_SUCCESS:
            return SapResultCode::SUCCESS;
        case RIL_SIM_SAP_RESET_SIM_RSP_Response_RIL_E_GENERIC_FAILURE:
            return SapResultCode::GENERIC_FAILURE;
        case RIL_SIM_SAP_RESET_SIM_RSP_Response_RIL_E_SIM_ABSENT:
            return SapResultCode::CARD_REMOVED;
        case RIL_SIM_SAP_RESET_SIM_RSP_Response_RIL_E_SIM_NOT_READY:
            return SapResultCode::CARD_NOT_ACCESSSIBLE;
        case RIL_SIM_SAP_RESET_SIM_RSP_Response_RIL_E_SIM_ALREADY_POWERED_OFF:
            return SapResultCode::CARD_ALREADY_POWERED_OFF;
    }
    return SapResultCode::GENERIC_FAILURE;
}

SapResultCode convertTransferCardReaderStatusResponseProtoToHal(
        RIL_SIM_SAP_TRANSFER_CARD_READER_STATUS_RSP_Response responseProto) {
    switch(responseProto) {
        case RIL_SIM_SAP_TRANSFER_CARD_READER_STATUS_RSP_Response_RIL_E_SUCCESS:
            return SapResultCode::SUCCESS;
        case RIL_SIM_SAP_TRANSFER_CARD_READER_STATUS_RSP_Response_RIL_E_GENERIC_FAILURE:
            return SapResultCode::GENERIC_FAILURE;
        case RIL_SIM_SAP_TRANSFER_CARD_READER_STATUS_RSP_Response_RIL_E_SIM_DATA_NOT_AVAILABLE:
            return SapResultCode::DATA_NOT_AVAILABLE;
    }
    return SapResultCode::GENERIC_FAILURE;
}

void processResponse(MsgHeader *rsp, RilSapSocket *sapSocket, MsgType msgType) {
    MsgId msgId = rsp->id;
    uint8_t *data = rsp->payload->bytes;
    size_t dataLen = rsp->payload->size;

    void *messagePtr = sapDecodeMessage(msgId, msgType, data, dataLen);

    sp<SapImpl> sapImpl = getSapImpl(sapSocket);
    if (sapImpl->sapCallback == NULL) {
        RLOGE("processResponse: sapCallback == NULL; msgId = %d; msgType = %d",
                msgId, msgType);
        return;
    }

    if (messagePtr == NULL) {
        RLOGE("processResponse: *messagePtr == NULL; msgId = %d; msgType = %d",
                msgId, msgType);
        sapImpl->sendFailedResponse(msgId, rsp->token, 0);
        return;
    }

    RLOGD("processResponse: sapCallback != NULL; msgId = %d; msgType = %d",
            msgId, msgType);

    Return<void> retStatus;
    switch (msgId) {
        case MsgId_RIL_SIM_SAP_CONNECT: {
            RIL_SIM_SAP_CONNECT_RSP *connectRsp = (RIL_SIM_SAP_CONNECT_RSP *)messagePtr;
            RLOGD("processResponse: calling sapCallback->connectResponse %d %d %d",
                    rsp->token,
                    connectRsp->response,
                    connectRsp->max_message_size);
            retStatus = sapImpl->sapCallback->connectResponse(rsp->token,
                    (SapConnectRsp)connectRsp->response,
                    connectRsp->max_message_size);
            break;
        }

        case MsgId_RIL_SIM_SAP_DISCONNECT:
            if (msgType == MsgType_RESPONSE) {
                RLOGD("processResponse: calling sapCallback->disconnectResponse %d", rsp->token);
                retStatus = sapImpl->sapCallback->disconnectResponse(rsp->token);
            } else {
                RIL_SIM_SAP_DISCONNECT_IND *disconnectInd =
                        (RIL_SIM_SAP_DISCONNECT_IND *)messagePtr;
                RLOGD("processResponse: calling sapCallback->disconnectIndication %d %d",
                        rsp->token, disconnectInd->disconnectType);
                retStatus = sapImpl->sapCallback->disconnectIndication(rsp->token,
                        (SapDisconnectType)disconnectInd->disconnectType);
            }
            break;

        case MsgId_RIL_SIM_SAP_APDU: {
            RIL_SIM_SAP_APDU_RSP *apduRsp = (RIL_SIM_SAP_APDU_RSP *)messagePtr;
            SapResultCode apduResponse = convertApduResponseProtoToHal(apduRsp->response);
            RLOGD("processResponse: calling sapCallback->apduResponse %d %d",
                    rsp->token, apduResponse);
            hidl_vec<uint8_t> apduRspVec;
            if (apduRsp->apduResponse != NULL && apduRsp->apduResponse->size > 0) {
                apduRspVec.setToExternal(apduRsp->apduResponse->bytes, apduRsp->apduResponse->size);
            }
            retStatus = sapImpl->sapCallback->apduResponse(rsp->token, apduResponse, apduRspVec);
            break;
        }

        case MsgId_RIL_SIM_SAP_TRANSFER_ATR: {
            RIL_SIM_SAP_TRANSFER_ATR_RSP *transferAtrRsp =
                (RIL_SIM_SAP_TRANSFER_ATR_RSP *)messagePtr;
            SapResultCode transferAtrResponse =
                convertTransferAtrResponseProtoToHal(transferAtrRsp->response);
            RLOGD("processResponse: calling sapCallback->transferAtrResponse %d %d",
                    rsp->token, transferAtrResponse);
            hidl_vec<uint8_t> transferAtrRspVec;
            if (transferAtrRsp->atr != NULL && transferAtrRsp->atr->size > 0) {
                transferAtrRspVec.setToExternal(transferAtrRsp->atr->bytes,
                        transferAtrRsp->atr->size);
            }
            retStatus = sapImpl->sapCallback->transferAtrResponse(rsp->token, transferAtrResponse,
                    transferAtrRspVec);
            break;
        }

        case MsgId_RIL_SIM_SAP_POWER: {
            SapResultCode powerResponse = convertPowerResponseProtoToHal(
                    ((RIL_SIM_SAP_POWER_RSP *)messagePtr)->response);
            RLOGD("processResponse: calling sapCallback->powerResponse %d %d",
                    rsp->token, powerResponse);
            retStatus = sapImpl->sapCallback->powerResponse(rsp->token, powerResponse);
            break;
        }

        case MsgId_RIL_SIM_SAP_RESET_SIM: {
            SapResultCode resetSimResponse = convertResetSimResponseProtoToHal(
                    ((RIL_SIM_SAP_RESET_SIM_RSP *)messagePtr)->response);
            RLOGD("processResponse: calling sapCallback->resetSimResponse %d %d",
                    rsp->token, resetSimResponse);
            retStatus = sapImpl->sapCallback->resetSimResponse(rsp->token, resetSimResponse);
            break;
        }

        case MsgId_RIL_SIM_SAP_STATUS: {
            RIL_SIM_SAP_STATUS_IND *statusInd = (RIL_SIM_SAP_STATUS_IND *)messagePtr;
            RLOGD("processResponse: calling sapCallback->statusIndication %d %d",
                    rsp->token, statusInd->statusChange);
            retStatus = sapImpl->sapCallback->statusIndication(rsp->token,
                    (SapStatus)statusInd->statusChange);
            break;
        }

        case MsgId_RIL_SIM_SAP_TRANSFER_CARD_READER_STATUS: {
            RIL_SIM_SAP_TRANSFER_CARD_READER_STATUS_RSP *transferStatusRsp =
                    (RIL_SIM_SAP_TRANSFER_CARD_READER_STATUS_RSP *)messagePtr;
            SapResultCode transferCardReaderStatusResponse =
                    convertTransferCardReaderStatusResponseProtoToHal(
                    transferStatusRsp->response);
            RLOGD("processResponse: calling sapCallback->transferCardReaderStatusResponse %d %d %d",
                    rsp->token,
                    transferCardReaderStatusResponse,
                    transferStatusRsp->CardReaderStatus);
            retStatus = sapImpl->sapCallback->transferCardReaderStatusResponse(rsp->token,
                    transferCardReaderStatusResponse,
                    transferStatusRsp->CardReaderStatus);
            break;
        }

        case MsgId_RIL_SIM_SAP_ERROR_RESP: {
            RLOGD("processResponse: calling sapCallback->errorResponse %d", rsp->token);
            retStatus = sapImpl->sapCallback->errorResponse(rsp->token);
            break;
        }

        case MsgId_RIL_SIM_SAP_SET_TRANSFER_PROTOCOL: {
            SapResultCode setTransferProtocolResponse;
            if (((RIL_SIM_SAP_SET_TRANSFER_PROTOCOL_RSP *)messagePtr)->response ==
                    RIL_SIM_SAP_SET_TRANSFER_PROTOCOL_RSP_Response_RIL_E_SUCCESS) {
                setTransferProtocolResponse = SapResultCode::SUCCESS;
            } else {
                setTransferProtocolResponse = SapResultCode::NOT_SUPPORTED;
            }
            RLOGD("processResponse: calling sapCallback->transferProtocolResponse %d %d",
                    rsp->token, setTransferProtocolResponse);
            retStatus = sapImpl->sapCallback->transferProtocolResponse(rsp->token,
                    setTransferProtocolResponse);
            break;
        }

        default:
            return;
    }
    sapImpl->checkReturnStatus(retStatus);
}

void sap::processResponse(MsgHeader *rsp, RilSapSocket *sapSocket) {
    processResponse(rsp, sapSocket, MsgType_RESPONSE);
}

void sap::processUnsolResponse(MsgHeader *rsp, RilSapSocket *sapSocket) {
    processResponse(rsp, sapSocket, MsgType_UNSOL_RESPONSE);
}

void sap::registerService(const RIL_RadioFunctions *callbacks) {
    using namespace android::hardware;
    int simCount = 1;
    const char *serviceNames[] = {
        android::RIL_getServiceName()
        #if (SIM_COUNT >= 2)
        , RIL2_SERVICE_NAME
        #if (SIM_COUNT >= 3)
        , RIL3_SERVICE_NAME
        #if (SIM_COUNT >= 4)
        , RIL4_SERVICE_NAME
        #endif
        #endif
        #endif
    };

    RIL_SOCKET_ID socketIds[] = {
        RIL_SOCKET_1
        #if (SIM_COUNT >= 2)
        , RIL_SOCKET_2
        #if (SIM_COUNT >= 3)
        , RIL_SOCKET_3
        #if (SIM_COUNT >= 4)
        , RIL_SOCKET_4
        #endif
        #endif
        #endif
    };
    #if (SIM_COUNT >= 2)
    simCount = SIM_COUNT;
    #endif

    for (int i = 0; i < simCount; i++) {
        sapService[i] = new SapImpl;
        sapService[i]->slotId = i;
        sapService[i]->rilSocketId = socketIds[i];
        RLOGD("registerService: starting ISap %s for slotId %d", serviceNames[i], i);
        android::status_t status = sapService[i]->registerAsService(serviceNames[i]);
        RLOGD("registerService: started ISap %s status %d", serviceNames[i], status);
    }
}
