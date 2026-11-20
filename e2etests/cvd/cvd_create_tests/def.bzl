# Copyright (C) 2026 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Macro for defining `cvd fetch` + `cvd create` e2e tests."""

load("@rules_go//go:def.bzl", "go_test")

TAGS = [
    "exclusive",
    "external",
    "no-sandbox",
    "requires_ab",
    "supports-graceful-termination",
]

def cvd_create_test(name, branch, target):
    """Defines the `cvd create` e2e tests for one branch/target pair.

    Two targets are created: `<name>`, which runs against the host package
    fetched from the build, and `<name>_additional_substitution`, which
    additionally substitutes the locally built debian packages.

    Args:
      name: Base name of the test targets.
      branch: Android Build branch to fetch from.
      target: Android Build target to fetch.
    """

    # Unqualified keys are resolved against the test's own import path.
    x_defs = {
        "branch": branch,
        "target": target,
    }
    go_test(
        name = name,
        size = "large",
        srcs = ["main_test.go"],
        tags = TAGS,
        x_defs = x_defs,
        deps = ["//cvd/common"],
    )
    go_test(
        name = name + "_additional_substitution",
        size = "large",
        srcs = ["main_test.go"],
        data = ["//:debian_substitution_marker"],
        env = {"LOCAL_DEBIAN_SUBSTITUTION_MARKER_FILE": "$(rlocationpath //:debian_substitution_marker)"},
        tags = TAGS,
        x_defs = x_defs,
        deps = ["//cvd/common"],
    )
