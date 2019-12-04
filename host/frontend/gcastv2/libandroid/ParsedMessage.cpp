/*
 * Copyright 2012, The Android Open Source Project
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

#include <media/stagefright/foundation/ParsedMessage.h>

#include <media/stagefright/Utils.h>
#include <media/stagefright/foundation/ABuffer.h>
#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/foundation/hexdump.h>

#include <ctype.h>

namespace android {

// static
std::shared_ptr<ParsedMessage> ParsedMessage::Parse(
        const char *data, size_t size, bool noMoreData, size_t *length) {
    std::shared_ptr<ParsedMessage> msg(new ParsedMessage);
    ssize_t res = msg->parse(data, size, noMoreData);

    if (res < 0) {
        *length = 0;
        return NULL;
    }

    *length = res;
    return msg;
}

ParsedMessage::ParsedMessage() {
}

ParsedMessage::~ParsedMessage() {
}

bool ParsedMessage::findString(const char *name, std::string *value) const {
    std::string key = name;
    toLower(&key);

    auto it = mDict.find(key);

    if (it == mDict.end()) {
        value->clear();

        return false;
    }

    *value = it->second;
    return true;
}

bool ParsedMessage::findInt32(const char *name, int32_t *value) const {
    std::string stringValue;

    if (!findString(name, &stringValue)) {
        return false;
    }

    char *end;
    *value = (int32_t)strtol(stringValue.c_str(), &end, 10);

    if (end == stringValue.c_str() || *end != '\0') {
        *value = 0;
        return false;
    }

    return true;
}

const char *ParsedMessage::getContent() const {
    return mContent.c_str();
}

ssize_t ParsedMessage::parse(const char *data, size_t size, bool noMoreData) {
    if (size == 0) {
        return -1;
    }

    auto lastDictIter = mDict.end();

    size_t offset = 0;
    bool headersComplete = false;
    while (offset < size) {
        size_t lineEndOffset = offset;
        while (lineEndOffset + 1 < size
                && (data[lineEndOffset] != '\r'
                        || data[lineEndOffset + 1] != '\n')) {
            ++lineEndOffset;
        }

        if (lineEndOffset + 1 >= size) {
            return -1;
        }

        std::string line(&data[offset], lineEndOffset - offset);

        if (offset == 0) {
            // Special handling for the request/status line.

            mDict[std::string("_")] = line;
            offset = lineEndOffset + 2;

            continue;
        }

        if (lineEndOffset == offset) {
            // An empty line separates headers from body.
            headersComplete = true;
            offset += 2;
            break;
        }

        if (line.c_str()[0] == ' ' || line.c_str()[0] == '\t') {
            // Support for folded header values.

            if (lastDictIter != mDict.end()) {
                // Otherwise it's malformed since the first header line
                // cannot continue anything...

                std::string &value = lastDictIter->second;
                value.append(line);
            }

            offset = lineEndOffset + 2;
            continue;
        }

        ssize_t colonPos = line.find(":");
        if (colonPos >= 0) {
            std::string key(line, 0, colonPos);
            trim(&key);
            toLower(&key);

            line.erase(0, colonPos + 1);

            lastDictIter = mDict.insert(std::make_pair(key, line)).first;
        }

        offset = lineEndOffset + 2;
    }

    if (!headersComplete && (!noMoreData || offset == 0)) {
        // We either saw the empty line separating headers from body
        // or we saw at least the status line and know that no more data
        // is going to follow.
        return -1;
    }

    for (auto &pair : mDict) {
        trim(&pair.second);
    }

    int32_t contentLength;
    if (!findInt32("content-length", &contentLength) || contentLength < 0) {
        contentLength = 0;
    }

    size_t totalLength = offset + contentLength;

    if (size < totalLength) {
        return -1;
    }

    mContent.assign(&data[offset], contentLength);

    return totalLength;
}

bool ParsedMessage::getRequestField(size_t index, std::string *field) const {
    std::string line;
    CHECK(findString("_", &line));

    size_t prevOffset = 0;
    size_t offset = 0;
    for (size_t i = 0; i <= index; ++i) {
        if (offset >= line.size()) {
            return false;
        }

        ssize_t spacePos = line.find(" ", offset);

        if (spacePos < 0) {
            spacePos = line.size();
        }

        prevOffset = offset;
        offset = spacePos + 1;
    }

    field->assign(line, prevOffset, offset - prevOffset - 1);

    return true;
}

bool ParsedMessage::getStatusCode(int32_t *statusCode) const {
    std::string statusCodeString;
    if (!getRequestField(1, &statusCodeString)) {
        return false;
    }

    char *end;
    *statusCode = (int32_t)strtol(statusCodeString.c_str(), &end, 10);

    if (*end != '\0' || end == statusCodeString.c_str()
            || (*statusCode) < 100 || (*statusCode) > 999) {
        *statusCode = 0;
        return false;
    }

    return true;
}

std::string ParsedMessage::debugString() const {
    std::string line;
    CHECK(findString("_", &line));

    line.append("\n");

    for (const auto &pair : mDict) {
        const std::string &key = pair.first;
        const std::string &value = pair.second;

        if (key == std::string("_")) {
            continue;
        }

        line.append(key);
        line.append(": ");
        line.append(value);
        line.append("\n");
    }

    line.append("\n");
    line.append(mContent);

    return line;
}

// static
bool ParsedMessage::GetAttribute(
        const char *s, const char *key, std::string *value) {
    value->clear();

    size_t keyLen = strlen(key);

    for (;;) {
        while (isspace(*s)) {
            ++s;
        }

        const char *colonPos = strchr(s, ';');

        size_t len =
            (colonPos == NULL) ? strlen(s) : colonPos - s;

        if (len >= keyLen + 1 && s[keyLen] == '=' && !strncmp(s, key, keyLen)) {
            value->assign(&s[keyLen + 1], len - keyLen - 1);
            return true;
        }

        if (colonPos == NULL) {
            return false;
        }

        s = colonPos + 1;
    }
}

// static
bool ParsedMessage::GetInt32Attribute(
        const char *s, const char *key, int32_t *value) {
    std::string stringValue;
    if (!GetAttribute(s, key, &stringValue)) {
        *value = 0;
        return false;
    }

    char *end;
    *value = (int32_t)strtol(stringValue.c_str(), &end, 10);

    if (end == stringValue.c_str() || *end != '\0') {
        *value = 0;
        return false;
    }

    return true;
}

}  // namespace android

