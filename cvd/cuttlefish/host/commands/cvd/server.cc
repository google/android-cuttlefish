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
#include "common/libs/utils/shared_fd_flag.h"
#include "common/libs/utils/subprocess.h"
#include "host/commands/cvd/server_constants.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/known_paths.h"

namespace cuttlefish {

static fruit::Component<> RequestComponent(CvdServer* server,
                                           InstanceManager* instance_manager) {
  return fruit::createComponent()
      .bindInstance(*server)
      .bindInstance(*instance_manager)
      .install(cvdCommandComponent)
      .install(cvdShutdownComponent)
      .install(cvdVersionComponent);
}

CvdServer::CvdServer(InstanceManager& instance_manager)
    : instance_manager_(instance_manager), running_(true) {}

void CvdServer::Stop() { running_ = false; }

static Result<CvdServerHandler*> RequestHandler(
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

Result<void> CvdServer::ServerLoop(SharedFD server) {
  while (running_) {
    auto client_fd = SharedFD::Accept(*server);
    CF_EXPECT(client_fd->IsOpen(), client_fd->StrError());

    auto message_queue = CF_EXPECT(ClientMessageQueue::Create(client_fd));

    std::atomic_bool running = true;
    std::mutex handler_mutex;
    CvdServerHandler* handler = nullptr;
    std::thread message_responder([this, &message_queue, &handler,
                                   &handler_mutex, &running]() {
      while (running) {
        auto request = message_queue.WaitForRequest();
        if (!request.ok()) {
          LOG(DEBUG) << "Didn't get client request:"
                     << request.error().message();
          break;
        }
        fruit::Injector<> request_injector(RequestComponent, this,
                                           &instance_manager_);
        Result<cvd::Response> response;
        {
          std::scoped_lock lock(handler_mutex);
          auto handler_result = RequestHandler(
              *request, request_injector.getMultibindings<CvdServerHandler>());
          if (handler_result.ok()) {
            handler = *handler_result;
          } else {
            handler = nullptr;
            response = cvd::Response();
            response->mutable_status()->set_code(cvd::Status::INTERNAL);
            response->mutable_status()->set_message("No handler found");
          }
          // Drop the handler lock so it has a chance to be interrupted.
        }
        if (handler) {
          response = handler->Handle(*request);
        }
        {
          std::scoped_lock lock(handler_mutex);
          handler = nullptr;
        }
        if (!response.ok()) {
          LOG(DEBUG) << "Error handling request: " << request.error().message();
          cvd::Response error_response;
          error_response.mutable_status()->set_code(cvd::Status::INTERNAL);
          error_response.mutable_status()->set_message(
              response.error().message());
          response = error_response;
        }
        auto write_response = message_queue.PostResponse(*response);
        if (!write_response.ok()) {
          LOG(DEBUG) << "Error writing response: " << write_response.error();
          break;
        }
      }
    });

    message_queue.Join();
    // The client has gone away, do our best to interrupt/stop ongoing
    // operations.
    running = false;
    {
      // This might end up executing after the handler is completed, but it at
      // least won't race with the handler being overwritten by another pointer.
      std::scoped_lock lock(handler_mutex);
      if (handler) {
        auto interrupt = handler->Interrupt();
        if (!interrupt.ok()) {
          LOG(ERROR) << "Failed to interrupt handler: " << interrupt.error();
        }
      }
    }
    message_responder.join();
  }
  return {};
}

static fruit::Component<CvdServer> ServerComponent() {
  return fruit::createComponent();
}

static Result<int> CvdServerMain(int argc, char** argv) {
  android::base::InitLogging(argv, android::base::StderrLogger);

  LOG(INFO) << "Starting server";

  signal(SIGPIPE, SIG_IGN);

  std::vector<Flag> flags;
  SharedFD server_fd;
  flags.emplace_back(
      SharedFDFlag("server_fd", server_fd)
          .Help("File descriptor to an already created vsock server"));
  std::vector<std::string> args =
      ArgsToVec(argc - 1, argv + 1);  // Skip argv[0]
  CF_EXPECT(ParseFlags(flags, args));

  CF_EXPECT(server_fd->IsOpen(), "Did not receive a valid cvd_server fd");

  fruit::Injector<CvdServer> injector(ServerComponent);
  CF_EXPECT(injector.get<CvdServer&>().ServerLoop(server_fd));

  return 0;
}

}  // namespace cuttlefish

int main(int argc, char** argv) {
  auto res = cuttlefish::CvdServerMain(argc, argv);
  CHECK(res.ok()) << "cvd server failed: " << res.error().message();
  return *res;
}
