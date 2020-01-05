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

