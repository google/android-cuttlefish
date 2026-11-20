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

"""Macro for defining `cvd create --config_file` e2e tests."""

load("@rules_go//go:def.bzl", "go_test")

def cvd_load_test(name, config):
    """Defines a `cvd create --config_file` e2e test for one load config.

    Args:
      name: Name of the test target.
      config: Label of the JSON load config file to pass to `cvd create`.
    """
    go_test(
        name = name,
        size = "large",
        srcs = ["main_test.go"],
        data = [config],
        env = {"CVD_LOAD_CONFIG": "$(rlocationpath %s)" % config},
        tags = [
            "exclusive",
            "external",
            "no-sandbox",
            "requires_ab",
            "supports-graceful-termination",
        ],
        deps = [
            "//cvd/common",
            "@rules_go//go/runfiles",
        ],
    )
