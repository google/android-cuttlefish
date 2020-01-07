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

#include <https/HTTPRequestResponse.h>

#include <cerrno>
#include <iostream>
#include <regex>

HTTPRequestResponse::HTTPRequestResponse()
    : mInitCheck(-ENODEV),
      mContentLength(0) {
}

int HTTPRequestResponse::setTo(const uint8_t *data, size_t size) {
    mInitCheck = -EINVAL;

    bool sawEmptyLine = false;

    size_t start = 0;
    while (start < size) {
        size_t end = start;
        while (end + 1 < size && memcmp(&data[end], "\r\n", 2)) {
            ++end;
        }

        if ((end + 1) == size) {
            return mInitCheck;
        }

        std::string line(
                reinterpret_cast<const char *>(&data[start]), end - start);

        if (start == 0) {
            // Parse the request/response line.

            if (!parseRequestResponseLine(line)) {
                return mInitCheck;
            }

        } else if (end > start) {
            std::regex re("([a-zA-Z0-9-]+): (.*)");
            std::smatch match;

            if (!std::regex_match(line, match, re)) {
                return mInitCheck;
            }

            auto key = match[1];
            auto value = match[2];
            mHeaders[key] = value;
        }

        sawEmptyLine = line.empty();

        start = end + 2;
    }

    if (!sawEmptyLine) {
        return mInitCheck;
    }

    std::string stringValue;
    if (getHeaderField("Content-Length", &stringValue)) {
        char *end;
        unsigned long value = strtoul(stringValue.c_str(), &end, 10);

        if (end == stringValue.c_str() || *end != '\0') {
            return mInitCheck;
        }

        mContentLength = value;
    }

    mInitCheck = 0;
    return mInitCheck;
}

int HTTPRequestResponse::initCheck() const {
    return mInitCheck;
}

bool HTTPRequestResponse::getHeaderField(
        std::string_view key, std::string *value) const {
    auto it = mHeaders.find(std::string(key));

    if (it != mHeaders.end()) {
        *value = it->second;
        return true;
    }

    return false;
}

size_t HTTPRequestResponse::getContentLength() const {
    return mContentLength;
}

////////////////////////////////////////////////////////////////////////////////

std::string HTTPRequest::getMethod() const {
    return mMethod;
}

std::string HTTPRequest::getPath() const {
    return mPath;
}

std::string HTTPRequest::getVersion() const {
    return mVersion;
}

bool HTTPRequest::parseRequestResponseLine(const std::string &line) {
    std::regex re("(GET|HEAD) ([a-zA-Z_/.0-9?&=]+) (HTTP/1\\.1)");
    std::smatch match;

    if (!std::regex_match(line, match, re)) {
        return false;
    }

    mMethod = match[1];
    mPath = match[2];
    mVersion = match[3];

    return true;
}

////////////////////////////////////////////////////////////////////////////////

std::string HTTPResponse::getVersion() const {
    return mVersion;
}

int32_t HTTPResponse::getStatusCode() const {
    return mStatusCode;
}

std::string HTTPResponse::getStatusMessage() const {
    return mStatusMessage;
}

bool HTTPResponse::parseRequestResponseLine(const std::string &line) {
    std::regex re(
            "(HTTP/1\\.1) ([1-9][0-9][0-9]) ([a-zA-Z _0-9.]+)");

    std::smatch match;

    if (!std::regex_match(line, match, re)) {
        return false;
    }

    mVersion = match[1];
    std::string statusString = match[2];
    mStatusMessage = match[3];

    mStatusCode =
        static_cast<int32_t>(strtol(statusString.c_str(), nullptr, 10));

    return true;
}

