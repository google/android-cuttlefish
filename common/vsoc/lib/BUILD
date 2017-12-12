cc_library(
    name = "vsoc_common",
    srcs = [
        "e2e_test_region.cpp",
        "region_common.cpp",
    ],
    hdrs = [
        "region.h",
    ],
    copts = ["-Wno-unused-private-field"],
    visibility = ["//visibility:public"],
    deps = [
        "//common/libs/fs",
        "@kernel//:uapi",
    ],
)
