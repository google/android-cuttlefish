/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include "host/commands/assemble_cvd/image_aggregator.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>

#include <fstream>
#include <string>
#include <vector>

#include <glog/logging.h>
#include <json/json.h>
#include <google/protobuf/text_format.h>
#include <sparse/sparse.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/subprocess.h"
#include "host/commands/assemble_cvd/mbr.h"
#include "host/libs/config/cuttlefish_config.h"
#include "device/google/cuttlefish/host/commands/assemble_cvd/cdisk_spec.pb.h"

namespace {

const int GPT_HEADER_SIZE = 512 * 34;
const int GPT_FOOTER_SIZE = 512 * 33;

const std::string BPTTOOL_FILE_PATH = "bin/cf_bpttool";

Json::Value BpttoolInput(const std::vector<ImagePartition>& partitions) {
  std::vector<off_t> file_sizes;
  off_t total_size = 20 << 20; // 20 MB for padding
  for (auto& partition : partitions) {
    LOG(INFO) << "Examining " << partition.label;
    auto file = cvd::SharedFD::Open(partition.image_file_path.c_str(), O_RDONLY);
    if (!file->IsOpen()) {
      LOG(FATAL) << "Could not open \"" << partition.image_file_path
                 << "\": " << file->StrError();
      break;
    }
    int fd = file->UNMANAGED_Dup();
    auto sparse = sparse_file_import(fd, /* verbose */ false, /* crc */ false);
    off_t partition_file_size = 0;
    if (sparse) {
      partition_file_size = sparse_file_len(sparse, /* sparse */ false,
                                            /* crc */ true);
      sparse_file_destroy(sparse);
      close(fd);
      LOG(INFO) << "was sparse";
    } else {
      partition_file_size = cvd::FileSize(partition.image_file_path);
      if (partition_file_size == 0) {
        LOG(FATAL) << "Could not get file size of \"" << partition.image_file_path
                  << "\"";
        break;
      }
      LOG(INFO) << "was not sparse";
    }
    LOG(INFO) << "size was " << partition_file_size;
    total_size += partition_file_size;
    file_sizes.push_back(partition_file_size);
  }
  Json::Value bpttool_input_json;
  bpttool_input_json["settings"] = Json::Value();
  bpttool_input_json["settings"]["disk_size"] = (Json::Int64) total_size;
  bpttool_input_json["partitions"] = Json::Value(Json::arrayValue);
  for (size_t i = 0; i < partitions.size(); i++) {
    Json::Value partition_json;
    partition_json["label"] = partitions[i].label;
    partition_json["size"] = (Json::Int64) file_sizes[i];
    partition_json["guid"] = "auto";
    partition_json["type_guid"] = "linux_fs";
    bpttool_input_json["partitions"].append(partition_json);
  }
  return bpttool_input_json;
}

std::string CreateFile(size_t len) {
  char file_template[] = "/tmp/diskXXXXXX";
  int fd = mkstemp(file_template);
  if (fd < 0) {
    LOG(FATAL) << "not able to create disk hole temp file";
  }
  char data[4096];
  for (size_t i = 0; i < sizeof(data); i++) {
    data[i] = '\0';
  }
  for (size_t i = 0; i < len + 2 * sizeof(data); i+= sizeof(data)) {
    if (write(fd, data, sizeof(data)) < (ssize_t) sizeof(data)) {
      LOG(FATAL) << "not able to write to disk hole temp file";
    }
  }
  close(fd);
  return std::string(file_template);
}

CompositeDisk MakeCompositeDiskSpec(const Json::Value& bpt_file,
                                    const std::vector<ImagePartition>& partitions,
                                    const std::string& header_file,
                                    const std::string& footer_file) {
  CompositeDisk disk;
  disk.set_version(1);
  ComponentDisk* header = disk.add_component_disks();
  header->set_file_path(header_file);
  header->set_offset(0);
  size_t previous_end = GPT_HEADER_SIZE;
  for (auto& bpt_partition: bpt_file["partitions"]) {
    if (bpt_partition["offset"].asUInt64() != previous_end) {
      ComponentDisk* component = disk.add_component_disks();
      component->set_file_path(CreateFile(bpt_partition["offset"].asUInt64() - previous_end));
      component->set_offset(previous_end);
    }
    ComponentDisk* component = disk.add_component_disks();
    for (auto& partition : partitions) {
      if (bpt_partition["label"] == partition.label) {
        component->set_file_path(partition.image_file_path);
      }
    }
    component->set_offset(bpt_partition["offset"].asUInt64());
    component->set_read_write_capability(ReadWriteCapability::READ_WRITE);
    previous_end = bpt_partition["offset"].asUInt64() + bpt_partition["size"].asUInt64();
  }
  size_t footer_start = bpt_file["settings"]["disk_size"].asUInt64() - GPT_FOOTER_SIZE;
  if (footer_start != previous_end) {
    ComponentDisk* component = disk.add_component_disks();
    component->set_file_path(CreateFile(footer_start - previous_end));
    component->set_offset(previous_end);
  }
  ComponentDisk* footer = disk.add_component_disks();
  footer->set_file_path(footer_file);
  footer->set_offset(bpt_file["settings"]["disk_size"].asUInt64() - GPT_FOOTER_SIZE);
  disk.set_length(bpt_file["settings"]["disk_size"].asUInt64());
  return disk;
}

cvd::SharedFD JsonToFd(const Json::Value& json) {
  Json::FastWriter json_writer;
  std::string json_string = json_writer.write(json);
  cvd::SharedFD pipe[2];
  cvd::SharedFD::Pipe(&pipe[0], &pipe[1]);
  int written = pipe[1]->Write(json_string.c_str(), json_string.size());
  if (written < 0) {
    LOG(FATAL) << "Failed to write to pipe, errno is " << pipe[0]->GetErrno();
  } else if (written < (int) json_string.size()) {
    LOG(FATAL) << "Failed to write full json to pipe, only did " << written;
  }
  return pipe[0];
}

Json::Value FdToJson(cvd::SharedFD fd) {
  std::string contents;
  cvd::ReadAll(fd, &contents);
  Json::Reader reader;
  Json::Value json;
  if (!reader.parse(contents, json)) {
    LOG(FATAL) << "Could not parse json: " << reader.getFormattedErrorMessages();
  }
  return json;
}

cvd::SharedFD BpttoolMakeTable(const cvd::SharedFD& input) {
  auto bpttool_path = vsoc::DefaultHostArtifactsPath(BPTTOOL_FILE_PATH);
  cvd::Command bpttool_cmd(bpttool_path);
  bpttool_cmd.AddParameter("make_table");
  bpttool_cmd.AddParameter("--input=/dev/stdin");
  bpttool_cmd.RedirectStdIO(cvd::Subprocess::StdIOChannel::kStdIn, input);
  bpttool_cmd.AddParameter("--output_json=/dev/stdout");
  cvd::SharedFD output_pipe[2];
  cvd::SharedFD::Pipe(&output_pipe[0], &output_pipe[1]);
  bpttool_cmd.RedirectStdIO(cvd::Subprocess::StdIOChannel::kStdOut, output_pipe[1]);
  int success = bpttool_cmd.Start().Wait();
  if (success != 0) {
    LOG(FATAL) << "Unable to run bpttool. Exited with status " << success;
  }
  return output_pipe[0];
}

cvd::SharedFD BpttoolMakePartitionTable(cvd::SharedFD input) {
  auto bpttool_path = vsoc::DefaultHostArtifactsPath(BPTTOOL_FILE_PATH);
  cvd::Command bpttool_cmd(bpttool_path);
  bpttool_cmd.AddParameter("make_table");
  bpttool_cmd.AddParameter("--input=/dev/stdin");
  bpttool_cmd.RedirectStdIO(cvd::Subprocess::StdIOChannel::kStdIn, input);
  bpttool_cmd.AddParameter("--output_gpt=/dev/stdout");
  cvd::SharedFD output_pipe[2];
  cvd::SharedFD::Pipe(&output_pipe[0], &output_pipe[1]);
  bpttool_cmd.RedirectStdIO(cvd::Subprocess::StdIOChannel::kStdOut, output_pipe[1]);
  int success = bpttool_cmd.Start().Wait();
  if (success != 0) {
    LOG(FATAL) << "Unable to run bpttool. Exited with status " << success;
  }
  return output_pipe[0];
}

void CreateGptFiles(cvd::SharedFD gpt, const std::string& header_file,
                    const std::string& footer_file) {
  std::string content;
  content.resize(GPT_HEADER_SIZE);
  if (cvd::ReadExact(gpt, &content) < GPT_HEADER_SIZE) {
    LOG(FATAL) << "Unable to run read full gpt. Errno is " << gpt->GetErrno();
  }
  auto header_fd = cvd::SharedFD::Open(header_file.c_str(), O_CREAT | O_RDWR, 0755);
  if (cvd::WriteAll(header_fd, content) < GPT_HEADER_SIZE) {
    LOG(FATAL) << "Unable to run write full gpt. Errno is " << gpt->GetErrno();
  }
  content.resize(GPT_FOOTER_SIZE);
  if (cvd::ReadExact(gpt, &content) < GPT_FOOTER_SIZE) {
    LOG(FATAL) << "Unable to run read full gpt. Errno is " << gpt->GetErrno();
  }
  auto footer_fd = cvd::SharedFD::Open(footer_file.c_str(), O_CREAT | O_RDWR, 0755);
  if (cvd::WriteAll(footer_fd, content) < GPT_FOOTER_SIZE) {
    LOG(FATAL) << "Unable to run write full gpt. Errno is " << gpt->GetErrno();
  }
}

void BptToolMakeDiskImage(const std::vector<ImagePartition>& partitions,
                          cvd::SharedFD table, const std::string& output) {
  auto bpttool_path = vsoc::DefaultHostArtifactsPath(BPTTOOL_FILE_PATH);
  cvd::Command bpttool_cmd(bpttool_path);
  bpttool_cmd.AddParameter("make_disk_image");
  bpttool_cmd.AddParameter("--input=/dev/stdin");
  bpttool_cmd.AddParameter("--output=", cvd::AbsolutePath(output));
  bpttool_cmd.RedirectStdIO(cvd::Subprocess::StdIOChannel::kStdIn, table);
  for (auto& partition : partitions) {
    auto abs_path = cvd::AbsolutePath(partition.image_file_path);
    bpttool_cmd.AddParameter("--image=" + partition.label + ":" + abs_path);
  }
  int success = bpttool_cmd.Start().Wait();
  if (success != 0) {
    LOG(FATAL) << "Unable to run bpttool. Exited with status " << success;
  }
}

void DeAndroidSparse(const std::vector<ImagePartition>& partitions) {
  for (const auto& partition : partitions) {
    auto file = cvd::SharedFD::Open(partition.image_file_path.c_str(), O_RDONLY);
    if (!file->IsOpen()) {
      LOG(FATAL) << "Could not open \"" << partition.image_file_path
                  << "\": " << file->StrError();
      break;
    }
    int fd = file->UNMANAGED_Dup();
    auto sparse = sparse_file_import(fd, /* verbose */ false, /* crc */ false);
    if (!sparse) {
      close(fd);
      continue;
    }
    LOG(INFO) << "Desparsing " << partition.image_file_path;
    std::string out_file_name = partition.image_file_path + ".desparse";
    auto out_file = cvd::SharedFD::Open(out_file_name.c_str(), O_RDWR | O_CREAT | O_TRUNC,
                                        S_IRUSR | S_IWUSR | S_IRGRP);
    int write_fd = out_file->UNMANAGED_Dup();
    int write_status = sparse_file_write(sparse, write_fd, /* gz */ false,
                                         /* sparse */ false, /* crc */ false);
    if (write_status < 0) {
      LOG(FATAL) << "Failed to desparse \"" << partition.image_file_path << "\": " << write_status;
    }
    close(write_fd);
    if (rename(out_file_name.c_str(), partition.image_file_path.c_str()) < 0) {
      int error_num = errno;
      LOG(FATAL) << "Could not move \"" << out_file_name << "\" to \""
                 << partition.image_file_path << "\": " << strerror(error_num);
    }
    sparse_file_destroy(sparse);
    close(fd);
  }
}

} // namespace

void AggregateImage(const std::vector<ImagePartition>& partitions,
                    const std::string& output_path) {
  DeAndroidSparse(partitions);
  auto bpttool_input_json = BpttoolInput(partitions);
  auto input_json_fd = JsonToFd(bpttool_input_json);
  auto table_fd = BpttoolMakeTable(input_json_fd);
  BptToolMakeDiskImage(partitions, table_fd, output_path);
};

void CreateCompositeDisk(std::vector<ImagePartition> partitions,
                         const std::string& header_file,
                         const std::string& footer_file,
                         const std::string& output_composite_path) {
  auto bpttool_input_json = BpttoolInput(partitions);
  auto table_fd = BpttoolMakeTable(JsonToFd(bpttool_input_json));
  auto table = FdToJson(table_fd);
  auto partition_table_fd = BpttoolMakePartitionTable(JsonToFd(bpttool_input_json));
  CreateGptFiles(partition_table_fd, header_file, footer_file);
  auto composite_proto = MakeCompositeDiskSpec(table, partitions, header_file, footer_file);
  std::ofstream composite(output_composite_path.c_str(), std::ios::binary | std::ios::trunc);
  composite << "composite_disk\x1d";
  composite_proto.SerializeToOstream(&composite);
  composite.flush();
}

void CreateQcowOverlay(const std::string& crosvm_path,
                       const std::string& backing_file,
                       const std::string& output_overlay_path) {
  cvd::Command crosvm_qcow2_cmd(crosvm_path);
  crosvm_qcow2_cmd.AddParameter("create_qcow2");
  crosvm_qcow2_cmd.AddParameter("--backing_file=", backing_file);
  crosvm_qcow2_cmd.AddParameter(output_overlay_path);
  int success = crosvm_qcow2_cmd.Start().Wait();
  if (success != 0) {
    LOG(FATAL) << "Unable to run crosvm create_qcow2. Exited with status " << success;
  }
}
