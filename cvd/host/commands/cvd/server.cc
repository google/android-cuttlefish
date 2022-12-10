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
#include <fruit/fruit.h>

#include "cvd_server.pb.h"

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/fs/shared_select.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/result.h"
#include "common/libs/utils/scope_guard.h"
#include "common/libs/utils/shared_fd_flag.h"
#include "common/libs/utils/subprocess.h"
#include "host/commands/cvd/acloud_command.h"
#include "host/commands/cvd/build_api.h"
#include "host/commands/cvd/command_sequence.h"
#include "host/commands/cvd/demo_multi_vd.h"
#include "host/commands/cvd/epoll_loop.h"
#include "host/commands/cvd/help_command.h"
#include "host/commands/cvd/load_configs.h"
#include "host/commands/cvd/logger.h"
#include "host/commands/cvd/server_constants.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/inject.h"
#include "host/libs/config/known_paths.h"

namespace cuttlefish {

static constexpr int kNumThreads = 10;

CvdServer::CvdServer(BuildApi& build_api, EpollPool& epoll_pool,
                     InstanceManager& instance_manager,
                     ServerLogger& server_logger)
    : build_api_(build_api),
      epoll_pool_(epoll_pool),
      instance_manager_(instance_manager),
      server_logger_(server_logger),
      running_(true) {
  std::scoped_lock lock(threads_mutex_);
  for (auto i = 0; i < kNumThreads; i++) {
    threads_.emplace_back([this]() {
      while (running_) {
        auto result = epoll_pool_.HandleEvent();
        if (!result.ok()) {
          LOG(ERROR) << "Epoll worker error:\n" << result.error().Message();
          LOG(DEBUG) << "Epoll worker error:\n" << result.error().Trace();
        }
      }
      auto wakeup = BestEffortWakeup();
      CHECK(wakeup.ok()) << wakeup.error().Trace();
    });
  }
}

CvdServer::~CvdServer() {
  running_ = false;
  auto wakeup = BestEffortWakeup();
  CHECK(wakeup.ok()) << wakeup.error().Trace();
  Join();
}

fruit::Component<> CvdServer::RequestComponent(CvdServer* server) {
  return fruit::createComponent()
      .bindInstance(*server)
      .bindInstance(server->instance_manager_)
      .bindInstance(server->build_api_)
      .install(AcloudCommandComponent)
      .install(CommandSequenceExecutorComponent)
      .install(cvdCommandComponent)
      .install(CvdHelpComponent)
      .install(CvdRestartComponent)
      .install(cvdShutdownComponent)
      .install(cvdVersionComponent)
      .install(DemoMultiVdComponent)
      .install(LoadConfigsComponent);
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
    CHECK(wakeup.ok()) << wakeup.error().Trace();
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

Result<void> CvdServer::Exec(SharedFD new_exe, SharedFD client_fd) {
  CF_EXPECT(server_fd_->IsOpen(), "Server not running");
  Stop();
  android::base::unique_fd server_dup{server_fd_->UNMANAGED_Dup()};
  CF_EXPECT(server_dup.get() >= 0, "dup: \"" << server_fd_->StrError() << "\"");
  android::base::unique_fd client_dup{client_fd->UNMANAGED_Dup()};
  CF_EXPECT(client_dup.get() >= 0, "dup: \"" << server_fd_->StrError() << "\"");
  std::vector<std::string> argv_str = {
      "cvd_server",
      "-INTERNAL_server_fd=" + std::to_string(server_dup.get()),
      "-INTERNAL_carryover_client_fd=" + std::to_string(client_dup.get()),
  };
  std::vector<char*> argv_cstr;
  for (const auto& argv : argv_str) {
    argv_cstr.emplace_back(strdup(argv.c_str()));
  }
  argv_cstr.emplace_back(nullptr);
  android::base::unique_fd new_exe_dup{new_exe->UNMANAGED_Dup()};
  CF_EXPECT(new_exe_dup.get() >= 0, "dup: \"" << new_exe->StrError() << "\"");
  fexecve(new_exe_dup.get(), argv_cstr.data(), environ);
  for (const auto& argv : argv_cstr) {
    free(argv);
  }
  return CF_ERR("fexecve failed: \"" << strerror(errno) << "\"");
}

Result<CvdServerHandler*> RequestHandler(
    const RequestWithStdio& request,
    const std::vector<CvdServerHandler*>& handlers) {
  Result<cvd::Response> response;
  std::vector<CvdServerHandler*> compatible_handlers;
  for (auto& handler : handlers) {
    if (CF_EXPECT(handler->CanHandle(request))) {
      compatible_handlers.push_back(handler);
    }
  }
  CF_EXPECT(compatible_handlers.size() == 1,
            "Expected exactly one handler for message, found "
                << compatible_handlers.size());
  return compatible_handlers[0];
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
  cvd::Response success_message;
  success_message.mutable_status()->set_code(cvd::Status::OK);
  success_message.mutable_command_response();
  CF_EXPECT(SendResponse(client, success_message));

  auto self_cb = [this](EpollEvent ev) -> Result<void> {
    CF_EXPECT(HandleMessage(ev));
    return {};
  };
  CF_EXPECT(epoll_pool_.Register(client, EPOLLIN, self_cb));

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

  stop_on_failure.Cancel();
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

  auto logger = server_logger_.LogThreadToFd(request->Err());
  auto response = HandleRequest(*request, event.fd);
  if (!response.ok()) {
    cvd::Response failure_message;
    failure_message.mutable_status()->set_code(cvd::Status::INTERNAL);
    failure_message.mutable_status()->set_message(response.error().Trace());
    CF_EXPECT(SendResponse(event.fd, failure_message));
    return {};  // Error already sent to the client, don't repeat on the server
  }
  CF_EXPECT(SendResponse(event.fd, *response));

  auto self_cb = [this, err = request->Err()](EpollEvent ev) -> Result<void> {
    CF_EXPECT(HandleMessage(ev));
    return {};
  };
  CF_EXPECT(epoll_pool_.Register(event.fd, EPOLLIN, self_cb));

  abandon_client.Cancel();
  return {};
}

// replace cvd -h or --help into cvd help
static RequestWithStdio ReplaceHelp(RequestWithStdio&& src_request) {
  auto request = std::move(src_request);
  cvd::Request modified_proto = request.Message();
  auto& args = *modified_proto.mutable_command_request()->mutable_args();
  if (args.size() < 2 || android::base::Basename(args.at(0)) != "cvd") {
    return request;
  }
  const auto help_arg = args.at(1);
  if (help_arg != "--help" && help_arg != "-h" && help_arg != "-help") {
    return request;
  }

  args[1] = "help";
  RequestWithStdio new_request(request.Client(), modified_proto,
                               request.FileDescriptors(),
                               request.Credentials());
  return new_request;
}

Result<cvd::Response> CvdServer::HandleRequest(RequestWithStdio request_orig,
                                               SharedFD client) {
  RequestWithStdio request = ReplaceHelp(std::move(request_orig));

  fruit::Injector<> injector(RequestComponent, this);

  for (auto& late_injected : injector.getMultibindings<LateInjected>()) {
    CF_EXPECT(late_injected->LateInject(injector));
  }

  auto possible_handlers = injector.getMultibindings<CvdServerHandler>();

  // Even if the interrupt callback outlives the request handler, it'll only
  // hold on to this struct which will be cleaned out when the request handler
  // exits.
  auto shared = std::make_shared<OngoingRequest>();
  shared->handler = CF_EXPECT(RequestHandler(request, possible_handlers));
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

  auto interrupt_cb = [this, shared,
                       err = request.Err()](EpollEvent) -> Result<void> {
    auto logger = server_logger_.LogThreadToFd(err);
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

static fruit::Component<> ServerComponent() {
  return fruit::createComponent()
      .addMultibinding<CvdServer, CvdServer>()
      .install(BuildApiModule)
      .install(EpollLoopComponent);
}

Result<int> CvdServerMain(SharedFD server_fd, SharedFD carryover_client) {
  LOG(INFO) << "Starting server";

  CF_EXPECT(daemon(0, 0) != -1, strerror(errno));

  signal(SIGPIPE, SIG_IGN);

  CF_EXPECT(server_fd->IsOpen(), "Did not receive a valid cvd_server fd");

  fruit::Injector<> injector(ServerComponent);

  for (auto& late_injected : injector.getMultibindings<LateInjected>()) {
    CF_EXPECT(late_injected->LateInject(injector));
  }

  auto server_bindings = injector.getMultibindings<CvdServer>();
  CF_EXPECT(server_bindings.size() == 1,
            "Expected 1 server binding, got " << server_bindings.size());
  auto& server = *(server_bindings[0]);
  server.StartServer(server_fd);

  if (carryover_client->IsOpen()) {
    CF_EXPECT(server.AcceptCarryoverClient(carryover_client));
  }

  server.Join();

  return 0;
}

}  // namespace cuttlefish
