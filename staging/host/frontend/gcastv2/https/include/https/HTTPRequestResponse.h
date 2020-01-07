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

#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>

struct HTTPRequestResponse {
    explicit HTTPRequestResponse();
    virtual ~HTTPRequestResponse() = default;

    HTTPRequestResponse(const HTTPRequestResponse &) = delete;
    HTTPRequestResponse &operator=(const HTTPRequestResponse &) = delete;

    int setTo(const uint8_t *data, size_t size);
    int initCheck() const;

    bool getHeaderField(std::string_view key, std::string *value) const;

    size_t getContentLength() const;

protected:
    virtual bool parseRequestResponseLine(const std::string &line) = 0;

private:
    struct CaseInsensitiveCompare {
        bool operator()(const std::string &a, const std::string &b) const {
            return strcasecmp(a.c_str(), b.c_str()) < 0;
        }
    };

    int mInitCheck;

    size_t mContentLength;

    std::map<std::string, std::string, CaseInsensitiveCompare> mHeaders;
};

struct HTTPRequest : public HTTPRequestResponse {
    std::string getMethod() const;
    std::string getPath() const;
    std::string getVersion() const;

protected:
    bool parseRequestResponseLine(const std::string &line) override;

private:
    std::string mMethod;
    std::string mPath;
    std::string mVersion;
};

struct HTTPResponse : public HTTPRequestResponse {
    std::string getVersion() const;
    int32_t getStatusCode() const;
    std::string getStatusMessage() const;

protected:
    bool parseRequestResponseLine(const std::string &line) override;

private:
    std::string mVersion;
    std::string mStatusMessage;
    int32_t mStatusCode;
};

