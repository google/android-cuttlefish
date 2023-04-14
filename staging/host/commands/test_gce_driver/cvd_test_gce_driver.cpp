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
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

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
#include "host/libs/web/http_client/http_client.h"
#include "host/libs/web/install_zip.h"

#include "test_gce_driver.pb.h"

using android::base::Error;
using android::base::Result;

using google::protobuf::util::ParseDelimitedFromZeroCopyStream;
using google::protobuf::util::SerializeDelimitedToFileDescriptor;

namespace cuttlefish {
namespace {

Result<Json::Value> ReadJsonFromFile(const std::string& path) {
  Json::CharReaderBuilder builder;
  std::ifstream ifs(path);
  Json::Value content;
  std::string errorMessage;
  CF_EXPECT(Json::parseFromStream(builder, ifs, &content, &errorMessage),
            "Could not read config file \"" << path << "\": " << errorMessage);
  return content;
}

class ReadEvalPrintLoop {
 public:
  ReadEvalPrintLoop(GceApi& gce, BuildApi& build, int in_fd, int out_fd,
                    bool internal_addresses)
      : gce_(gce),
        build_(build),
        in_(in_fd),
        out_(out_fd),
        internal_addresses_(internal_addresses) {}

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
        return CF_ERR("Failed to parse input message");
      }
      Result<void> handler_result;
      switch (msg.contents_case()) {
        case test_gce_driver::TestMessage::ContentsCase::kExit: {
          test_gce_driver::TestMessage stream_end_msg;
          stream_end_msg.mutable_exit();  // Set this in the oneof
          if (!SerializeDelimitedToFileDescriptor(stream_end_msg, out_)) {
            return CF_ERR("Failure while writing stream end message");
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
            handler_result =
                CF_ERR("Unexpected message: \"" << msg_txt << "\"");
          } else {
            handler_result = CF_ERR("Unexpected message: (unprintable)");
          }
        }
      }
      if (!handler_result.ok()) {
        test_gce_driver::TestMessage error_msg;
        error_msg.mutable_error()->set_text(handler_result.error().Trace());
        CF_EXPECT(SerializeDelimitedToFileDescriptor(error_msg, out_),
                  "Failure while writing error message: (\n"
                      << handler_result.error().Trace() << "\n)");
      }
      test_gce_driver::TestMessage stream_end_msg;
      stream_end_msg.mutable_stream_end();  // Set this in the oneof
      CF_EXPECT(SerializeDelimitedToFileDescriptor(stream_end_msg, out_));
    }
    return {};
  }

 private:
  Result<void> NewInstance(const test_gce_driver::CreateInstance& request) {
    CF_EXPECT(request.id().name() != "", "Instance name must be specified");
    CF_EXPECT(request.id().zone() != "", "Instance zone must be specified");
    auto instance = CF_EXPECT(ScopedGceInstance::CreateDefault(
        gce_, request.id().zone(), request.id().name(), internal_addresses_));
    instances_.emplace(request.id().name(), std::move(instance));
    return {};
  }
  Result<void> SshCommand(const test_gce_driver::SshCommand& request) {
    auto instance = instances_.find(request.instance().name());
    CF_EXPECT(instance != instances_.end(),
              "Instance \"" << request.instance().name() << "\" not found");
    auto ssh = CF_EXPECT(instance->second->Ssh());
    for (auto argument : request.arguments()) {
      ssh.RemoteParameter(argument);
    }

    std::optional<Subprocess> ssh_proc;
    SharedFD stdout_read;
    SharedFD stderr_read;
    {  // Things created here need to be closed early
      auto cmd = ssh.Build();

      SharedFD stdout_write;
      CF_EXPECT(SharedFD::Pipe(&stdout_read, &stdout_write));
      cmd.RedirectStdIO(Subprocess::StdIOChannel::kStdOut, stdout_write);

      SharedFD stderr_write;
      CF_EXPECT(SharedFD::Pipe(&stderr_read, &stderr_write));
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
        CF_EXPECT(read >= 0,
                  "Failure in reading ssh stdout: " << stdout_read->StrError());
        if (read == 0) {  // EOF
          stdout_read = SharedFD();
        } else {
          test_gce_driver::TestMessage stdout_chunk;
          stdout_chunk.mutable_data()->set_type(
              test_gce_driver::DataType::DATA_TYPE_STDOUT);
          stdout_chunk.mutable_data()->set_contents(buffer, read);
          CF_EXPECT(SerializeDelimitedToFileDescriptor(stdout_chunk, out_));
        }
      }
      if (read_set.IsSet(stderr_read)) {
        char buffer[1 << 14];
        auto read = stderr_read->Read(buffer, sizeof(buffer));
        CF_EXPECT(read >= 0,
                  "Failure in reading ssh stderr: " << stdout_read->StrError());
        if (read == 0) {  // EOF
          stderr_read = SharedFD();
        } else {
          test_gce_driver::TestMessage stderr_chunk;
          stderr_chunk.mutable_data()->set_type(
              test_gce_driver::DataType::DATA_TYPE_STDERR);
          stderr_chunk.mutable_data()->set_contents(buffer, read);
          CF_EXPECT(SerializeDelimitedToFileDescriptor(stderr_chunk, out_));
        }
      }
    }

    auto ret = ssh_proc->Wait();
    test_gce_driver::TestMessage retcode_chunk;
    retcode_chunk.mutable_data()->set_type(
        test_gce_driver::DataType::DATA_TYPE_RETURN_CODE);
    retcode_chunk.mutable_data()->set_contents(std::to_string(ret));
    CF_EXPECT(SerializeDelimitedToFileDescriptor(retcode_chunk, out_));
    return {};
  }
  Result<void> UploadBuildArtifact(
      const test_gce_driver::UploadBuildArtifact& request) {
    auto instance = instances_.find(request.instance().name());
    CF_EXPECT(instance != instances_.end(),
              "Instance \"" << request.instance().name() << "\" not found");

    struct {
      ScopedGceInstance* instance;
      SharedFD ssh_in;
      std::optional<Subprocess> ssh_proc;
      Result<void> result;
    } callback_state;

    callback_state.instance = instance->second.get();

    auto callback = [&request, &callback_state](char* data, size_t size) {
      if (data == nullptr) {
        auto ssh = callback_state.instance->Ssh();
        if (!ssh.ok()) {
          callback_state.result = CF_ERR("ssh command failed\n"
                                         << ssh.error().Trace());
          return false;
        }

        SharedFD ssh_stdin_out;
        if (!SharedFD::Pipe(&ssh_stdin_out, &callback_state.ssh_in)) {
          callback_state.result = CF_ERRNO("pipe failed");
          return false;
        }

        ssh->RemoteParameter("cat >" + request.remote_path());
        auto command = ssh->Build();
        command.RedirectStdIO(Subprocess::StdIOChannel::kStdIn, ssh_stdin_out);
        callback_state.ssh_proc = command.Start();
      } else if (WriteAll(callback_state.ssh_in, data, size) != size) {
        callback_state.ssh_proc->Stop();
        callback_state.result = CF_ERR("Failed to write contents\n"
                                       << callback_state.ssh_in->StrError());
        return false;
      }
      return true;
    };

    DeviceBuild build(request.build().id(), request.build().target());
    CF_EXPECT(
        build_.ArtifactToCallback(build, request.artifact_name(), callback),
        "Failed to send file: (\n"
            << (callback_state.result.ok()
                    ? "Unknown failure"
                    : callback_state.result.error().Trace() + "\n)"));

    callback_state.ssh_in->Close();

    if (callback_state.ssh_proc) {
      auto ssh_ret = callback_state.ssh_proc->Wait();
      CF_EXPECT(ssh_ret == 0, "SSH command failed with code: " << ssh_ret);
    }

    return {};
  }
  Result<void> UploadFile(const test_gce_driver::UploadFile& request) {
    auto instance = instances_.find(request.instance().name());
    CF_EXPECT(instance != instances_.end(),
              "Instance \"" << request.instance().name() << "\" not found");

    auto ssh = CF_EXPECT(instance->second->Ssh());

    ssh.RemoteParameter("cat >" + request.remote_path());

    auto command = ssh.Build();

    SharedFD ssh_stdin_out, ssh_stdin_in;
    CF_EXPECT(SharedFD::Pipe(&ssh_stdin_out, &ssh_stdin_in), strerror(errno));
    command.RedirectStdIO(Subprocess::StdIOChannel::kStdIn, ssh_stdin_out);

    auto ssh_proc = command.Start();
    ssh_stdin_out->Close();

    {
      Command unused = std::move(command);  // force deletion
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
        return CF_ERR("Received EOF");
      } else if (!parsed) {
        ssh_proc.Stop();
        return CF_ERR("Failed to parse message");
      } else if (data_msg.contents_case() ==
                 test_gce_driver::TestMessage::ContentsCase::kStreamEnd) {
        break;
      } else if (data_msg.contents_case() !=
                 test_gce_driver::TestMessage::ContentsCase::kData) {
        ssh_proc.Stop();
        return CF_ERR(
            "Received wrong type of message: " << data_msg.contents_case());
      } else if (data_msg.data().type() !=
                 test_gce_driver::DataType::DATA_TYPE_FILE_CONTENTS) {
        ssh_proc.Stop();
        return CF_ERR(
            "Received unexpected data type: " << data_msg.data().type());
      }
      LOG(INFO) << "going to write message of size "
                << data_msg.data().contents().size();
      if (WriteAll(ssh_stdin_in, data_msg.data().contents()) !=
          data_msg.data().contents().size()) {
        ssh_proc.Stop();
        return CF_ERR("Failed to write contents: " << ssh_stdin_in->StrError());
      }
      LOG(INFO) << "successfully wrote message?";
    }

    ssh_stdin_in->Close();

    auto ssh_ret = ssh_proc.Wait();
    CF_EXPECT(ssh_ret == 0, "SSH command failed with code: " << ssh_ret);

    return {};
  }

  GceApi& gce_;
  BuildApi& build_;
  google::protobuf::io::FileInputStream in_;
  int out_;
  bool internal_addresses_;

  std::unordered_map<std::string, std::unique_ptr<ScopedGceInstance>>
      instances_;
};

}  // namespace

Result<void> TestGceDriverMain(int argc, char** argv) {
  std::vector<Flag> flags;
  std::string service_account_json_private_key_path = "";
  flags.emplace_back(GflagsCompatFlag("service-account-json-private-key-path",
                                      service_account_json_private_key_path));
  std::string cloud_project = "";
  flags.emplace_back(GflagsCompatFlag("cloud-project", cloud_project));
  bool internal_addresses = false;
  flags.emplace_back(
      GflagsCompatFlag("internal-addresses", internal_addresses));

  std::vector<std::string> args =
      ArgsToVec(argc - 1, argv + 1);  // Skip argv[0]
  CF_EXPECT(ParseFlags(flags, args), "Could not process command line flags.");

  auto service_json =
      CF_EXPECT(ReadJsonFromFile(service_account_json_private_key_path));

  static constexpr char COMPUTE_SCOPE[] =
      "https://www.googleapis.com/auth/compute";
  auto curl = HttpClient::CurlClient();
  auto gce_creds = CF_EXPECT(ServiceAccountOauthCredentialSource::FromJson(
      *curl, service_json, COMPUTE_SCOPE));

  GceApi gce(*curl, gce_creds, cloud_project);

  static constexpr char BUILD_SCOPE[] =
      "https://www.googleapis.com/auth/androidbuild.internal";
  auto build_creds = std::make_unique<ServiceAccountOauthCredentialSource>(
      CF_EXPECT(ServiceAccountOauthCredentialSource::FromJson(
          *curl, service_json, BUILD_SCOPE)));

  BuildApi build(std::move(curl), std::move(build_creds));

  ReadEvalPrintLoop executor(gce, build, STDIN_FILENO, STDOUT_FILENO,
                             internal_addresses);
  LOG(INFO) << "Starting processing";
  CF_EXPECT(executor.Process());

  return {};
}

}  // namespace cuttlefish

int main(int argc, char** argv) {
  auto res = cuttlefish::TestGceDriverMain(argc, argv);
  if (res.ok()) {
    return 0;
  }
  LOG(ERROR) << "cvd_test_gce_driver failed: " << res.error().Message();
  LOG(DEBUG) << "cvd_test_gce_driver failed: " << res.error().Trace();
}
