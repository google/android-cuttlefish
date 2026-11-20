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

def cvd_create_test(name, branch, target):
    """Defines a `cvd fetch` + `cvd create` e2e test for one branch/target pair.

    Args:
      name: Name of the test target.
      branch: Android Build branch to fetch from.
      target: Android Build target to fetch.
    """
    go_test(
        name = name,
        size = "large",
        srcs = ["main_test.go"],
        tags = [
            "exclusive",
            "external",
            "no-sandbox",
            "requires_ab",
            "supports-graceful-termination",
        ],
        # Unqualified keys are resolved against the test's own import path.
        x_defs = {
            "branch": branch,
            "target": target,
        },
        deps = ["//cvd/common"],
    )
