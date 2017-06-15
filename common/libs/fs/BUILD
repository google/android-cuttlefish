cc_library(
    name = "fs",
    srcs = [
        "gce_fs.cpp",
        "gce_fs.h",
        "shared_fd.cpp",
        "shared_fd.h",
        "shared_select.h",
    ],
    hdrs = [
        "gce_fs.h",
        "shared_fd.h",
        "shared_select.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//common/libs/auto_resources",
    ],
)

cc_test(
    name = "shared_fd_test",
    srcs = [
        "shared_fd_test.cpp",
    ],
    deps = [
        ":fs",
        "@glog_repo//:glog",
        "@gtest_repo//:gtest_main",
    ],
)
