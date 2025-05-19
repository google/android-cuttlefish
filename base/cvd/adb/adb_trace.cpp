/*
 * Copyright (C) 2015 The Android Open Source Project
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

#include "sysdeps.h"
#include "adb_trace.h"

#include <string>
#include <unordered_map>
#include <vector>

#include <android-base/logging.h>
#include <android-base/strings.h>

#include "adb.h"

#if !ADB_HOST
#include <android-base/properties.h>
#endif

#if !ADB_HOST
const char* adb_device_banner = "device";
#if defined(__ANDROID__)
static android::base::LogdLogger gLogdLogger;
#endif
#else
const char* adb_device_banner = "host";
#endif

void AdbLogger(android::base::LogId id, android::base::LogSeverity severity,
               const char* tag, const char* file, unsigned int line,
               const char* message) {
    android::base::StderrLogger(id, severity, tag, file, line, message);
#if defined(_WIN32)
    // stderr can be buffered on Windows (and setvbuf doesn't seem to work), so explicitly flush.
    fflush(stderr);
#endif

#if !ADB_HOST && defined(__ANDROID__)
    // Only print logs of INFO or higher to logcat, so that `adb logcat` with adbd tracing on
    // doesn't result in exponential logging.
    if (severity >= android::base::INFO) {
        gLogdLogger(id, severity, tag, file, line, message);
    }
#endif
}


#if !ADB_HOST
static std::string get_log_file_name() {
    struct tm now;
    time_t t;
    tzset();
    time(&t);
    localtime_r(&t, &now);

    char timestamp[PATH_MAX];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d-%H-%M-%S", &now);

    return android::base::StringPrintf("/data/adb/adb-%s-%d", timestamp,
                                       getpid());
}

void start_device_log() {
    int fd = unix_open(get_log_file_name(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0640);
    if (fd == -1) {
        return;
    }

    // Redirect stdout and stderr to the log file.
    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);
    fprintf(stderr, "--- adb starting (pid %d) ---\n", getpid());
    unix_close(fd);
}
#endif

int adb_trace_mask;

std::string get_trace_setting() {
#if ADB_HOST || !defined(__ANDROID__)
    const char* setting = getenv("ADB_TRACE");
    if (setting == nullptr) {
        setting = "";
    }
    return setting;
#else
    return android::base::GetProperty("persist.adb.trace_mask", "");
#endif
}

// Split the space separated list of tags from the trace setting and build the
// trace mask from it. note that '1' and 'all' are special cases to enable all
// tracing.
//
// adb's trace setting comes from the ADB_TRACE environment variable, whereas
// adbd's comes from the system property persist.adb.trace_mask.
static void setup_trace_mask() {
    const std::string trace_setting = get_trace_setting();
    if (trace_setting.empty()) {
        return;
    }

    std::unordered_map<std::string, int> trace_flags = {
            {"1", -1},
            {"all", -1},
            {"adb", ADB},
            {"sockets", SOCKETS},
            {"packets", PACKETS},
            {"rwx", RWX},
            {"usb", USB},
            {"sync", SYNC},
            {"sysdeps", SYSDEPS},
            {"transport", TRANSPORT},
            {"jdwp", JDWP},
            {"services", SERVICES},
            {"auth", AUTH},
            {"fdevent", FDEVENT},
            {"shell", SHELL},
            {"incremental", INCREMENTAL},
            {"mdns", MDNS},
            {"mdns-stack", MDNS_STACK},
    };

    // Make sure we check for ALL enum in AdbTrace.
    size_t num_flags = trace_flags.size() - 2;
    size_t num_traces = AdbTrace::NUM_TRACES;
    if (num_flags != num_traces) {
        LOG(FATAL) << "Mismatched #AdbTrace=" << num_traces
                   << " and trace_flags.size=" << num_flags;
    }

    std::vector<std::string> elements = android::base::Split(trace_setting, ", ");
    for (const auto& elem : elements) {
        const auto& flag = trace_flags.find(elem);
        if (flag == trace_flags.end()) {
            LOG(ERROR) << "Unknown trace flag: " << elem;
            continue;
        }

        if (flag->second == -1) {
            // -1 is used for the special values "1" and "all" that enable all
            // tracing.
            adb_trace_mask = ~0;
            break;
        } else {
            adb_trace_mask |= 1 << flag->second;
        }
    }

    if (adb_trace_mask != 0) {
        android::base::SetMinimumLogSeverity(android::base::VERBOSE);
    }
}

void adb_trace_init(char** argv) {
#if !ADB_HOST
    // Don't open log file if no tracing, since this will block
    // the crypto unmount of /data
    if (!get_trace_setting().empty()) {
        if (unix_isatty(STDOUT_FILENO) == 0) {
            start_device_log();
        }
    }
#endif

#if ADB_HOST && !defined(_WIN32)
    // adb historically ignored $ANDROID_LOG_TAGS but passed it through to logcat.
    // If set, move it out of the way so that libbase logging doesn't try to parse it.
    std::string log_tags;
    char* ANDROID_LOG_TAGS = getenv("ANDROID_LOG_TAGS");
    if (ANDROID_LOG_TAGS) {
        log_tags = ANDROID_LOG_TAGS;
        unsetenv("ANDROID_LOG_TAGS");
    }
#endif

    android::base::InitLogging(argv, &AdbLogger);

#if ADB_HOST && !defined(_WIN32)
    // Put $ANDROID_LOG_TAGS back so we can pass it to logcat.
    if (!log_tags.empty()) setenv("ANDROID_LOG_TAGS", log_tags.c_str(), 1);
#endif

    setup_trace_mask();

    VLOG(ADB) << adb_version();
}

void adb_trace_enable(AdbTrace trace_tag) {
    adb_trace_mask |= (1 << trace_tag);
}
