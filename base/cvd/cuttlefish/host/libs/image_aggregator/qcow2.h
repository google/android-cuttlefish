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
#pragma once

#include <stdint.h>

#include <memory>
#include <string>

#include "cuttlefish/common/libs/utils/result.h"

namespace cuttlefish {

/**
 * qcow, or "QEMU Copy-On-Write" is a file format containing a list of disk
 * offsets and file contents. This can be combined with a backing file, to
 * represent an original disk file plus disk updates over that file. The qcow
 * files can be swapped out and replaced without affecting the original. qcow
 * is supported by QEMU and crosvm.
 */
class Qcow2Image {
 public:
  /**
   * Generate a qcow overlay backed by a given implementation file.
   *
   * The crosvm binary at `crosvm_path` is used to generate an overlay file at
   * `output_overlay_path` that functions as an overlay on the file at
   * `backing_file`.
   */
  static Result<Qcow2Image> Create(const std::string& crosvm_path,
                                   const std::string& backing_file,
                                   const std::string& output_overlay_path);
  static Result<Qcow2Image> OpenExisting(const std::string& path);

  Qcow2Image(Qcow2Image&&);
  ~Qcow2Image();
  Qcow2Image& operator=(Qcow2Image&&);

  static std::string MagicHeader();

  Result<uint64_t> Size() const;

 private:
  struct Impl;

  explicit Qcow2Image(std::unique_ptr<Impl>);

  std::unique_ptr<Impl> impl_;
};

}  // namespace cuttlefish
