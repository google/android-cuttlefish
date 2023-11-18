/*
 * Copyright (C) 2016 The Android Open Source Project
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
#ifndef CUTTLEFISH_COMMON_COMMON_LIBS_FS_SHARED_SELECT_H_
#define CUTTLEFISH_COMMON_COMMON_LIBS_FS_SHARED_SELECT_H_

#include <set>

#include "common/libs/fs/shared_fd.h"

namespace cuttlefish {
/**
 * The SharedFD version of fdset for the Select call.
 *
 * There are two types of methods. STL inspired methods and types use
 * all_lowercase_underscore notation.
 *
 * Methods that are inspired by POSIX Use UpperCamelCase.
 *
 * Assume that any mutation invalidates all iterators.
 */
class SharedFDSet {
 public:
  // These methods and types have more to do with the STL than POSIX,
  // so I'm using STL-compatible notation.
  typedef std::set<SharedFD>::iterator iterator;
  typedef std::set<SharedFD>::const_iterator const_iterator;

  iterator begin() { return value_.begin(); }
  iterator end() { return value_.end(); }
  const_iterator begin() const { return value_.begin(); }
  const_iterator end() const { return value_.end(); }

  void swap(SharedFDSet* rhs) {
    value_.swap(rhs->value_);
  }

  void Clr(const SharedFD& in) {
    value_.erase(in);
  }

  bool IsSet(const SharedFD& in) const {
    return value_.count(in) != 0;
  }

  void Set(const SharedFD& in) {
    value_.insert(in);
  }

  void Zero() {
    value_.clear();
  }

 private:
  std::set<SharedFD> value_;
};

/**
 * SharedFD version of select.
 *
 * read_set, write_set, and timeout are in/out parameters. This caller should keep
 * a copy of the original values if it wants to preserve them.
 */
int Select(SharedFDSet* read_set, SharedFDSet* write_set,
           SharedFDSet* error_set, struct timeval* timeout);

}  // namespace cuttlefish

#endif  // CUTTLEFISH_COMMON_COMMON_LIBS_FS_SHARED_SELECT_H_
