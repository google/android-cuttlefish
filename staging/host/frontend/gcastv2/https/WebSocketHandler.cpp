/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include <https/WebSocketHandler.h>

#include <https/ClientSocket.h>
#include <https/Support.h>

#include <iostream>
#include <sstream>

#include <string.h>

ssize_t WebSocketHandler::handleRequest(
        uint8_t *data, size_t size, bool isEOS) {
    (void)isEOS;

    size_t offset = 0;
    while (offset + 1 < size) {
        uint8_t *packet = &data[offset];
        const size_t avail = size - offset;

        size_t packetOffset = 0;
        const uint8_t headerByte = packet[packetOffset];

        const bool hasMask = (packet[packetOffset + 1] & 0x80) != 0;
        size_t payloadLen = packet[packetOffset + 1] & 0x7f;
        packetOffset += 2;

        if (payloadLen == 126) {
            if (packetOffset + 1 >= avail) {
                break;
            }

            payloadLen = U16_AT(&packet[packetOffset]);
            packetOffset += 2;
        } else if (payloadLen == 127) {
            if (packetOffset + 7 >= avail) {
                break;
            }

            payloadLen = U64_AT(&packet[packetOffset]);
            packetOffset += 8;
        }

        uint32_t mask = 0;
        if (hasMask) {
            if (packetOffset + 3 >= avail) {
                break;
            }

            mask = U32_AT(&packet[packetOffset]);
            packetOffset += 4;
        }

        if (packetOffset + payloadLen > avail) {
            break;
        }

        if (mask) {
            for (size_t i = 0; i < payloadLen; ++i) {
                packet[packetOffset + i] ^= ((mask >> (8 * (3 - (i % 4)))) & 0xff);
            }
        }

        int err = 0;
        bool is_control_frame = (headerByte & 0x08) != 0;
        if (is_control_frame) {
          uint8_t opcode = headerByte & 0x0f;
          if (opcode == 0x9 /*ping*/) {
            sendMessage(&packet[packetOffset], payloadLen, SendMode::pong);
          } else if (opcode == 0x8 /*close*/) {
            return -1;
          }
        } else {
          err = handleMessage(headerByte, &packet[packetOffset], payloadLen);
        }

        offset += packetOffset + payloadLen;

        if (err < 0) {
            return err;
        }
    }

    return offset;
}

bool WebSocketHandler::isConnected() {
    return mOutputCallback != nullptr || mClientSocket.lock() != nullptr;
}

void WebSocketHandler::setClientSocket(std::weak_ptr<ClientSocket> clientSocket) {
    mClientSocket = clientSocket;
}

void WebSocketHandler::setOutputCallback(
        const sockaddr_in &remoteAddr, OutputCallback fn) {
    mOutputCallback = fn;
    mRemoteAddr = remoteAddr;
}

int WebSocketHandler::handleMessage(
        uint8_t headerByte, const uint8_t *msg, size_t len) {
    std::cerr
        << "WebSocketHandler::handleMessage(0x"
        << std::hex
        << (unsigned)headerByte
        << std::dec
        << ")"
        << std::endl;

    std::cerr << hexdump(msg, len);

    const uint8_t opcode = headerByte & 0x0f;
    if (opcode == 8) {
        // Connection close.
        return -1;
    }

    return 0;
}

int WebSocketHandler::sendMessage(
        const void *data, size_t size, SendMode mode) {
    static constexpr bool kUseMask = false;

    size_t numHeaderBytes = 2 + (kUseMask ? 4 : 0);
    if (size > 65535) {
        numHeaderBytes += 8;
    } else if (size > 125) {
        numHeaderBytes += 2;
    }

    static constexpr uint8_t kOpCodeBySendMode[] = {
        0x1,  // text
        0x2,  // binary
        0x8,  // closeConnection
        0xa,  // pong
    };

    auto opcode = kOpCodeBySendMode[static_cast<uint8_t>(mode)];

    std::unique_ptr<uint8_t[]> buffer(new uint8_t[numHeaderBytes + size]);
    uint8_t *msg = buffer.get();
    msg[0] = 0x80 | opcode;  // FIN==1
    msg[1] = kUseMask ? 0x80 : 0x00;

    if (size > 65535) {
        msg[1] |= 127;
        msg[2] = 0x00;
        msg[3] = 0x00;
        msg[4] = 0x00;
        msg[5] = 0x00;
        msg[6] = (size >> 24) & 0xff;
        msg[7] = (size >> 16) & 0xff;
        msg[8] = (size >> 8) & 0xff;
        msg[9] = size & 0xff;
    } else if (size > 125) {
        msg[1] |= 126;
        msg[2] = (size >> 8) & 0xff;
        msg[3] = size & 0xff;
    } else {
        msg[1] |= size;
    }

    if (kUseMask) {
        uint32_t mask = rand();
        msg[numHeaderBytes - 4] = (mask >> 24) & 0xff;
        msg[numHeaderBytes - 3] = (mask >> 16) & 0xff;
        msg[numHeaderBytes - 2] = (mask >> 8) & 0xff;
        msg[numHeaderBytes - 1] = mask & 0xff;

        for (size_t i = 0; i < size; ++i) {
            msg[numHeaderBytes + i] =
                ((const uint8_t *)data)[i]
                    ^ ((mask >> (8 * (3 - (i % 4)))) & 0xff);
        }
    } else {
        memcpy(&msg[numHeaderBytes], data, size);
    }

    if (mOutputCallback) {
        mOutputCallback(msg, numHeaderBytes + size);
    } else {
        auto clientSocket = mClientSocket.lock();
        if (clientSocket) {
            clientSocket->queueOutputData(msg, numHeaderBytes + size);
        }
    }

    return 0;
}

std::string WebSocketHandler::remoteHost() const {
    sockaddr_in remoteAddr;

    if (mOutputCallback) {
        remoteAddr = mRemoteAddr;
    } else {
        auto clientSocket = mClientSocket.lock();
        if (clientSocket) {
            remoteAddr = clientSocket->remoteAddr();
        } else {
            return "0.0.0.0";
        }
    }

    const uint32_t ipAddress = ntohl(remoteAddr.sin_addr.s_addr);

    std::stringstream ss;
    ss << (ipAddress >> 24)
       << "."
       << ((ipAddress >> 16) & 0xff)
       << "."
       << ((ipAddress >> 8) & 0xff)
       << "."
       << (ipAddress & 0xff);

    return ss.str();
}

