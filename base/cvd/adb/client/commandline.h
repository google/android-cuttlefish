/*
 * Copyright (C) 2016 The Android Open Source Project
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

#ifndef COMMANDLINE_H
#define COMMANDLINE_H

#include <android-base/strings.h>
#include <google/protobuf/text_format.h>

#include <stdlib.h>
#include <optional>

#include "adb.h"
#include "adb_client.h"
#include "adb_unique_fd.h"
#include "transport.h"

// Callback used to handle the standard streams (stdout and stderr) sent by the
// device's upon receiving a command.

//
class StandardStreamsCallbackInterface {
  public:
    StandardStreamsCallbackInterface() {
    }
    // Handles the stdout output from devices supporting the Shell protocol.
    // Returns true on success and false on failure.
    virtual bool OnStdoutReceived(const char* buffer, size_t length) = 0;

    // Handles the stderr output from devices supporting the Shell protocol.
    // Returns true on success and false on failure.
    virtual bool OnStderrReceived(const char* buffer, size_t length) = 0;

    // Indicates the communication is finished and returns the appropriate error
    // code.
    //
    // |status| has the status code returning by the underlying communication
    // channels
    virtual int Done(int status) = 0;

  protected:
    static bool SendTo(std::string* string, FILE* stream, const char* buffer, size_t length,
                       bool returnErrors) {
        if (string != nullptr) {
            string->append(buffer, length);
            return true;
        } else {
            bool okay = (fwrite(buffer, 1, length, stream) == length);
            fflush(stream);
            return returnErrors ? okay : true;
        }
    }

  private:
    DISALLOW_COPY_AND_ASSIGN(StandardStreamsCallbackInterface);
};

// Default implementation that redirects the streams to the equivalent host
// stream or to a string passed to the constructor.
class DefaultStandardStreamsCallback : public StandardStreamsCallbackInterface {
  public:
    // If |stdout_str| is non-null, OnStdoutReceived will append to it.
    // If |stderr_str| is non-null, OnStderrReceived will append to it.
    DefaultStandardStreamsCallback(std::string* stdout_str, std::string* stderr_str)
        : stdout_str_(stdout_str), stderr_str_(stderr_str), returnErrors_(false) {
    }
    DefaultStandardStreamsCallback(std::string* stdout_str, std::string* stderr_str,
                                   bool returnErrors)
        : stdout_str_(stdout_str), stderr_str_(stderr_str), returnErrors_(returnErrors) {
    }

    // Called when receiving from the device standard input stream
    bool OnStdoutReceived(const char* buffer, size_t length) override {
        return SendToOut(buffer, length);
    }

    // Called when receiving from the device error input stream
    bool OnStderrReceived(const char* buffer, size_t length) override {
        return SendToErr(buffer, length);
    }

    // Send to local standard input stream (or stdout_str if one was provided).
    bool SendToOut(const char* buffer, size_t length) {
        return SendTo(stdout_str_, stdout, buffer, length, returnErrors_);
    }

    // Send to local standard error stream (or stderr_str if one was provided).
    bool SendToErr(const char* buffer, size_t length) {
        return SendTo(stderr_str_, stderr, buffer, length, returnErrors_);
    }

    int Done(int status) {
        return status;
    }

    void ReturnErrors(bool returnErrors) {
        returnErrors_ = returnErrors;
    }

  private:
    std::string* stdout_str_;
    std::string* stderr_str_;
    bool returnErrors_;

    DISALLOW_COPY_AND_ASSIGN(DefaultStandardStreamsCallback);
};

class SilentStandardStreamsCallbackInterface : public StandardStreamsCallbackInterface {
  public:
    SilentStandardStreamsCallbackInterface() = default;
    bool OnStdoutReceived(const char*, size_t) final { return true; }
    bool OnStderrReceived(const char*, size_t) final { return true; }
    int Done(int status) final { return status; }
};

// Singleton.
extern DefaultStandardStreamsCallback DEFAULT_STANDARD_STREAMS_CALLBACK;

// Prints out human-readable form of the protobuf message received in binary format.
// Expected input is a stream of (<hex4>, [binary protobuf]).
template <typename T>
class ProtoBinaryToText : public DefaultStandardStreamsCallback {
  public:
    explicit ProtoBinaryToText(const std::string& m, std::string* std_out = nullptr,
                               std::string* std_err = nullptr)
        : DefaultStandardStreamsCallback(std_out, std_err), message(m) {}
    bool OnStdoutReceived(const char* b, size_t l) override {
        constexpr size_t kHeader_size = 4;

        // Add the incoming bytes to our internal buffer.
        std::copy_n(b, l, std::back_inserter(buffer_));

        // Do we have at least the header?
        if (buffer_.size() < kHeader_size) {
            return true;
        }

        // We have a header. Convert <hex4> to size_t and check if we have received all
        // the payload.
        const std::string expected_size_hex = std::string(buffer_.data(), kHeader_size);
        const size_t expected_size = strtoull(expected_size_hex.c_str(), nullptr, 16);

        // Do we have the header + all expected payload?
        if (buffer_.size() < expected_size + kHeader_size) {
            return true;
        }

        // Convert binary to text proto.
        T binary_proto;
        binary_proto.ParseFromString(std::string(buffer_.data() + kHeader_size, expected_size));
        std::string string_proto;
        google::protobuf::TextFormat::PrintToString(binary_proto, &string_proto);

        // Drop bytes that we just consumed.
        buffer_.erase(buffer_.begin(), buffer_.begin() + kHeader_size + expected_size);

        SendToOut(message.data(), message.length());
        SendToOut(string_proto.data(), string_proto.length());

        // Recurse if there is still data in our buffer (there may be more messages).
        if (!buffer_.empty()) {
            OnStdoutReceived("", 0);
        }

        return true;
    }

  private:
    DISALLOW_COPY_AND_ASSIGN(ProtoBinaryToText);
    // We buffer bytes here until we get all the header and payload bytes
    std::vector<char> buffer_;
    std::string message;
};

int adb_commandline(int argc, const char** argv);

// Helper retrieval function.
const std::optional<FeatureSet>& adb_get_feature_set_or_die();

bool copy_to_file(int inFd, int outFd);

// Connects to the device "shell" service with |command| and prints the
// resulting output.
// if |callback| is non-null, stdout/stderr output will be handled by it.
int send_shell_command(
        const std::string& command, bool disable_shell_protocol = false,
        StandardStreamsCallbackInterface* callback = &DEFAULT_STANDARD_STREAMS_CALLBACK);

// Reads from |fd| and prints received data. If |use_shell_protocol| is true
// this expects that incoming data will use the shell protocol, in which case
// stdout/stderr are routed independently and the remote exit code will be
// returned.
// if |callback| is non-null, stdout/stderr output will be handled by it.
int read_and_dump(borrowed_fd fd, bool use_shell_protocol = false,
                  StandardStreamsCallbackInterface* callback = &DEFAULT_STANDARD_STREAMS_CALLBACK);

// Connects to the device "abb" service with |command| and returns the fd.
template <typename ContainerT>
unique_fd send_abb_exec_command(const ContainerT& command_args, std::string* error) {
    std::string service_string = "abb_exec:" + android::base::Join(command_args, ABB_ARG_DELIMETER);

    unique_fd fd(adb_connect(service_string, error));
    if (fd < 0) {
        fprintf(stderr, "adb: failed to run abb_exec. Error: %s\n", error->c_str());
        return unique_fd{};
    }
    return fd;
}

#endif  // COMMANDLINE_H
