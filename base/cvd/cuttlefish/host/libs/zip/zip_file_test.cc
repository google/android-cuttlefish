//
// Copyright (C) 2025 The Android Open Source Project
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

#include "cuttlefish/host/libs/zip/zip_file.h"

#include <stdint.h>
#include <stdlib.h>
#include <sys/stat.h>

#include <fstream>
#include <string>
#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "cuttlefish/host/libs/zip/libzip_cc/archive.h"
#include "cuttlefish/result/result.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace {

using ::testing::Eq;

class ZipFileTest : public ::testing::Test {
 protected:
  void SetUp() override {
    std::string dir_template = ::testing::TempDir() + "/zip_file_testXXXXXX";
    ASSERT_NE(mkdtemp(dir_template.data()), nullptr);
    dir_ = dir_template;
  }

  std::string dir_;
};

// libzip defers reading a source until `Finalize`, where reading a directory
// fails with EISDIR and `zip_source_rollback_write` discards the whole
// archive. A directory that slips through must not take unrelated entries
// down with it.
TEST_F(ZipFileTest, DirectoryDoesNotDiscardOtherEntries) {
  std::string file = dir_ + "/netsim.log";
  std::ofstream(file) << "log contents";
  std::string subdirectory = dir_ + "/pcap";
  EXPECT_EQ(mkdir(subdirectory.c_str(), 0755), 0);
  std::string zip_path = dir_ + "/out.zip";

  Result<WritableZip> zip = ZipOpenReadWrite(zip_path);
  ASSERT_THAT(zip, IsOk());
  ASSERT_THAT(AddFileAt(*zip, file, "netsimd/netsim.log"), IsOk());
  // Callers such as `cvd host_bugreport` log per-entry failures and carry on.
  EXPECT_THAT(AddFileAt(*zip, subdirectory, "netsimd/pcap"), IsError());
  ASSERT_THAT(WritableZip::Finalize(std::move(*zip)), IsOk());

  Result<ReadableZip> readable = ZipOpenRead(zip_path);
  ASSERT_THAT(readable, IsOk());
  EXPECT_THAT(readable->NumEntries(), IsOkAndValue(Eq(uint64_t{1})));
  EXPECT_THAT(readable->EntryName(0), IsOkAndValue(Eq("netsimd/netsim.log")));
}

TEST_F(ZipFileTest, AddFileAtRejectsFifos) {
  std::string fifo = dir_ + "/pipe";
  ASSERT_EQ(mkfifo(fifo.c_str(), 0600), 0);

  Result<WritableZip> zip = ZipOpenReadWrite(dir_ + "/out.zip");
  ASSERT_THAT(zip, IsOk());

  EXPECT_THAT(AddFileAt(*zip, fifo, "netsimd/pipe"), IsError());
}

}  // namespace
}  // namespace cuttlefish
