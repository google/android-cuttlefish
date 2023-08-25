/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include "host/commands/cvd/common_utils.h"

#include <memory>
#include <mutex>
#include <sstream>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/strings.h>

#include "common/libs/utils/contains.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/users.h"

namespace cuttlefish {

cvd::Request MakeRequest(const MakeRequestForm& request_form) {
  return MakeRequest(request_form, cvd::WAIT_BEHAVIOR_COMPLETE);
}

cvd::Request MakeRequest(const MakeRequestForm& request_form,
                         cvd::WaitBehavior wait_behavior) {
  const auto& args = request_form.cmd_args;
  const auto& env = request_form.env;
  const auto& selector_args = request_form.selector_args;
  cvd::Request request;
  auto command_request = request.mutable_command_request();
  for (const std::string& arg : args) {
    command_request->add_args(arg);
  }
  auto selector_opts = command_request->mutable_selector_opts();
  for (const std::string& selector_arg : selector_args) {
    selector_opts->add_args(selector_arg);
  }

  for (const auto& [key, value] : env) {
    (*command_request->mutable_env())[key] = value;
  }

  /*
   * the client must set the kAndroidHostOut environment variable. There were,
   * however, a few branches where kAndroidSoongHostOut replaced
   * kAndroidHostOut. Cvd server eventually read kAndroidHostOut only and set
   * both for the subtools.
   *
   * If none of the two are set, cvd server tries to use the parent directory of
   * the client cvd executable as env[kAndroidHostOut].
   *
   */
  if (!Contains(command_request->env(), kAndroidHostOut)) {
    const std::string new_android_host_out =
        Contains(command_request->env(), kAndroidSoongHostOut)
            ? (*command_request->mutable_env())[kAndroidSoongHostOut]
            : android::base::Dirname(android::base::GetExecutableDirectory());
    (*command_request->mutable_env())[kAndroidHostOut] = new_android_host_out;
  }

  if (!request_form.working_dir) {
    std::unique_ptr<char, void (*)(void*)> cwd(getcwd(nullptr, 0), &free);
    command_request->set_working_directory(cwd.get());
  } else {
    command_request->set_working_directory(request_form.working_dir.value());
  }
  command_request->set_wait_behavior(wait_behavior);

  return request;
}

template <typename T>
std::ostream& operator<<(std::ostream& out, const std::vector<T>& v) {
  if (v.empty()) {
    out << "{}";
    return out;
  }
  if (v.size() == 1) {
    out << "{" << v.front() << "}";
    return out;
  }
  out << "{";
  for (size_t i = 0; i != v.size() - 1; i++) {
    out << v.at(i) << ", ";
  }
  out << v.back() << "}";
  return out;
}

Result<android::base::LogSeverity> EncodeVerbosity(
    const std::string& verbosity) {
  std::unordered_map<std::string, android::base::LogSeverity>
      verbosity_encode_tab{
          {"VERBOSE", android::base::VERBOSE},
          {"DEBUG", android::base::DEBUG},
          {"INFO", android::base::INFO},
          {"WARNING", android::base::WARNING},
          {"ERROR", android::base::ERROR},
          {"FATAL_WITHOUT_ABORT", android::base::FATAL_WITHOUT_ABORT},
          {"FATAL", android::base::FATAL},
      };
  CF_EXPECT(Contains(verbosity_encode_tab, verbosity),
            "Verbosity \"" << verbosity << "\" is unrecognized.");
  return verbosity_encode_tab.at(verbosity);
}

Result<std::string> VerbosityToString(
    const android::base::LogSeverity verbosity) {
  std::unordered_map<android::base::LogSeverity, std::string>
      verbosity_decode_tab{
          {android::base::VERBOSE, "VERBOSE"},
          {android::base::DEBUG, "DEBUG"},
          {android::base::INFO, "INFO"},
          {android::base::WARNING, "WARNING"},
          {android::base::ERROR, "ERROR"},
          {android::base::FATAL_WITHOUT_ABORT, "FATAL_WITHOUT_ABORT"},
          {android::base::FATAL, "FATAL"},
      };
  CF_EXPECT(Contains(verbosity_decode_tab, verbosity),
            "Verbosity \"" << verbosity << "\" is unrecognized.");
  return verbosity_decode_tab.at(verbosity);
}

static std::mutex verbosity_mutex;

android::base::LogSeverity SetMinimumVerbosity(
    const android::base::LogSeverity severity) {
  std::lock_guard lock(verbosity_mutex);
  return android::base::SetMinimumLogSeverity(severity);
}

Result<android::base::LogSeverity> SetMinimumVerbosity(
    const std::string& severity) {
  std::lock_guard lock(verbosity_mutex);
  return SetMinimumVerbosity(CF_EXPECT(EncodeVerbosity(severity)));
}

android::base::LogSeverity GetMinimumVerbosity() {
  std::lock_guard lock(verbosity_mutex);
  return android::base::GetMinimumLogSeverity();
}

}  // namespace cuttlefish
