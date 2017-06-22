cc_library(
    name = "vsoc_lib",
    srcs = [
        "host_region.cpp",
    ],
    hdrs = [
        "host_region.h",
    ],
    copts = ["-Wno-unused-private-field"],
    visibility = ["//visibility:public"],
    deps = [
        "//common/libs/fs",
        "//common/vsoc/lib:vsoc_common",
        "@kernel//:uapi",
    ],
)

cc_binary(
    name = "host_region_e2e_test",
    srcs = [
        "host_region_e2e_test.cpp",
    ],
    deps = [
        ":vsoc_lib",
        "//common/vsoc/shm",
        "@glog_repo//:glog",
        "@gtest_repo//:gtest",
    ],
)

cc_binary(
    name = "host_region_postguest_e2e_test",
    srcs = [
        "host_region_postguest_e2e_test.cpp",
    ],
    deps = [
        ":vsoc_lib",
        "//common/vsoc/shm",
        "@glog_repo//:glog",
        "@gtest_repo//:gtest",
    ],
)
