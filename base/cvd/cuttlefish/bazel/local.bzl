# Copyright (C) 2023 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#       http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""
Drop-in replacement for `{new_,}local_repository` such that
paths are resolved against the module it is called from.
"""

def _common_attrs():
    return {
        "path": attr.label(
            doc = "a label representing the path relative to the module where it is called",
            mandatory = True,
        ),
    }

def _local_repository_impl(repository_ctx):

    target = repository_ctx.path(repository_ctx.attr.path)
    for child in target.readdir():
        repository_ctx.symlink(child, repository_ctx.path(child.basename))

local_repository = repository_rule(
    attrs = _common_attrs(),
    implementation = _local_repository_impl,
)

def _new_local_repository_impl(repository_ctx):
    _local_repository_impl(repository_ctx)

    # For now, new_local_repository requires that the original directory does
    # not already have a BUILD file. We could relax this if we hit this error in
    # the future.
    if repository_ctx.path("BUILD.bazel").exists:
        fail("{} already exists. Use local_repository instead.".format(
            repository_ctx.path("BUILD.bazel"),
        ))
    if repository_ctx.path("BUILD").exists:
        fail("{} already exists. Use local_repository instead.".format(
            repository_ctx.path("BUILD"),
        ))

    repository_ctx.symlink(
        repository_ctx.path(repository_ctx.attr.build_file),
        repository_ctx.path("BUILD.bazel"),
    )

    # Create repo boundary marker.
    if not repository_ctx.path("REPO.bazel").exists:
        repository_ctx.file(repository_ctx.path("REPO.bazel"), "")

new_local_repository = repository_rule(
    attrs = _common_attrs() | {
        "build_file": attr.label(doc = "build file. Path is calculated with `repository_ctx.path(build_file)`"),
    },
    implementation = _new_local_repository_impl,
)
