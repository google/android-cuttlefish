/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include <memory>
#include <string>

namespace config {
// FilePartition class manages partition image files.
// Partition image files can be reused or created on demand. Temporary images
// are deleted when corresponding instances of FilePartition object are
// destroyed.
class FilePartition {
 public:
  ~FilePartition();

  // Create FilePartition object from existing file.
  // Specified file will not be disposed of after this instance is destroyed.
  static std::unique_ptr<FilePartition> ReuseExistingFile(
      const std::string& path);

  // Create FilePartition object at specified location and initialize content.
  // Specified file will not be disposed of after this instance is destroyed.
  static std::unique_ptr<FilePartition> CreateNewFile(const std::string& path,
                                                      int size_mb);

  // Create temporary FilePartition object using supplied prefix.
  // Newly created file will be deleted after this instance is destroyed.
  static std::unique_ptr<FilePartition> CreateTemporaryFile(
      const std::string& prefix, int size_mb);

  const std::string& GetName() const { return name_; }

 private:
  std::string name_;
  bool should_delete_ = false;

  FilePartition(const std::string& name, bool should_delete)
      : name_(name), should_delete_(should_delete) {}

  FilePartition(const FilePartition&) = delete;
  FilePartition& operator=(const FilePartition&) = delete;
};

}  // namespace config
