//
// Copyright (C) 2026 The Android Open Source Project
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

#pragma once

namespace cuttlefish {

struct PrettyAdlPlaceholder {};

/**
 * This is the fallback pretty-printing case: if abseil can print the value,
 * have abseil do it.
 *
 * This function is intended to be overloaded. Overloads for common types are in
 * this directory, but other types can also define their own overloads.
 *
 * Not all overloads will return `std::string`. The returned type should be
 * formattable by abseil, libfmt, and ostreams. This allows deferring formatting
 * to avoid allocating fewer intermediate strings.
 *
 * If a type defines both a cuttlefish::Pretty overload and an AbslStringify
 * hook, the cuttlefish::Pretty implementation is preferred by overload
 * resolution as it is a more specific type. However, if the cuttlefish::Pretty
 * overload is defined in a different header, the presence of the header affects
 * which overloads are in scope, so leaving the header include out could still
 * compile but unintentionally refer to this default fallback. Therefore,
 * cuttlefish::Pretty overloads should be declared together with the header.
 *
 * Worse than that, the `PrettyAdlPlaceHolder()` hack is there to trigger
 * argument-dependent lookup within `namespace cuttlefish`: otherwise the
 * `Pretty` overloads for non-`namespace cuttlefish` types will only be detected
 * based on declaration order through standard name lookup rules. Having an
 * argument type declared within `namespace cuttlefish` forces it to search the
 * entire namespace rather than only declarations defined earlier in the
 * translation unit. This is relevant for `Pretty` invocations within
 * `PrettyContainer` and `PrettyStruct`.
 */
template <typename T>
const T& Pretty(const T& value,
                PrettyAdlPlaceholder unused = PrettyAdlPlaceholder()) {
  return value;
}

}  // namespace cuttlefish
