load("//cuttlefish/bazel:rules.bzl", "cf_cc_library")

package(
    default_visibility = ["//:android_cuttlefish"],
)

cf_cc_library(
    name = "screen_recording_controls",
    srcs = [
        "screen_recording_controls.cc",
    ],
    hdrs = [
        "screen_recording_controls.h",
    ],
    deps = [
        "//cuttlefish/common/libs/fs",
        "//cuttlefish/host/libs/command_util",
        "//cuttlefish/host/libs/command_util:libcuttlefish_run_cvd_proto",
        "//cuttlefish/host/libs/config:cuttlefish_config",
        "//cuttlefish/result",
    ],
)
