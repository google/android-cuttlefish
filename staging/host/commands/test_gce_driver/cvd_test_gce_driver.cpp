//
// Copyright (C) 2021 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <curl/curl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <unordered_map>

#include <android-base/logging.h>
#include <android-base/result.h>
#include <android-base/strings.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <google/protobuf/text_format.h>
#include <google/protobuf/util/delimited_message_util.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/fs/shared_select.h"
#include "common/libs/utils/archive.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/subprocess.h"
#include "host/commands/test_gce_driver/gce_api.h"
#include "host/commands/test_gce_driver/key_pair.h"
#include "host/commands/test_gce_driver/scoped_instance.h"
#include "host/libs/web/build_api.h"
#include "host/libs/web/credential_source.h"
#include "host/libs/web/curl_wrapper.h"
#include "host/libs/web/install_zip.h"

#include "test_gce_driver.pb.h"

using android::base::Error;
using android::base::Result;

using google::protobuf::util::ParseDelimitedFromZeroCopyStream;
using google::protobuf::util::SerializeDelimitedToFileDescriptor;

namespace cuttlefish {
namespace {

android::base::Result<Json::Value> ReadJsonFromFile(const std::string& path) {
  Json::CharReaderBuilder builder;
  std::ifstream ifs(path);
  Json::Value content;
  std::string errorMessage;
  if (!Json::parseFromStream(builder, ifs, &content, &errorMessage)) {
    return android::base::Error()
           << "Could not read config file \"" << path << "\": " << errorMessage;
  }
  return content;
}

class ReadEvalPrintLoop {
 public:
  ReadEvalPrintLoop(GceApi& gce, BuildApi& build, int in_fd, int out_fd)
      : gce_(gce), build_(build), in_(in_fd), out_(out_fd) {}

  Result<void> Process() {
    while (true) {
      test_gce_driver::TestMessage msg;
      bool clean_eof;
      LOG(DEBUG) << "Waiting for message";
      bool parsed = ParseDelimitedFromZeroCopyStream(&msg, &in_, &clean_eof);
      LOG(DEBUG) << "Received message";
      if (clean_eof) {
        return {};
      } else if (!parsed) {
        return Error() << "Failed to parse input message";
      }
      Result<void> handler_result;
      switch (msg.contents_case()) {
        case test_gce_driver::TestMessage::ContentsCase::kExit: {
          test_gce_driver::TestMessage stream_end_msg;
          stream_end_msg.mutable_exit();  // Set this in the oneof
          if (!SerializeDelimitedToFileDescriptor(stream_end_msg, out_)) {
            return Error() << "Failure while writing stream end message";
          }
          return {};
        }
        case test_gce_driver::TestMessage::ContentsCase::kStreamEnd:
          continue;
        case test_gce_driver::TestMessage::ContentsCase::kCreateInstance:
          handler_result = NewInstance(msg.create_instance());
          break;
        case test_gce_driver::TestMessage::ContentsCase::kSshCommand:
          handler_result = SshCommand(msg.ssh_command());
          break;
        case test_gce_driver::TestMessage::ContentsCase::kUploadBuildArtifact:
          handler_result = UploadBuildArtifact(msg.upload_build_artifact());
          break;
        case test_gce_driver::TestMessage::ContentsCase::kUploadFile:
          handler_result = UploadFile(msg.upload_file());
          break;
        default: {
          std::string msg_txt;
          if (google::protobuf::TextFormat::PrintToString(msg, &msg_txt)) {
            handler_result = Error()
                             << "Unexpected message: \"" << msg_txt << "\"";
          } else {
            handler_result = Error() << "Unexpected message: (unprintable)";
          }
        }
      }
      if (!handler_result.ok()) {
        test_gce_driver::TestMessage error_msg;
        error_msg.mutable_error()->set_text(handler_result.error().message());
        if (!SerializeDelimitedToFileDescriptor(error_msg, out_)) {
          return Error() << "Failure while writing error message: \""
                         << handler_result.error() << "\"";
        }
      }
      test_gce_driver::TestMessage stream_end_msg;
      stream_end_msg.mutable_stream_end();  // Set this in the oneof
      if (!SerializeDelimitedToFileDescriptor(stream_end_msg, out_)) {
        return Error() << "Failure while writing stream end message";
      }
    }
    return {};
  }

 private:
  Result<void> NewInstance(const test_gce_driver::CreateInstance& request) {
    if (request.id().name() == "") {
      return Error() << "Instance name must be specified";
    }
    if (request.id().zone() == "") {
      return Error() << "Instance zone must be specified";
    }
    auto instance = ScopedGceInstance::CreateDefault(gce_, request.id().zone(),
                                                     request.id().name());
    if (!instance.ok()) {
      return Error() << "Failed to create instance: " << instance.error();
    }
    instances_.emplace(request.id().name(), std::move(*instance));
    return {};
  }
  Result<void> SshCommand(const test_gce_driver::SshCommand& request) {
    auto instance = instances_.find(request.instance().name());
    if (instance == instances_.end()) {
      return Error() << "Instance \"" << request.instance().name()
                     << "\" not found";
    }
    auto ssh = instance->second->Ssh();
    if (!ssh.ok()) {
      return Error() << "Failed to set up ssh command: " << ssh.error();
    }
    for (auto argument : request.arguments()) {
      ssh->RemoteParameter(argument);
    }

    std::optional<Subprocess> ssh_proc;
    SharedFD stdout_read;
    SharedFD stderr_read;
    {  // Things created here need to be closed early
      auto cmd = ssh->Build();

      SharedFD stdout_write;
      if (!SharedFD::Pipe(&stdout_read, &stdout_write)) {
        return Error() << "Failed to open pipe for stdout";
      }
      cmd.RedirectStdIO(Subprocess::StdIOChannel::kStdOut, stdout_write);

      SharedFD stderr_write;
      if (!SharedFD::Pipe(&stderr_read, &stderr_write)) {
        return Error() << "Failed to open pipe for stderr";
      }
      cmd.RedirectStdIO(Subprocess::StdIOChannel::kStdErr, stderr_write);

      ssh_proc = cmd.Start();
    }

    while (stdout_read->IsOpen() || stderr_read->IsOpen()) {
      SharedFDSet read_set;
      if (stdout_read->IsOpen()) {
        read_set.Set(stdout_read);
      }
      if (stderr_read->IsOpen()) {
        read_set.Set(stderr_read);
      }
      Select(&read_set, nullptr, nullptr, nullptr);
      if (read_set.IsSet(stdout_read)) {
        char buffer[1 << 14];
        auto read = stdout_read->Read(buffer, sizeof(buffer));
        if (read < 0) {
          return Error() << "Failure in reading ssh stdout: "
                         << stdout_read->StrError();
        } else if (read == 0) {  // EOF
          stdout_read = SharedFD();
        } else {
          test_gce_driver::TestMessage output;
          output.mutable_data()->set_type(
              test_gce_driver::DataType::DATA_TYPE_STDOUT);
          output.mutable_data()->set_contents(buffer, read);
          if (!SerializeDelimitedToFileDescriptor(output, out_)) {
            return Error() << "Failure while writing stdout message";
          }
        }
      }
      if (read_set.IsSet(stderr_read)) {
        char buffer[1 << 14];
        auto read = stderr_read->Read(buffer, sizeof(buffer));
        if (read < 0) {
          return Error() << "Failure in reading ssh stderr: "
                         << stderr_read->StrError();
        } else if (read == 0) {  // EOF
          stderr_read = SharedFD();
        } else {
          test_gce_driver::TestMessage output;
          output.mutable_data()->set_type(
              test_gce_driver::DataType::DATA_TYPE_STDERR);
          output.mutable_data()->set_contents(buffer, read);
          if (!SerializeDelimitedToFileDescriptor(output, out_)) {
            return Error() << "Failure while writing stdout message";
          }
        }
      }
    }

    auto ret = ssh_proc->Wait();
    test_gce_driver::TestMessage output;
    output.mutable_data()->set_type(
        test_gce_driver::DataType::DATA_TYPE_RETURN_CODE);
    output.mutable_data()->set_contents(std::to_string(ret));
    if (!SerializeDelimitedToFileDescriptor(output, out_)) {
      return Error() << "Failure while writing return code message";
    }
    return {};
  }
  Result<void> UploadBuildArtifact(
      const test_gce_driver::UploadBuildArtifact& request) {
    auto instance = instances_.find(request.instance().name());
    if (instance == instances_.end()) {
      return Error() << "Instance \"" << request.instance().name()
                     << "\" not found";
    }

    struct {
      ScopedGceInstance* instance;
      SharedFD tcp_server;
      SharedFD tcp_client;
      std::optional<Subprocess> ssh_proc;
      Result<void> result;
    } callback_state;

    callback_state.instance = instance->second.get();

    auto callback = [&request, &callback_state](char* data, size_t size) {
      if (data == nullptr) {
        auto ssh = callback_state.instance->Ssh();
        if (!ssh.ok()) {
          callback_state.result = Error()
                                  << "ssh command failed: " << ssh.error();
          return false;
        }

        auto tcp_server = ssh->TcpServerStdin();
        if (!tcp_server.ok()) {
          callback_state.result = Error() << "ssh tcp server failed: "
                                          << tcp_server.error();
          return false;
        }
        callback_state.tcp_server = *tcp_server;

        ssh->RemoteParameter("cat >" + request.remote_path());
        callback_state.ssh_proc = ssh->Build().Start();

        callback_state.tcp_client = SharedFD::Accept(**tcp_server);
        if (!callback_state.tcp_client->IsOpen()) {
          callback_state.ssh_proc->Stop();
          callback_state.ssh_proc->Wait();
          callback_state.result = Error()
                                  << "Failed to accept TCP client: "
                                  << callback_state.tcp_client->StrError();
          return false;
        }
      }
      if (WriteAll(callback_state.tcp_client, data, size) != size) {
        callback_state.ssh_proc->Stop();
        callback_state.result = Error()
                                << "Failed to write contents: "
                                << callback_state.tcp_client->StrError();
        return false;
      }
      return true;
    };

    DeviceBuild build(request.build().id(), request.build().target());
    if (!build_.ArtifactToCallback(build, request.artifact_name(), callback)) {
      return Error() << "Failed to send file: "
                     << (callback_state.result.ok()
                             ? "Unknown failure"
                             : callback_state.result.error().message());
    }

    if (callback_state.tcp_client->IsOpen() &&
        callback_state.tcp_client->Shutdown(SHUT_WR) != 0) {
      return Error() << "Failed to shutdown socket: "
                     << callback_state.tcp_client->StrError();
    }

    if (callback_state.ssh_proc) {
      auto ssh_ret = callback_state.ssh_proc->Wait();
      if (ssh_ret != 0) {
        return Error() << "SSH command failed with code: " << ssh_ret;
      }
    }

    return {};
  }
  Result<void> UploadFile(const test_gce_driver::UploadFile& request) {
    auto instance = instances_.find(request.instance().name());
    if (instance == instances_.end()) {
      return Error() << "Instance \"" << request.instance().name()
                     << "\" not found";
    }

    auto ssh = instance->second->Ssh();
    if (!ssh.ok()) {
      return Error() << "Could not create command: " << ssh.error();
    }

    auto tcp = ssh->TcpServerStdin();
    if (!tcp.ok()) {
      return Error() << "Failed to set up remote stdin: " << tcp.error();
    }

    ssh->RemoteParameter("cat >" + request.remote_path());

    auto ssh_proc = ssh->Build().Start();

    auto client = SharedFD::Accept(**tcp);
    if (!client->IsOpen()) {
      ssh_proc.Stop();
      return Error() << "Failed to accept TCP client: " << client->StrError();
    }

    while (true) {
      LOG(INFO) << "upload data loop";
      test_gce_driver::TestMessage data_msg;
      bool clean_eof;
      LOG(DEBUG) << "Waiting for message";
      bool parsed =
          ParseDelimitedFromZeroCopyStream(&data_msg, &in_, &clean_eof);
      if (clean_eof) {
        ssh_proc.Stop();
        return Error() << "Received EOF";
      } else if (!parsed) {
        ssh_proc.Stop();
        return Error() << "Failed to parse message";
      } else if (data_msg.contents_case() ==
                 test_gce_driver::TestMessage::ContentsCase::kStreamEnd) {
        break;
      } else if (data_msg.contents_case() !=
                 test_gce_driver::TestMessage::ContentsCase::kData) {
        ssh_proc.Stop();
        return Error() << "Received wrong type of message: "
                       << data_msg.contents_case();
      } else if (data_msg.data().type() !=
                 test_gce_driver::DataType::DATA_TYPE_FILE_CONTENTS) {
        ssh_proc.Stop();
        return Error() << "Received unexpected data type: "
                       << data_msg.data().type();
      }
      LOG(INFO) << "going to write message of size "
                << data_msg.data().contents().size();
      if (WriteAll(client, data_msg.data().contents()) !=
          data_msg.data().contents().size()) {
        ssh_proc.Stop();
        return Error() << "Failed to write contents: " << client->StrError();
      }
      LOG(INFO) << "successfully wrote message?";
    }

    if (client->Shutdown(SHUT_WR) != 0) {
      return Error() << "Failed to shutdown socket: " << client->StrError();
    }

    auto ssh_ret = ssh_proc.Wait();
    if (ssh_ret != 0) {
      return Error() << "SSH command failed with code: " << ssh_ret;
    }

    return {};
  }

  GceApi& gce_;
  BuildApi& build_;
  google::protobuf::io::FileInputStream in_;
  int out_;

  std::unordered_map<std::string, std::unique_ptr<ScopedGceInstance>>
      instances_;
};

}  // namespace

int TestGceDriverMain(int argc, char** argv) {
  std::vector<Flag> flags;
  std::string service_account_json_private_key_path = "";
  flags.emplace_back(GflagsCompatFlag("service-account-json-private-key-path",
                                      service_account_json_private_key_path));
  std::string cloud_project = "";
  flags.emplace_back(GflagsCompatFlag("cloud-project", cloud_project));

  std::vector<std::string> args =
      ArgsToVec(argc - 1, argv + 1);  // Skip argv[0]
  CHECK(ParseFlags(flags, args)) << "Could not process command line flags.";

  auto service_json = ReadJsonFromFile(service_account_json_private_key_path);
  CHECK(service_json.ok()) << service_json.error();

  static constexpr char COMPUTE_SCOPE[] =
      "https://www.googleapis.com/auth/compute";
  auto curl = CurlWrapper::Create();
  auto gce_creds = ServiceAccountOauthCredentialSource::FromJson(
      *curl, *service_json, COMPUTE_SCOPE);
  CHECK(gce_creds);

  // TODO(b/216667647): Allow these settings to be configured.
  GceApi gce(*curl, *gce_creds, cloud_project);

  static constexpr char BUILD_SCOPE[] =
      "https://www.googleapis.com/auth/androidbuild.internal";
  auto build_creds = ServiceAccountOauthCredentialSource::FromJson(
      *curl, *service_json, BUILD_SCOPE);
  CHECK(build_creds);

  BuildApi build(*curl, build_creds.get());

  ReadEvalPrintLoop executor(gce, build, STDIN_FILENO, STDOUT_FILENO);
  LOG(INFO) << "Starting processing";
  auto result = executor.Process();
  CHECK(result.ok()) << "Executor loop failed: " << result.error();

  return 0;
}

}  // namespace cuttlefish

int main(int argc, char** argv) {
  return cuttlefish::TestGceDriverMain(argc, argv);
}
