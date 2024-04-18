sh_library(
    name = "shflags_lib",
    srcs = [
      "shflags",
    ],
)

genrule(
    name = "debs_tar",
    visibility = ["//visibility:public"],
    srcs = [],  # No input files in this example
    outs = ["debs.tar"],
    cmd = "$(location :debs_tar_builder) -o $@",
    tools = [":debs_tar_builder"],
)

sh_binary(
    name = "debs_tar_builder",
    srcs = [
      "debs_tar_builder.sh",
    ],
    deps = [
      ":shflags_lib",
      "//docker/debs-builder-docker:build-debs-with-docker",
      "//docker/debs-builder-docker:build-hostpkg",
      "//docker/debs-builder-docker:dockerfile",
      "@bazel_tools//tools/bash/runfiles",
    ],
)

genrule(
    name = "orchestration-image-tar",
    visibility = ["//visibility:public"],
    outs = ["orchestration-image.tar"],
    cmd = "$(location :orchestration-image-builder) -o $@",
    tools = [":orchestration-image-builder"],
)

sh_binary(
    name = "orchestration-image-builder",
    srcs = ["orchestration-image-builder.sh"],
    data = [":debs_tar"],
    deps = [
      "//docker/arm-server:dockerfile",
      "//docker/arm-server:run-services",
      "@bazel_tools//tools/bash/runfiles",
    ],
)
