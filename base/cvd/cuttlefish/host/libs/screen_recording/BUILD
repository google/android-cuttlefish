load("//cuttlefish/bazel:rules.bzl", "cf_cc_library")

package(
    default_visibility = ["//:android_cuttlefish"],
)

cf_cc_library(
    name = "screen_recording",
    srcs = [
        "screen_recording.cc",
    ],
    hdrs = [
        "screen_recording.h",
    ],
    deps = [
        "//cuttlefish/common/libs/fs",
        "//cuttlefish/common/libs/utils:result",
        "//cuttlefish/host/libs/command_util",
        "//cuttlefish/host/libs/command_util:libcuttlefish_run_cvd_proto",
        "//cuttlefish/host/libs/config:cuttlefish_config",
    ],
)
