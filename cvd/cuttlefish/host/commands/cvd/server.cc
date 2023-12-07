/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include "host/commands/cvd/server.h"

#include <signal.h>
#include <unistd.h>

#include <atomic>
#include <future>
#include <map>
#include <mutex>
#include <optional>
#include <thread>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/scopeguard.h>
#include <android-base/strings.h>
#include <fmt/core.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/fs/shared_select.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/result.h"
#include "common/libs/utils/shared_fd_flag.h"
#include "common/libs/utils/subprocess.h"
#include "cvd_server.pb.h"
#include "host/commands/cvd/command_sequence.h"
#include "host/commands/cvd/common_utils.h"
#include "host/commands/cvd/epoll_loop.h"
#include "host/commands/cvd/logger.h"
#include "host/commands/cvd/request_context.h"
#include "host/commands/cvd/run_server.h"
#include "host/commands/cvd/selector/selector_constants.h"
#include "host/commands/cvd/server_command/cmd_list.h"
#include "host/commands/cvd/server_command/display.h"
#include "host/commands/cvd/server_command/env.h"
#include "host/commands/cvd/server_command/generic.h"
#include "host/commands/cvd/server_command/handler_proxy.h"
#include "host/commands/cvd/server_command/load_configs.h"
#include "host/commands/cvd/server_command/power.h"
#include "host/commands/cvd/server_command/reset.h"
#include "host/commands/cvd/server_command/snapshot.h"
#include "host/commands/cvd/server_command/start.h"
#include "host/commands/cvd/server_command/subcmd.h"
#include "host/commands/cvd/server_constants.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/known_paths.h"

using android::base::ScopeGuard;

namespace cuttlefish {

static constexpr int kNumThreads = 10;

CvdServer::CvdServer(BuildApi& build_api, EpollPool& epoll_pool,
                     InstanceManager& instance_manager,
                     HostToolTargetManager& host_tool_target_manager,
                     ServerLogger& server_logger)
    : build_api_(build_api),
      epoll_pool_(epoll_pool),
      instance_manager_(instance_manager),
      host_tool_target_manager_(host_tool_target_manager),
      server_logger_(server_logger),
      running_(true),
      optout_(false) {
  std::scoped_lock lock(threads_mutex_);
  for (auto i = 0; i < kNumThreads; i++) {
    threads_.emplace_back([this]() {
      while (running_) {
        auto result = epoll_pool_.HandleEvent();
        if (!result.ok()) {
          LOG(ERROR) << "Epoll worker error:\n"
                     << result.error().FormatForEnv();
        }
      }
      auto wakeup = BestEffortWakeup();
      CHECK(wakeup.ok()) << wakeup.error().FormatForEnv();
    });
  }
}

CvdServer::~CvdServer() {
  running_ = false;
  auto wakeup = BestEffortWakeup();
  CHECK(wakeup.ok()) << wakeup.error().FormatForEnv();
  Join();
}

Result<void> CvdServer::BestEffortWakeup() {
  // This attempts to cascade through the responder threads, forcing them
  // to wake up and see that running_ is false, then exit and wake up
  // further threads.
  auto eventfd = SharedFD::Event();
  CF_EXPECT(eventfd->IsOpen(), eventfd->StrError());
  CF_EXPECT(eventfd->EventfdWrite(1) == 0, eventfd->StrError());

  auto cb = [](EpollEvent) -> Result<void> { return {}; };
  CF_EXPECT(epoll_pool_.Register(eventfd, EPOLLIN, cb));
  return {};
}

void CvdServer::Stop() {
  {
    std::lock_guard lock(ongoing_requests_mutex_);
    running_ = false;
  }
  while (true) {
    std::shared_ptr<OngoingRequest> request;
    {
      std::lock_guard lock(ongoing_requests_mutex_);
      if (ongoing_requests_.empty()) {
        break;
      }
      auto it = ongoing_requests_.begin();
      request = *it;
      ongoing_requests_.erase(it);
    }
    {
      std::lock_guard lock(request->mutex);
      if (request->handler == nullptr) {
        continue;
      }
      request->handler->Interrupt();
    }
    auto wakeup = BestEffortWakeup();
    CHECK(wakeup.ok()) << wakeup.error().FormatForEnv();
    std::scoped_lock lock(threads_mutex_);
    for (auto& thread : threads_) {
      auto current_thread = thread.get_id() == std::this_thread::get_id();
      auto matching_thread = thread.get_id() == request->thread_id;
      if (!current_thread && matching_thread && thread.joinable()) {
        thread.join();
      }
    }
  }
}

void CvdServer::Join() {
  for (auto& thread : threads_) {
    if (thread.joinable()) {
      thread.join();
    }
  }
}

Result<void> CvdServer::Exec(const ExecParam& exec_param) {
  CF_EXPECT(server_fd_->IsOpen(), "Server not running");
  Stop();
  android::base::unique_fd server_dup{server_fd_->UNMANAGED_Dup()};
  CF_EXPECT(server_dup.get() >= 0, "dup: \"" << server_fd_->StrError() << "\"");
  android::base::unique_fd client_dup{
      exec_param.carryover_client_fd->UNMANAGED_Dup()};
  CF_EXPECT(client_dup.get() >= 0, "dup: \"" << server_fd_->StrError() << "\"");

  cvd_common::Args argv_str = {
      kServerExecPath,
      fmt::format("-{}={}", kInternalServerFd, server_dup.get()),
      fmt::format("-{}={}", kInternalCarryoverClientFd, client_dup.get()),
      fmt::format("-{}={}", kInternalAcloudTranslatorOptOut, optout_),
      fmt::format("-{}={}", kInternalRestartedInProcess, true),
  };

  int in_memory_dup = -1;
  ScopeGuard exit_action([&in_memory_dup]() {
    if (in_memory_dup >= 0) {
      if (close(in_memory_dup) != 0) {
        LOG(ERROR) << "Failed to close file " << in_memory_dup;
      }
    }
  });
  if (exec_param.in_memory_data_fd) {
    in_memory_dup = exec_param.in_memory_data_fd.value()->UNMANAGED_Dup();
    CF_EXPECTF(in_memory_dup >= 0, "dup: \"{}\"",
               exec_param.in_memory_data_fd.value()->StrError());
    argv_str.push_back(
        ConcatToString("-", kInternalMemoryCarryoverFd, "=", in_memory_dup));
  }

  std::vector<char*> argv_cstr;
  for (const auto& argv : argv_str) {
    argv_cstr.emplace_back(strdup(argv.c_str()));
  }
  argv_cstr.emplace_back(nullptr);
  android::base::unique_fd new_exe_dup{exec_param.new_exe->UNMANAGED_Dup()};
  CF_EXPECT(new_exe_dup.get() >= 0,
            "dup: \"" << exec_param.new_exe->StrError() << "\"");

  if (exec_param.verbose) {
    LOG(ERROR) << "Server Exec'ing: " << android::base::Join(argv_str, " ");
  }

  fexecve(new_exe_dup.get(), argv_cstr.data(), environ);
  for (const auto& argv : argv_cstr) {
    free(argv);
  }
  return CF_ERR("fexecve failed: \"" << strerror(errno) << "\"");
}

Result<void> CvdServer::StartServer(SharedFD server_fd) {
  server_fd_ = server_fd;
  auto cb = [this](EpollEvent ev) -> Result<void> {
    CF_EXPECT(AcceptClient(ev));
    return {};
  };
  CF_EXPECT(epoll_pool_.Register(server_fd, EPOLLIN, cb));
  return {};
}

Result<void> CvdServer::AcceptCarryoverClient(SharedFD client) {
  auto self_cb = [this](EpollEvent ev) -> Result<void> {
    CF_EXPECT(HandleMessage(ev));
    return {};
  };
  CF_EXPECT(epoll_pool_.Register(client, EPOLLIN, self_cb));

  cvd::Response success_message;
  success_message.mutable_status()->set_code(cvd::Status::OK);
  success_message.mutable_command_response();
  CF_EXPECT(SendResponse(client, success_message));
  return {};
}

Result<void> CvdServer::AcceptClient(EpollEvent event) {
  ScopeGuard stop_on_failure([this] { Stop(); });

  CF_EXPECT(event.events & EPOLLIN);
  auto client_fd = SharedFD::Accept(*event.fd);
  CF_EXPECT(client_fd->IsOpen(), client_fd->StrError());
  auto client_cb = [this](EpollEvent ev) -> Result<void> {
    CF_EXPECT(HandleMessage(ev));
    return {};
  };
  CF_EXPECT(epoll_pool_.Register(client_fd, EPOLLIN, client_cb));

  auto self_cb = [this](EpollEvent ev) -> Result<void> {
    CF_EXPECT(AcceptClient(ev));
    return {};
  };
  CF_EXPECT(epoll_pool_.Register(event.fd, EPOLLIN, self_cb));

  stop_on_failure.Disable();
  return {};
}

Result<void> CvdServer::HandleMessage(EpollEvent event) {
  ScopeGuard abandon_client([this, event] { epoll_pool_.Remove(event.fd); });

  if (event.events & EPOLLHUP) {  // Client went away.
    epoll_pool_.Remove(event.fd);
    return {};
  }

  CF_EXPECT(event.events & EPOLLIN);
  auto request = CF_EXPECT(GetRequest(event.fd));
  if (!request) {  // End-of-file / client went away.
    epoll_pool_.Remove(event.fd);
    return {};
  }
  const auto verbosity = request->Message().verbosity();
  const auto encoded_verbosity = EncodeVerbosity(verbosity);
  auto logger =
      encoded_verbosity.ok()
          ? server_logger_.LogThreadToFd(request->Err(), *encoded_verbosity)
          : server_logger_.LogThreadToFd(request->Err());
  auto response = HandleRequest(*request, event.fd);
  if (!response.ok()) {
    cvd::Response failure_message;
    failure_message.mutable_status()->set_code(cvd::Status::INTERNAL);
    failure_message.mutable_status()->set_message(
        response.error().FormatForEnv());
    CF_EXPECT(SendResponse(event.fd, failure_message));
    return {};  // Error already sent to the client, don't repeat on the server
  }
  CF_EXPECT(SendResponse(event.fd, *response));

  auto self_cb = [this, err = request->Err()](EpollEvent ev) -> Result<void> {
    CF_EXPECT(HandleMessage(ev));
    return {};
  };
  CF_EXPECT(epoll_pool_.Register(event.fd, EPOLLIN, self_cb));

  abandon_client.Disable();
  return {};
}

static Result<android::base::LogSeverity> Verbosity(
    const RequestWithStdio& request, const std::string& default_val) {
  if (request.Message().contents_case() !=
      cvd::Request::ContentsCase::kCommandRequest) {
    return default_val.empty() ? kCvdDefaultVerbosity
                               : CF_EXPECT(EncodeVerbosity(default_val));
  }
  const auto& selector_opts =
      request.Message().command_request().selector_opts();
  auto selector_args = cvd_common::ConvertToArgs(selector_opts.args());
  auto verbosity_flag =
      selector::SelectorFlags::New().FlagsAsCollection().GetFlag(
          selector::SelectorFlags::kVerbosity);
  auto verbosity_opt =
      CF_EXPECT(verbosity_flag->FilterFlag<std::string>(selector_args));
  auto ret_val = verbosity_opt.value_or(default_val);
  if (ret_val.empty()) {
    const auto severity_in_string =
        CF_EXPECT(VerbosityToString(kCvdDefaultVerbosity));
    LOG(DEBUG) << "Verbosity level is not given, so using the default value: "
               << severity_in_string << " is used";
    return kCvdDefaultVerbosity;
  }
  return CF_EXPECT(EncodeVerbosity(ret_val),
                   "Invalid verbosity level : \"" << ret_val << "\"");
}

// convert HOME, ANDROID_HOST_OUT, ANDROID_SOONG_HOST_OUT
// and ANDROID_PRODUCT_OUT into absolute paths if any.
static Result<RequestWithStdio> ConvertDirPathToAbsolute(
    const RequestWithStdio& request) {
  if (request.Message().contents_case() !=
      cvd::Request::ContentsCase::kCommandRequest) {
    return request;
  }
  if (request.Message().command_request().env().empty()) {
    return request;
  }
  auto envs =
      cvd_common::ConvertToEnvs(request.Message().command_request().env());
  std::unordered_set<std::string> interested_envs{
      kAndroidHostOut, kAndroidSoongHostOut, "HOME", kAndroidProductOut};
  const auto& current_dir =
      request.Message().command_request().working_directory();

  // make sure that "~" is not included
  for (const auto& key : interested_envs) {
    if (!Contains(envs, key)) {
      continue;
    }
    const auto& dir = envs.at(key);
    CF_EXPECT(dir != "~" && !android::base::StartsWith(dir, "~/"),
              "The " << key << " directory should not start with ~");
  }

  for (const auto& key : interested_envs) {
    if (!Contains(envs, key)) {
      continue;
    }
    const auto dir = envs.at(key);
    envs[key] =
        CF_EXPECT(EmulateAbsolutePath({.current_working_dir = current_dir,
                                       .home_dir = std::nullopt,  // unused
                                       .path_to_convert = dir,
                                       .follow_symlink = false}));
  }

  auto cmd_args =
      cvd_common::ConvertToArgs(request.Message().command_request().args());
  auto selector_args = cvd_common::ConvertToArgs(
      request.Message().command_request().selector_opts().args());
  RequestWithStdio new_request(
      request.Client(),
      MakeRequest({.cmd_args = std::move(cmd_args),
                   .env = std::move(envs),
                   .selector_args = std::move(selector_args),
                   .working_dir = current_dir},
                  request.Message().command_request().wait_behavior()),
      request.FileDescriptors(), request.Credentials());
  return new_request;
}

static Result<void> VerifyUser(const RequestWithStdio& request) {
  CF_EXPECT(request.Credentials(),
            "ucred is not available while it is necessary.");
  const uid_t client_uid = request.Credentials()->uid;
  CF_EXPECT_EQ(client_uid, getuid(), "Cvd server process is one per user.");
  return {};
}

Result<cvd::Response> CvdServer::HandleRequest(RequestWithStdio orig_request,
                                               SharedFD client) {
  CF_EXPECT(VerifyUser(orig_request));
  auto request = CF_EXPECT(ConvertDirPathToAbsolute(orig_request));
  const auto verbosity =
      CF_EXPECT(Verbosity(request, request.Message().verbosity()));
  server_logger_.SetSeverity(verbosity);

  RequestContext context(*this, instance_manager_, build_api_,
                         host_tool_target_manager_, optout_);

  // Even if the interrupt callback outlives the request handler, it'll only
  // hold on to this struct which will be cleaned out when the request handler
  // exits.
  auto shared = std::make_shared<OngoingRequest>();
  shared->handler = CF_EXPECT(context.Handler(request));
  shared->thread_id = std::this_thread::get_id();

  {
    std::lock_guard lock(ongoing_requests_mutex_);
    if (running_) {
      ongoing_requests_.insert(shared);
    } else {
      // We're executing concurrently with a Stop() call.
      return {};
    }
  }
  ScopeGuard remove_ongoing_request([this, shared] {
    std::lock_guard lock(ongoing_requests_mutex_);
    ongoing_requests_.erase(shared);
  });

  auto interrupt_cb = [this, shared, verbosity,
                       err = request.Err()](EpollEvent) -> Result<void> {
    auto logger = server_logger_.LogThreadToFd(err, verbosity);
    std::lock_guard lock(shared->mutex);
    CF_EXPECT(shared->handler != nullptr);
    CF_EXPECT(shared->handler->Interrupt());
    return {};
  };
  CF_EXPECT(epoll_pool_.Register(client, EPOLLHUP, interrupt_cb));

  auto response = CF_EXPECT(shared->handler->Handle(request));
  {
    std::lock_guard lock(shared->mutex);
    shared->handler = nullptr;
  }
  CF_EXPECT(epoll_pool_.Remove(client));  // Delete interrupt handler

  return response;
}

Result<void> CvdServer::InstanceDbFromJson(const std::string& json_string) {
  const uid_t uid = getuid();
  auto json = CF_EXPECT(ParseJson(json_string));
  CF_EXPECT(instance_manager_.LoadFromJson(uid, json));
  return {};
}

Result<int> CvdServerMain(ServerMainParam&& param) {
  SetMinimumVerbosity(android::base::VERBOSE);

  LOG(INFO) << "Starting server";

  if (!param.restarted_in_process) {
    LOG(INFO) << "Server is being daemonized...";
    CF_EXPECT(daemon(0, 0) != -1, strerror(errno));
  }

  signal(SIGPIPE, SIG_IGN);

  SharedFD server_fd = std::move(param.internal_server_fd);
  CF_EXPECT(server_fd->IsOpen(), "Did not receive a valid cvd_server fd");

  std::unique_ptr<ServerLogger> server_logger = std::move(param.server_logger);
  BuildApi build_api;
  EpollPool epoll_pool;
  auto host_tool_target_manager = NewHostToolTargetManager();
  InstanceLockFileManager lock_manager;
  InstanceManager instance_manager(lock_manager, *host_tool_target_manager);
  CvdServer server(build_api, epoll_pool, instance_manager,
                   *host_tool_target_manager, *server_logger);

  std::optional<SharedFD> memory_carryover_fd =
      std::move(param.memory_carryover_fd);
  if (memory_carryover_fd) {
    const std::string json_string =
        CF_EXPECT(ReadAllFromMemFd(*memory_carryover_fd));
    CF_EXPECT(server.InstanceDbFromJson(json_string),
              "Failed to load from: " << json_string);
  }
  if (param.acloud_translator_optout) {
    server.optout_ = param.acloud_translator_optout.value();
  }
  server.StartServer(server_fd);

  // The carryover_client wouldn't be available after AcceptCarryoverClient()
  if (param.carryover_client_fd->IsOpen()) {
    // release scoped_logger for this thread inside AcceptCarryoverClient()
    CF_EXPECT(
        server.AcceptCarryoverClient(std::move(param.carryover_client_fd)));
  }
  server.Join();

  return 0;
}

Result<std::string> ReadAllFromMemFd(const SharedFD& mem_fd) {
  const auto n_message_size = mem_fd->LSeek(0, SEEK_END);
  CF_EXPECT_NE(n_message_size, -1, "LSeek on the memory file failed.");
  std::vector<char> buffer(n_message_size);
  CF_EXPECT_EQ(mem_fd->LSeek(0, SEEK_SET), 0, mem_fd->StrError());
  auto n_read = ReadExact(mem_fd, buffer.data(), n_message_size);
  CF_EXPECT(n_read == n_message_size,
            "Expected to read " << n_message_size << " bytes but actually read "
                                << n_read << " bytes.");
  std::string message(buffer.begin(), buffer.end());
  return message;
}

}  // namespace cuttlefish
