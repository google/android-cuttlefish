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

#include <unistd.h>

#include <algorithm>
#include <memory>
#include <sstream>
#include <stack>

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

// given /a/b/c/d/e, ensures
// all directories from /a through /a/b/c/d/e exist
Result<void> EnsureDirectoryExistsAllTheWay(const std::string& dir) {
  CF_EXPECT(!dir.empty() && dir.at(0) == '/',
            "EnsureDirectoryExistsAllTheWay() handles absolute paths only.");
  if (dir == "/") {
    return {};
  }
  std::string path_exclude_root = dir.substr(1);
  std::vector<std::string> tokens =
      android::base::Tokenize(path_exclude_root, "/");
  std::string current_dir = "/";
  for (int i = 0; i < tokens.size(); i++) {
    current_dir.append(tokens[i]);
    CF_EXPECT(EnsureDirectoryExists(current_dir),
              current_dir << " does not exist and cannot be created.");
    current_dir.append("/");
  }
  return {};
}

static std::vector<std::string> Reverse(std::stack<std::string>& s) {
  std::vector<std::string> reversed;
  while (!s.empty()) {
    reversed.push_back(s.top());
    s.pop();
  }
  std::reverse(reversed.begin(), reversed.end());
  return reversed;
}

static std::vector<std::string> EmulateAbsolutePathImpl(
    std::stack<std::string>& so_far, const std::vector<std::string>& tokens,
    const size_t idx = 0) {
  if (idx == tokens.size()) {
    return Reverse(so_far);
  }
  const std::string& token = tokens.at(idx);
  if (token == "." || token.empty()) {
    // If token is empty, it might be //, so should be simply ignored
    return EmulateAbsolutePathImpl(so_far, tokens, idx + 1);
  }
  if (token == "..") {
    if (!so_far.empty()) {
      // at /, ls ../../.. shows just the root. So, if too many ..s are here,
      // we silently ignore them
      so_far.pop();
    }
    return EmulateAbsolutePathImpl(so_far, tokens, idx + 1);
  }
  so_far.push(token);
  return EmulateAbsolutePathImpl(so_far, tokens, idx + 1);
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

Result<std::string> EmulateAbsolutePath(const InputPathForm& path_info) {
  const auto& path = path_info.path_to_convert;
  std::string working_dir;
  if (path_info.current_working_dir) {
    working_dir = *path_info.current_working_dir;
  } else {
    std::unique_ptr<char, void (*)(void*)> cwd(getcwd(nullptr, 0), &free);
    std::string process_cwd(cwd.get());
    working_dir = std::move(process_cwd);
  }
  CF_EXPECT(android::base::StartsWith(working_dir, '/'),
            "Current working directory should be given in an absolute path.");

  const std::string home_dir = path_info.home_dir
                                   ? *path_info.home_dir
                                   : CF_EXPECT(SystemWideUserHome());
  CF_EXPECT(android::base::StartsWith(home_dir, '/'),
            "Home directory should be given in an absolute path.");

  if (path.empty()) {
    LOG(ERROR) << "The requested path to convert an absolute path is empty.";
    return "";
  }
  if (path == "/") {
    return path;
  }
  std::vector<std::string> tokens = android::base::Tokenize(path, "/");
  std::stack<std::string> prefix_dir_stack;
  if (path == "~" || android::base::StartsWith(path, "~/")) {
    // tokens == {"~", "some", "dir", "file"}
    std::vector<std::string> home_dir_tokens =
        android::base::Tokenize(home_dir, "/");
    tokens.erase(tokens.begin());
    for (const auto& home_dir_token : home_dir_tokens) {
      prefix_dir_stack.push(home_dir_token);
    }
  } else if (!android::base::StartsWith(path, "/")) {
    // path was like "a/b/c", which should be expanded to $PWD/a/b/c
    std::vector<std::string> working_dir_tokens =
        android::base::Tokenize(working_dir, "/");
    for (const auto& working_dir_token : working_dir_tokens) {
      prefix_dir_stack.push(working_dir_token);
    }
  }

  auto result = EmulateAbsolutePathImpl(prefix_dir_stack, tokens, 0);
  std::stringstream assemble_output;
  assemble_output << "/";
  if (!result.empty()) {
    assemble_output << android::base::Join(result, "/");
  }
  if (path_info.follow_symlink) {
    return AbsolutePath(assemble_output.str());
  }
  return assemble_output.str();
}

}  // namespace cuttlefish
