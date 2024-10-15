# Copyright (C) 2024 The Android Open Source Project
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

load("@io_bazel_rules_go//go:def.bzl", "go_test")

def create_single_instance_test(name, build_id, build_target):
    go_test(
        name = name,
        srcs = ["main_test.go"],
        data = [
            "@images//docker:orchestration_image_tar",
        ],
        env = {
            "BUILD_ID": build_id,
            "BUILD_TARGET": build_target,
        },
        deps = [
            "//orchestration/common",
            "@com_github_google_cloud_android_orchestration//pkg/client",
            "@com_github_google_android_cuttlefish_frontend_src_host_orchestrator//api/v1:api",
            "@com_github_google_go_cmp//cmp",
            "@libhoclient",
        ],
    )
