/*
 * Copyright (C) 2026 The Android Open Source Project
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

#include "cuttlefish/pretty/pretty.h"

#include <map>
#include <memory>
#include <vector>

#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "gtest/gtest.h"

#include "cuttlefish/pretty/map.h"
#include "cuttlefish/pretty/string.h"
#include "cuttlefish/pretty/struct.h"
#include "cuttlefish/pretty/unique_ptr.h"
#include "cuttlefish/pretty/vector.h"

namespace cuttlefish {
namespace {

struct InnerStruct {
  std::string inner_string;
  int inner_number;
};

PrettyStruct Pretty(const InnerStruct& inner,
                    PrettyAdlPlaceholder unused = PrettyAdlPlaceholder()) {
  return PrettyStruct("InnerStruct")
      .Member("inner_string", inner.inner_string)
      .Member("inner_number", inner.inner_number);
}

struct OuterStruct {
  std::vector<int> number_vector;
  InnerStruct nested_member;
  std::vector<InnerStruct> nested_vector;
  std::unique_ptr<int> int_ptr_set;
  std::unique_ptr<int> int_ptr_unset;
  std::map<std::string, InnerStruct> nested_map;
};

PrettyStruct Pretty(const OuterStruct& outer,
                    PrettyAdlPlaceholder unused = PrettyAdlPlaceholder()) {
  return PrettyStruct("OuterStruct")
      .Member("number_vector", outer.number_vector)
      .Member("nested_member", outer.nested_member)
      .Member("nested_vector", outer.nested_vector)
      .Member("int_ptr_set", outer.int_ptr_set)
      .Member("int_ptr_unset", outer.int_ptr_unset)
      .Member("nested_map", outer.nested_map);
}

}  // namespace

TEST(Pretty, OuterInnerStruct) {
  OuterStruct outer{
      .number_vector = {1, 2, 3},
      .nested_member =
          {
              .inner_string = "a",
              .inner_number = 1,
          },
      .nested_vector =
          {
              {
                  .inner_string = "b",
                  .inner_number = 2,
              },
              {
                  .inner_string = "c",
                  .inner_number = 3,
              },
          },
      .int_ptr_set = std::make_unique<int>(5),
      .int_ptr_unset = std::unique_ptr<int>(),
      .nested_map =
          {
              {"d",
               {
                   .inner_string = "d",
                   .inner_number = 4,
               }},
              {"e",
               {
                   .inner_string = "e",
                   .inner_number = 5,
               }},
          },
  };

  std::string expected(absl::StripAsciiWhitespace(R"(
OuterStruct {
  number_vector: {
    1,
    2,
    3
  },
  nested_member: InnerStruct {
    inner_string: "a",
    inner_number: 1
  },
  nested_vector: {
    InnerStruct {
      inner_string: "b",
      inner_number: 2
    },
    InnerStruct {
      inner_string: "c",
      inner_number: 3
    }
  },
  int_ptr_set: 5,
  int_ptr_unset: (nullptr),
  nested_map: {
    "d" => InnerStruct {
      inner_string: "d",
      inner_number: 4
    },
    "e" => InnerStruct {
      inner_string: "e",
      inner_number: 5
    }
  }
}
  )"));

  EXPECT_EQ(absl::StrCat(Pretty(outer)), expected);
}

}  // namespace cuttlefish
